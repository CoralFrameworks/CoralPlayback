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
#ifndef CORAL_PLAYBACK_ANALYTICS_ANALYTICS_LISTENER_H
#define CORAL_PLAYBACK_ANALYTICS_ANALYTICS_LISTENER_H

#include <coral/playback/Player.h>
#include <coral/source/MediaLoadData.h>
#include <coral/source/MediaPeriodId.h>
#include <coral/source/Timeline.h>

#include <cstdint>
#include <optional>
#include <string>

namespace coral {

/**
 * The state of the player and of the media at the moment an event happened.
 *
 * <p>Mirrors ExoPlayer's {@code EventTime}. Every event that reaches an {@link AnalyticsListener}
 * carries one, so a listener can attribute the event to a window, a period and a position of the
 * media without having to keep its own copy of the timeline.
 */
struct EventTime {
  /** The timeline at the time of the event, or null when the player has none yet. */
  std::shared_ptr<Timeline> timeline;

  /** The index of the window the event belongs to, or {@link Definitions::kIndexUnset}. */
  int windowIndex = Definitions::kIndexUnset;

  /** The period the event belongs to, or null when it belongs to no period. */
  std::optional<MediaPeriodId> mediaPeriodId;

  /** The wall clock time of the event, in milliseconds. */
  int64_t eventTimeMs = 0;

  /** The playback position at the time of the event, in milliseconds. */
  int64_t eventPlaybackPositionMs = 0;

  /** The number of windows of the timeline at the time of the event. */
  int64_t currentTimelineWindowCount = 0;

  /** The total buffered duration at the time of the event, in milliseconds. */
  int64_t totalBufferedDurationMs = 0;

  /**
   * Returns the media item of the window the event belongs to, or null when the event belongs to no
   * window or the player has no timeline.
   */
  [[nodiscard]] std::shared_ptr<MediaItem> getMediaItem() const {
    if (timeline == nullptr || windowIndex == Definitions::kIndexUnset ||
        windowIndex >= timeline->getWindowCount()) {
      return nullptr;
    }
    const auto window = timeline->getWindow(windowIndex, std::make_shared<Timeline::Window>(), 0);
    return window != nullptr ? window->mediaItem : nullptr;
  }
};

/**
 * A listener of every event of the player and of its media sources, for analytics and logging.
 *
 * <p>Mirrors ExoPlayer's {@code AnalyticsListener}, reduced to the events this player can report.
 * Every callback has an empty default implementation, so a listener only overrides the events it is
 * interested in. Unlike {@link Player::EventListener} and {@link MediaSourceEventListener}, an
 * analytics listener receives the high level events and the load events of the media sources
 * together, each with the {@link EventTime} that puts it in context.
 */
class AnalyticsListener {
public:
  virtual ~AnalyticsListener() = default;

  // ------------------------------------------------------------------------------------------------
  // The player
  // ------------------------------------------------------------------------------------------------

  /** Called when the playback state changed. */
  virtual void onPlaybackStateChanged(const EventTime& eventTime, Player::State state) {
    static_cast<void>(eventTime);
    static_cast<void>(state);
  }

  /** Called when whether the playback proceeds once ready changed. */
  virtual void onPlayWhenReadyChanged(const EventTime& eventTime, bool playWhenReady) {
    static_cast<void>(eventTime);
    static_cast<void>(playWhenReady);
  }

  /** Called when whether the player is loading the media changed. */
  virtual void onIsLoadingChanged(const EventTime& eventTime, bool isLoading) {
    static_cast<void>(eventTime);
    static_cast<void>(isLoading);
  }

  /** Called when the position of the player changed without a seek. */
  virtual void onPositionDiscontinuity(const EventTime& eventTime, int64_t positionUs) {
    static_cast<void>(eventTime);
    static_cast<void>(positionUs);
  }

  /** Called when the playback parameters changed. */
  virtual void onPlaybackParametersChanged(const EventTime& eventTime,
      const Parameters& parameters) {
    static_cast<void>(eventTime);
    static_cast<void>(parameters);
  }

  /** Called when the repeat mode changed. */
  virtual void onRepeatModeChanged(const EventTime& eventTime, const RepeatMode& repeatMode) {
    static_cast<void>(eventTime);
    static_cast<void>(repeatMode);
  }

  /** Called when the volume changed. */
  virtual void onVolumeChanged(const EventTime& eventTime, float volume) {
    static_cast<void>(eventTime);
    static_cast<void>(volume);
  }

  /** Called when the size of the video changed. */
  virtual void onVideoSizeChanged(const EventTime& eventTime, int width, int height) {
    static_cast<void>(eventTime);
    static_cast<void>(width);
    static_cast<void>(height);
  }

  /** Called when an error stopped the playback. */
  virtual void onPlayerError(const EventTime& eventTime, const std::string& errorMessage) {
    static_cast<void>(eventTime);
    static_cast<void>(errorMessage);
  }

  // ------------------------------------------------------------------------------------------------
  // The media sources
  // ------------------------------------------------------------------------------------------------

  /** Called when a media period is created. */
  virtual void onMediaPeriodCreated(const EventTime& eventTime) {
    static_cast<void>(eventTime);
  }

  /** Called when a media period is released. */
  virtual void onMediaPeriodReleased(const EventTime& eventTime) {
    static_cast<void>(eventTime);
  }

  /** Called when a load started. */
  virtual void onLoadStarted(const EventTime& eventTime, const MediaLoadData& mediaLoadData) {
    static_cast<void>(eventTime);
    static_cast<void>(mediaLoadData);
  }

  /** Called when a load completed. */
  virtual void onLoadCompleted(const EventTime& eventTime, const MediaLoadData& mediaLoadData) {
    static_cast<void>(eventTime);
    static_cast<void>(mediaLoadData);
  }

  /** Called when a load was canceled. */
  virtual void onLoadCanceled(const EventTime& eventTime, const MediaLoadData& mediaLoadData) {
    static_cast<void>(eventTime);
    static_cast<void>(mediaLoadData);
  }

  /** Called when a load failed. */
  virtual void onLoadError(const EventTime& eventTime, const MediaLoadData& mediaLoadData,
      const std::string& errorMessage, bool wasCanceled) {
    static_cast<void>(eventTime);
    static_cast<void>(mediaLoadData);
    static_cast<void>(errorMessage);
    static_cast<void>(wasCanceled);
  }

  /** Called when the format of the media being read changed. */
  virtual void onDownstreamFormatChanged(const EventTime& eventTime,
      const MediaLoadData& mediaLoadData) {
    static_cast<void>(eventTime);
    static_cast<void>(mediaLoadData);
  }

  /**
   * Called when media was discarded from the back of a buffer.
   *
   * <p>No source of this repository discards upstream samples yet, so this is only called by a source
   * that does, see {@code MEDIA_SOURCE_ALIGNMENT.md}.
   */
  virtual void onUpstreamDiscarded(const EventTime& eventTime,
      const MediaLoadData& mediaLoadData) {
    static_cast<void>(eventTime);
    static_cast<void>(mediaLoadData);
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_ANALYTICS_ANALYTICS_LISTENER_H
