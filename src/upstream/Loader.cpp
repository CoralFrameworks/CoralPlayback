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

#include <coral/upstream/Loader.h>

#include <chrono>
#include <utility>

namespace coral {

Loader::Loader(std::shared_ptr<EventLooper> callbackLooper, std::string threadName)
    : mPreferredCallbackLooper(std::move(callbackLooper)),
      mLoadThread(std::move(threadName)) {
  // `start()` returns once the loader thread's looper is live, so `getLooper()`
  // cannot be null here and no polling is needed.
  mLoadThread.start();
  mLoadHandler = EventHandler::create(mLoadThread.getLooper(), nullptr);
  if (mLoadHandler == nullptr) {
    throw LoaderException("Failed to create loader handler");
  }
}

Loader::~Loader() {
  release(nullptr);
}

void Loader::cancelLoading() {
  // The shared pointer is taken under the lock and used after it: `cancel()` can
  // call into user code (`Loadable::cancelLoad()`), which must not run with the
  // loader's own mutex held.
  std::shared_ptr<LoadTaskBase> task;
  {
    Lock lock(mMutex);
    task = mCurrentTask;
  }
  if (task != nullptr) {
    task->cancel();
  }
}

void Loader::release(ReleaseCallback* callback) {
  // One-shot: a second release is a no-op, so a caller that also lets the
  // destructor run does not shut the loader down twice.
  std::shared_ptr<LoadTaskBase> task;
  {
    Lock lock(mMutex);
    if (mReleased.load(std::memory_order_acquire)) {
      return;
    }
    mReleased.store(true, std::memory_order_release);
    task = mCurrentTask;
  }

  if (task != nullptr) {
    task->release();
  }

  // Stop the loader thread. `quit()` joins it when it is called from another thread and detaches when
  // it is called from the loader thread itself, so it can never join the current thread. A running
  // `load()` must have been woken by the `cancel()` above; otherwise the join blocks until it
  // returns, which is the same contract ExoPlayer has.
  if (mLoadThread.isAlive()) {
    mLoadThread.quit();
  }

  if (callback == nullptr) {
    return;
  }
  // Deliver the release on the callback looper, so the owner releases its resources on the thread
  // that owns them. This only happens after the loader thread has stopped.
  if (mCallbackHandler != nullptr) {
    // Non-owning shared pointer: the caller keeps ownership, and the queued
    // closure holds the reference alive only for the duration of the post.
    auto shared = std::shared_ptr<ReleaseCallback>(callback, [](ReleaseCallback*) {});
    if (mCallbackHandler->post([shared] {
          shared->onLoaderReleased();
        })) {
      return;
    }
  }
  // No callback looper, or its looper is gone: report the release inline instead of dropping it.
  // Dropping it would leave the owner's resources (sample queues, extractor) allocated for good.
  callback->onLoaderReleased();
}

void Loader::ensureCallbackHandler() {
  if (mCallbackHandler != nullptr) {
    return;
  }
  // Like ExoPlayer's Loader, the handler belongs to the looper of the thread that starts the load
  // when the caller did not pass one.
  auto looper = mPreferredCallbackLooper != nullptr ? mPreferredCallbackLooper : EventLooper::myLooper();
  if (looper == nullptr) {
    // The calling thread has no looper: results are delivered inline on the loader thread, which is
    // what the previous implementation did. Callers must then keep the callback object alive for as
    // long as the load runs.
    return;
  }
  mCallbackHandler = EventHandler::create(looper, nullptr);
}

void Loader::onTaskFinished(LoadTaskBase* task) {
  Lock lock(mMutex);
  if (mCurrentTask.get() == task) {
    mCurrentTask.reset();
    mLoading.store(false, std::memory_order_release);
  }
}

void Loader::dispatchCallback(std::function<void()> callback) {
  if (mCallbackHandler != nullptr) {
    // The post may fail when the caller's looper has already quit; the work is then dropped, which
    // matches ExoPlayer's behaviour on a dead Handler.
    mCallbackHandler->post(std::move(callback));
    return;
  }
  callback();
}

void Loader::postLoadTask(std::shared_ptr<LoadTaskBase> task, const int64_t delayMs) {
  if (delayMs <= 0) {
    mLoadHandler->post([task = std::move(task)] {
      task->run();
    });
    return;
  }
  mLoadHandler->postDelay(
      [task = std::move(task)] {
        task->run();
      },
      std::chrono::milliseconds(delayMs));
}

int64_t Loader::currentTimeMs() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace coral
