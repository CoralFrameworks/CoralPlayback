/*
 * Copyright (C) 2026 The Coral Project
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
#include <coral/analytics/AnalyticsCollector.h>

#include <core/time/SystemClock.h>

#include <algorithm>
#include <utility>

namespace coral {

AnalyticsCollector::AnalyticsCollector() = default;

void AnalyticsCollector::addListener(const std::shared_ptr<AnalyticsListener>& listener) {
  if (listener == nullptr) {
    return;
  }
  Lock lock(listenersLock);
  listeners.push_back(listener);
}

void AnalyticsCollector::removeListener(const std::shared_ptr<AnalyticsListener>& listener) {
  Lock lock(listenersLock);
  const auto position = std::find(listeners.begin(), listeners.end(), listener);
  if (position != listeners.end()) {
    listeners.erase(position);
  }
}

int AnalyticsCollector::getListenerCount() const {
  Lock lock(listenersLock);
  return static_cast<int>(listeners.size());
}

void AnalyticsCollector::setState(const std::shared_ptr<Timeline>& newTimeline,
    const int newWindowIndex, const std::optional<MediaPeriodId>& newMediaPeriodId,
    const int64_t newPositionMs, const int64_t newBufferedDurationMs) {
  Lock lock(stateLock);
  if (newTimeline != nullptr) {
    timeline = newTimeline;
  }
  windowIndex = newWindowIndex;
  mediaPeriodId = newMediaPeriodId;
  positionMs = newPositionMs;
  bufferedDurationMs = newBufferedDurationMs;
}

EventTime AnalyticsCollector::createEventTime(const int eventWindowIndex,
    const std::optional<MediaPeriodId>& eventMediaPeriodId) const {
  EventTime eventTime;
  {
    Lock lock(stateLock);
    eventTime.timeline = timeline;
    eventTime.windowIndex = windowIndex;
    eventTime.mediaPeriodId = mediaPeriodId;
    eventTime.eventPlaybackPositionMs = positionMs;
    eventTime.totalBufferedDurationMs = bufferedDurationMs;
  }
  // An event that belongs to a window or a period of its own reports the ones it belongs to, so that
  // a listener can attribute it correctly even when the player has already moved on.
  if (eventWindowIndex != Definitions::kIndexUnset) {
    eventTime.windowIndex = eventWindowIndex;
  }
  if (eventMediaPeriodId.has_value()) {
    eventTime.mediaPeriodId = eventMediaPeriodId;
  }
  eventTime.eventTimeMs = SystemClock::CurrentTimeMillis();
  eventTime.currentTimelineWindowCount =
    eventTime.timeline != nullptr ? eventTime.timeline->getWindowCount() : 0;
  return eventTime;
}

void AnalyticsCollector::dispatch(const std::function<void(AnalyticsListener&)>& action) {
  // The listeners are copied under the lock and called outside it: a listener that registers or
  // unregisters another one from its callback must not deadlock the collector.
  auto snapshot = std::vector<std::shared_ptr<AnalyticsListener>>();
  {
    Lock lock(listenersLock);
    snapshot = listeners;
  }
  for (const auto& listener : snapshot) {
    if (listener != nullptr) {
      action(*listener);
    }
  }
}

// ==================================================================================================
// The player events
// ==================================================================================================

void AnalyticsCollector::onPlaybackStateChanged(const Player::State& state) {
  const auto eventTime = createEventTime(Definitions::kIndexUnset, std::nullopt);
  dispatch([&eventTime, &state](AnalyticsListener& listener) {
    listener.onPlaybackStateChanged(eventTime, state);
  });
}

void AnalyticsCollector::onPlayWhenReadyChanged(const bool playWhenReady) {
  const auto eventTime = createEventTime(Definitions::kIndexUnset, std::nullopt);
  dispatch([&eventTime, playWhenReady](AnalyticsListener& listener) {
    listener.onPlayWhenReadyChanged(eventTime, playWhenReady);
  });
}

void AnalyticsCollector::onIsLoadingChanged(const bool isLoading) {
  const auto eventTime = createEventTime(Definitions::kIndexUnset, std::nullopt);
  dispatch([&eventTime, isLoading](AnalyticsListener& listener) {
    listener.onIsLoadingChanged(eventTime, isLoading);
  });
}

void AnalyticsCollector::onPositionDiscontinuity(const int64_t positionUs) {
  const auto eventTime = createEventTime(Definitions::kIndexUnset, std::nullopt);
  dispatch([&eventTime, positionUs](AnalyticsListener& listener) {
    listener.onPositionDiscontinuity(eventTime, positionUs);
  });
}

void AnalyticsCollector::onPlaybackParametersChanged(const Parameters& parameters) {
  const auto eventTime = createEventTime(Definitions::kIndexUnset, std::nullopt);
  dispatch([&eventTime, &parameters](AnalyticsListener& listener) {
    listener.onPlaybackParametersChanged(eventTime, parameters);
  });
}

void AnalyticsCollector::onRepeatModeChanged(const RepeatMode& repeatMode) {
  const auto eventTime = createEventTime(Definitions::kIndexUnset, std::nullopt);
  dispatch([&eventTime, &repeatMode](AnalyticsListener& listener) {
    listener.onRepeatModeChanged(eventTime, repeatMode);
  });
}

void AnalyticsCollector::onVolumeChanged(const float volume) {
  const auto eventTime = createEventTime(Definitions::kIndexUnset, std::nullopt);
  dispatch([&eventTime, volume](AnalyticsListener& listener) {
    listener.onVolumeChanged(eventTime, volume);
  });
}

void AnalyticsCollector::onVideoSizeChanged(const int width, const int height) {
  const auto eventTime = createEventTime(Definitions::kIndexUnset, std::nullopt);
  dispatch([&eventTime, width, height](AnalyticsListener& listener) {
    listener.onVideoSizeChanged(eventTime, width, height);
  });
}

void AnalyticsCollector::onPlayerError(const std::string& errorMessage) {
  const auto eventTime = createEventTime(Definitions::kIndexUnset, std::nullopt);
  dispatch([&eventTime, &errorMessage](AnalyticsListener& listener) {
    listener.onPlayerError(eventTime, errorMessage);
  });
}

// ==================================================================================================
// The media source events
// ==================================================================================================

void AnalyticsCollector::onMediaPeriodCreated(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId) {
  const auto eventTime = createEventTime(windowIndex, mediaPeriodId);
  dispatch([&eventTime](AnalyticsListener& listener) {
    listener.onMediaPeriodCreated(eventTime);
  });
}

void AnalyticsCollector::onMediaPeriodReleased(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId) {
  const auto eventTime = createEventTime(windowIndex, mediaPeriodId);
  dispatch([&eventTime](AnalyticsListener& listener) {
    listener.onMediaPeriodReleased(eventTime);
  });
}

void AnalyticsCollector::onLoadStarted(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId, const LoadEventInfo& loadEventInfo,
    const MediaLoadData& mediaLoadData) {
  static_cast<void>(loadEventInfo);
  const auto eventTime = createEventTime(windowIndex, mediaPeriodId);
  dispatch([&eventTime, &mediaLoadData](AnalyticsListener& listener) {
    listener.onLoadStarted(eventTime, mediaLoadData);
  });
}

void AnalyticsCollector::onLoadCompleted(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId, const LoadEventInfo& loadEventInfo,
    const MediaLoadData& mediaLoadData) {
  static_cast<void>(loadEventInfo);
  const auto eventTime = createEventTime(windowIndex, mediaPeriodId);
  dispatch([&eventTime, &mediaLoadData](AnalyticsListener& listener) {
    listener.onLoadCompleted(eventTime, mediaLoadData);
  });
}

void AnalyticsCollector::onLoadCanceled(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId, const LoadEventInfo& loadEventInfo,
    const MediaLoadData& mediaLoadData) {
  static_cast<void>(loadEventInfo);
  const auto eventTime = createEventTime(windowIndex, mediaPeriodId);
  dispatch([&eventTime, &mediaLoadData](AnalyticsListener& listener) {
    listener.onLoadCanceled(eventTime, mediaLoadData);
  });
}

void AnalyticsCollector::onLoadError(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId, const LoadEventInfo& loadEventInfo,
    const MediaLoadData& mediaLoadData, const Error* error, const bool wasCanceled) {
  static_cast<void>(loadEventInfo);
  const auto eventTime = createEventTime(windowIndex, mediaPeriodId);
  const auto message = error != nullptr ? error->failureReason : std::string();
  dispatch([&eventTime, &mediaLoadData, &message, wasCanceled](AnalyticsListener& listener) {
    listener.onLoadError(eventTime, mediaLoadData, message, wasCanceled);
  });
}

void AnalyticsCollector::onDownstreamFormatChanged(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId, const MediaLoadData& mediaLoadData) {
  const auto eventTime = createEventTime(windowIndex, mediaPeriodId);
  dispatch([&eventTime, &mediaLoadData](AnalyticsListener& listener) {
    listener.onDownstreamFormatChanged(eventTime, mediaLoadData);
  });
}

void AnalyticsCollector::onUpstreamDiscarded(const int windowIndex,
    const MediaPeriodId& mediaPeriodId, const MediaLoadData& mediaLoadData) {
  const auto eventTime = createEventTime(windowIndex, mediaPeriodId);
  dispatch([&eventTime, &mediaLoadData](AnalyticsListener& listener) {
    listener.onUpstreamDiscarded(eventTime, mediaLoadData);
  });
}

} // namespace coral
