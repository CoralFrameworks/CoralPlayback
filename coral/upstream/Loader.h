/*
 * Copyright (C) 2025 The Coral Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CORAL_UPSTREAM_LOADER_H
#define CORAL_UPSTREAM_LOADER_H

#include <core/task/Mutex.h>

#include <core/event/EventHandler.h>
#include <core/event/EventLooper.h>
#include <core/event/EventThread.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace coral {

/**
 * Thrown by {@link Loader::startLoading} when the loader cannot accept work.
 */
class LoaderException : public std::runtime_error {
public:
  explicit LoaderException(const std::string& message)
      : std::runtime_error(message) {}
};

/**
 * A unit of work the Loader runs on its background thread.
 *
 * Implementations must be reentrant against {@link cancelLoad}: the loader
 * signals a cancellation from whichever thread asks for it, and the running
 * `load()` on the loader thread must observe that signal and return promptly.
 * `load()` may throw any `std::exception` to report a failure; the loader
 * converts it into a {@link LoaderCallback::onLoadError} with the option to
 * retry.
 */
class Loadable {
public:
  virtual ~Loadable() = default;

  /** Requests that an in-flight `load()` return as soon as possible. */
  virtual void cancelLoad() = 0;

  /** Performs the load, blocking until done or cancelled. */
  virtual void load() = 0;

  /**
   * The message of the failure that stopped the last `load()`, or an empty string when the load
   * ended normally.
   *
   * `load()` may also throw to report a failure, but throwing is not usable everywhere: the Android
   * build takes the unwinder from the prebuilt ffmpeg library, whose unwinder cannot unwind coral's
   * frames, so a throw aborts the process instead of reaching the loader. Report the failure here in
   * that case; the loader treats a non-empty message exactly like a caught exception.
   *
   * @return The failure message, or an empty string.
   */
  [[nodiscard]] virtual std::string getLoadErrorMessage() const {
    return {};
  }
};

/**
 * What the loader should do after {@link LoaderCallback::onLoadError}.
 *
 * Mirrors ExoPlayer's `LoadErrorAction`: a retry re-runs `load()` on the loader
 * thread after an optional delay, a cancel ends the load for good.
 */
class LoadErrorAction {
public:
  enum class Type { kRetry, kCancel };

  static LoadErrorAction Retry(const int64_t retryDelayMs = 0) noexcept {
    return LoadErrorAction{ Type::kRetry, retryDelayMs };
  }

  static LoadErrorAction Cancel() noexcept {
    return LoadErrorAction{ Type::kCancel, 0 };
  }

  [[nodiscard]] Type type() const noexcept { return mType; }
  [[nodiscard]] int64_t retryDelayMs() const noexcept { return mRetryDelayMs; }

private:
  LoadErrorAction(const Type type, const int64_t delay) noexcept
      : mType(type),
        mRetryDelayMs(delay) {}

  Type mType;
  int64_t mRetryDelayMs;
};

/**
 * Receives the outcome of a load. Every method is called on the thread that
 * owns the callback looper the loader delivers on, never on the loader thread
 * (unless no looper is available at all, see {@link Loader}).
 */
template<typename T>
class LoaderCallback {
public:
  virtual ~LoaderCallback() = default;

  virtual void onLoadCompleted(T* loadable,
      int64_t startTimeMs,
      int64_t loadDurationMs) = 0;

  virtual void onLoadCanceled(T* loadable,
      int64_t startTimeMs,
      int64_t loadDurationMs,
      bool released) = 0;

  virtual LoadErrorAction onLoadError(T* loadable,
      const std::string& errorMessage,
      int errorCount,
      int64_t startTimeMs,
      int64_t loadDurationMs) = 0;
};

/**
 * Notified once the loader has fully stopped.
 */
class ReleaseCallback {
public:
  virtual ~ReleaseCallback() = default;

  virtual void onLoaderReleased() = 0;
};

/**
 * Runs a {@link Loadable} on a background thread and reports back on the looper
 * the loader was told to deliver on.
 *
 * @section lifecycle Lifetime
 *
 * The loader owns a background `EventThread` created from `threadName`, and it
 * uses a callback looper purely to post results back. Like ExoPlayer's
 * `Loader`, the callback looper defaults to the looper of the thread that
 * starts the load (ExoPlayer's `new Handler()`), which keeps the results on the
 * thread that owns the loadable; callers that know their looper up front can
 * pass it to the constructor instead.
 *
 * Callers are expected to keep the callback and loadable alive until the
 * corresponding completion callback arrives, and to call `release()` before
 * dropping the loader.
 *
 * Destroying the loader from its own loader thread is not supported: the
 * background thread is detached in that case (so it cannot join itself) but the
 * loader object it would still touch must outlive it.
 */
class Loader final {
public:
  /**
   * @param callbackLooper looper the callbacks are delivered on, or null to use the looper of the
   *                       thread that starts the first load.
   * @param threadName     name given to the background loader thread.
   */
  explicit Loader(std::shared_ptr<EventLooper> callbackLooper = nullptr,
      std::string threadName = "Coral-Loader");

  ~Loader();

  Loader(const Loader&) = delete;
  Loader& operator=(const Loader&) = delete;
  Loader(Loader&&) = delete;
  Loader& operator=(Loader&&) = delete;

  /**
   * Starts a load. Returns the start time in milliseconds.
   *
   * <p>The loader takes a reference to the loadable and keeps it until the completion callback has
   * run, so that a callback that is delivered after the loading state was cleared can never
   * dereference a loadable its owner already destroyed. The caller is free to replace its own
   * reference as soon as this method returns.
   *
   * @throws LoaderException if a load is already running, or the loader has been
   *         released, or the loader thread has stopped accepting work.
   */
  template<typename T>
  int64_t startLoading(std::shared_ptr<T> loadable, LoaderCallback<T>* callback) {
    if (loadable == nullptr) {
      throw LoaderException("Loader cannot load a null loadable");
    }
    std::shared_ptr<LoadTaskBase> task;
    int64_t startTimeMs = 0;
    {
      Lock lock(mMutex);
      if (mReleased.load(std::memory_order_acquire)) {
        throw LoaderException("Loader has been released");
      }
      if (mLoading.load(std::memory_order_acquire)) {
        throw LoaderException("Loader is already loading");
      }

      auto newTask =
          std::make_shared<LoadTask<T>>(this, std::move(loadable), callback);
      startTimeMs = newTask->startTimeMs();
      mCurrentTask = newTask;
      mLoading.store(true, std::memory_order_release);
      task = std::move(newTask);
    }

    // Like ExoPlayer's Loader, the callback handler belongs to the thread that starts the load when
    // no looper was given up front.
    ensureCallbackHandler();

    if (!mLoadHandler->post([task] { task->run(); })) {
      Lock lock(mMutex);
      mCurrentTask.reset();
      mLoading.store(false, std::memory_order_release);
      throw LoaderException("Loader thread refused the task");
    }
    return startTimeMs;
  }

  /**
   * Asks the running load to stop. Safe to call from any thread, and a no-op
   * when no load is running.
   */
  void cancelLoading();

  /**
   * Shuts the loader down. Afterwards `startLoading` will throw.
   *
   * The release is one-shot: a second release is a no-op. The loader thread is
   * stopped before the callback runs, so everything the callback releases (the
   * sample queues, the extractor) is no longer written to.
   *
   * @param callback receives `onLoaderReleased` once the loader thread has
   *                 stopped and the completion callbacks have been posted. The
   *                 callback is delivered on the callback looper, or inline on
   *                 the calling thread when there is no callback looper.
   */
  void release(ReleaseCallback* callback = nullptr);

  [[nodiscard]] bool isLoading() const noexcept {
    return mLoading.load(std::memory_order_acquire);
  }

private:
  /** Type-erased handle to a `LoadTask<T>` so the Loader can own one without
   *  being templated. */
  class LoadTaskBase {
  public:
    virtual ~LoadTaskBase() = default;
    virtual void cancel() = 0;
    virtual void release() = 0;
    virtual void run() = 0;
  };

  template<typename T>
  class LoadTask final
      : public LoadTaskBase,
        public std::enable_shared_from_this<LoadTask<T>> {
  public:
    LoadTask(Loader* loader, std::shared_ptr<T> loadable, LoaderCallback<T>* callback)
        : mLoader(loader),
          mLoadable(std::move(loadable)),
          mCallback(callback),
          mStartTimeMs(currentTimeMs()) {}

    void cancel() override {
      // Exactly one caller flips the flag and triggers `cancelLoad`; a second
      // cancel is a no-op so an already-cancelled loadable is not disturbed.
      bool expected = false;
      if (mCanceled.compare_exchange_strong(expected, true,
              std::memory_order_acq_rel)) {
        mLoadable->cancelLoad();
      }
    }

    void release() override {
      mReleased.store(true, std::memory_order_release);
      cancel();
    }

    void run() override {
      auto self = this->shared_from_this();
      const int64_t startMs = mStartTimeMs;

      bool success = false;
      std::string errorMessage;
      try {
        mLoadable->load();
        success = true;
      } catch (const std::exception& e) {
        errorMessage = e.what();
      } catch (...) {
        errorMessage = "unknown error";
      }

      if (success) {
        // A loadable may report the failure through `getLoadErrorMessage()` instead of throwing,
        // which is the only option where unwinding is not available.
        errorMessage = mLoadable->getLoadErrorMessage();
        if (!errorMessage.empty()) {
          success = false;
        }
      }

      const bool canceled = mCanceled.load(std::memory_order_acquire);
      const bool released = mReleased.load(std::memory_order_acquire);

      if (success) {
        // Clear the running state before the callback so a caller that starts
        // a new load from within it sees a free loader.
        mLoader->onTaskFinished(this);
        if (!released) {
          mLoader->dispatchCallback([self, startMs, canceled] {
            const int64_t durationMs = currentTimeMs() - startMs;
            if (canceled) {
              const auto loadable = self->mLoadable.get();
            self->mCallback->onLoadCanceled(loadable, startMs, durationMs, false);
            } else {
              const auto loadable = self->mLoadable.get();
            self->mCallback->onLoadCompleted(loadable, startMs, durationMs);
            }
          });
        }
        return;
      }

      // A load that failed because it was cancelled or released is not a
      // reportable error: end the task quietly.
      if (canceled || released) {
        mLoader->onTaskFinished(this);
        return;
      }

      const int errorCount = ++mErrorCount;
      mLoader->dispatchCallback([self, startMs, errorMessage, errorCount] {
        const int64_t durationMs = currentTimeMs() - startMs;
        const LoadErrorAction action = self->mCallback->onLoadError(
            self->mLoadable.get(), errorMessage, errorCount, startMs, durationMs);

        if (action.type() == LoadErrorAction::Type::kRetry) {
          // Re-post onto the loader thread, optionally after a delay.
          self->mLoader->postLoadTask(self, action.retryDelayMs());
        } else {
          self->mLoader->onTaskFinished(self.get());
        }
      });
    }

    [[nodiscard]] int64_t startTimeMs() const noexcept { return mStartTimeMs; }

  private:
    Loader* mLoader;
    /**
     * The loadable, owned for as long as the task can reach it.
     *
     * <p>The task is kept alive by the closures it posts to the callback looper, so the loadable
     * outlives the completion callback even though the loading state was cleared before it was
     * dispatched.
     */
    std::shared_ptr<T> mLoadable;
    LoaderCallback<T>* mCallback;
    const int64_t mStartTimeMs;

    std::atomic<bool> mCanceled{ false };
    std::atomic<bool> mReleased{ false };
    int mErrorCount = 0;
  };

  /** Creates the callback handler from the preferred looper, or from this thread's looper. */
  void ensureCallbackHandler();

  /** Clears the running task once it has stopped. */
  void onTaskFinished(LoadTaskBase* task);

  /**
   * Posts a callback onto the callback looper. Without a callback looper the callback runs inline on
   * the calling thread, which is the loader thread for the load results.
   */
  void dispatchCallback(std::function<void()> callback);

  /** Re-posts a load onto the loader thread, optionally after a delay. */
  void postLoadTask(std::shared_ptr<LoadTaskBase> task, int64_t delayMs);

  /** Milliseconds from `std::chrono::steady_clock::now()`, the same clock the framework uses. */
  [[nodiscard]] static int64_t currentTimeMs() noexcept;

  /** The looper the caller asked the callbacks to be delivered on, may be null. */
  const std::shared_ptr<EventLooper> mPreferredCallbackLooper;

  /** Where the callbacks are delivered. Created on the thread that starts the first load. */
  std::shared_ptr<EventHandler> mCallbackHandler;

  /** The background thread that runs `Loadable::load()`. */
  EventThread mLoadThread;

  /** Handles posted onto `mLoadThread`, used to submit and retry loads. */
  std::shared_ptr<EventHandler> mLoadHandler;

  /** Guards `mCurrentTask`, `mLoading` and `mReleased` transitions. */
  mutable Mutex mMutex;

  std::shared_ptr<LoadTaskBase> mCurrentTask;
  std::atomic<bool> mLoading{ false };
  std::atomic<bool> mReleased{ false };
};

} // namespace coral

#endif // CORAL_UPSTREAM_LOADER_H
