/*
 * Copyright (C) 2023 The Coral Project
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

#ifndef CORAL_PLAYBACK_UPSTREAM_BANDWIDTH_METER_H
#define CORAL_PLAYBACK_UPSTREAM_BANDWIDTH_METER_H

#include <core/task/Mutex.h>

#include <core/event/EventHandler.h>

#include <memory>
#include <vector>

namespace coral {

/**
 * Provides estimates of the currently available bandwidth.
 */
class BandwidthMeter {
public:
  virtual ~BandwidthMeter() = default;

  /**
   * A listener of {@link BandwidthMeter} events.
   */
  class EventListener {
  public:
    virtual ~EventListener() = default;

    /**
     * Called periodically to indicate that bytes have been transferred or the
     * estimated bitrate has changed.
     *
     * <p>Note: The estimated bitrate is typically derived from more information
     * than just
     * {@code bytes} and {@code elapsedMs}.
     *
     * @param elapsedMs The time taken to transfer {@code bytesTransferred}, in
     * milliseconds. This is at most the elapsed time since the last callback,
     * but may be less if there were periods during which data was not being
     * transferred.
     * @param bytesTransferred The number of bytes transferred since the last
     * callback.
     * @param bitrateEstimate The estimated bitrate in bits/sec.
     */
    virtual void onBandwidthSample(int elapsedMs, int64_t bytesTransferred,
        int64_t bitrateEstimate) = 0;
  };

  /**
   * Returns the estimated bitrate.
   */
  virtual int64_t getBitrateEstimate() = 0;

  /**
   * Adds an {@link EventListener}.
   * @param eventHandler A handler for events.
   * @param listener A listener of events.
   */
  virtual void
  addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
      const std::shared_ptr<EventListener>& listener) = 0;

  /**
   * Removes an {@link EventListener}.
   *
   * @param listener The listener to be removed.
   */
  virtual void
  removeEventListener(const std::shared_ptr<EventListener>& listener) = 0;

protected:
  /** Event dispatcher which allows listener registration. */
  class EventDispatcher {
  public:
    EventDispatcher() = default;

    ~EventDispatcher() = default;

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    void addListener(const std::shared_ptr<EventHandler>& handler,
        const std::shared_ptr<EventListener>& listener) {
      if (listener == nullptr) {
        return;
      }
      Lock lock(listenersLock);
      removeListenerLocked(listener);
      listeners.push_back(std::make_shared<HandlerAndListener>(handler, listener));
    }

    void removeListener(const std::shared_ptr<EventListener>& listener) {
      Lock lock(listenersLock);
      removeListenerLocked(listener);
    }

    void bandwidthSample(int elapsedMs, int64_t bytesTransferred,
        int64_t bitrateEstimate) {
      // The listeners are copied out of the list before they are called: the sample is produced by
      // the thread that reads the media while the application registers and unregisters listeners,
      // and the dispatch must not run with the lock held.
      std::vector<std::shared_ptr<HandlerAndListener>> currentListeners;
      {
        Lock lock(listenersLock);
        if (released) {
          return;
        }
        currentListeners = listeners;
      }
      for (auto const& handlerAndListener: currentListeners) {
        const auto listener = handlerAndListener->listener;
        const auto& handler = handlerAndListener->handler;
        const auto sample = [listener, elapsedMs, bytesTransferred, bitrateEstimate] {
          if (listener != nullptr) {
            listener->onBandwidthSample(elapsedMs, bytesTransferred, bitrateEstimate);
          }
        };
        if (handler != nullptr) {
          handler->post(sample);
        } else {
          // No handler: the listener is called inline, on the thread that produced the sample.
          sample();
        }
      }
    }

    /** Stops the dispatch of further samples. Called when the meter that owns it is released. */
    void release() {
      Lock lock(listenersLock);
      released = true;
      listeners.clear();
    }

  private:
    class HandlerAndListener {
    public:
      std::shared_ptr<EventHandler> handler;
      std::shared_ptr<EventListener> listener;

      explicit HandlerAndListener(
          std::shared_ptr<EventHandler> handler,
          std::shared_ptr<EventListener> listener) noexcept
          : handler(std::move(handler)), listener(std::move(listener)) {}
    };

    void removeListenerLocked(const std::shared_ptr<EventListener>& listener) {
      auto iterator = listeners.begin();
      while (iterator != listeners.end()) {
        if ((*iterator)->listener == listener) {
          iterator = listeners.erase(iterator);
        } else {
          ++iterator;
        }
      }
    }

    /** Guards {@link #listeners} and {@link #released}. */
    mutable Mutex listenersLock;
    std::vector<std::shared_ptr<HandlerAndListener>> listeners;
    bool released = false;
  };
};

} // namespace coral

#endif // CORAL_PLAYBACK_UPSTREAM_BANDWIDTH_METER_H
