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
#ifndef CORAL_PLAYBACK_ANALYTICS_ANALYTICS_COLLECTOR_H
#define CORAL_PLAYBACK_ANALYTICS_ANALYTICS_COLLECTOR_H

#include <coral/analytics/AnalyticsListener.h>
#include <coral/source/MediaSourceEventListener.h>

#include <core/task/Mutex.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace coral {

/**
 * Fans the events of the player and of its media sources out to the {@link AnalyticsListener}s.
 *
 * <p>Mirrors the part of ExoPlayer's {@code AnalyticsCollector} this player needs: it keeps the state
 * an {@link EventTime} is built from, and it is the single place the events are dispatched from, so
 * that every listener sees every event with the same context.
 *
 * <p>It is a {@link MediaSourceEventListener}, so the player registers it on its media source list
 * like any other listener; the high level events are reported to it by the player directly.
 */
class AnalyticsCollector final : public MediaSourceEventListener,
                                 public Player::EventListener {
public:
  AnalyticsCollector();

  ~AnalyticsCollector() override = default;

  AnalyticsCollector(const AnalyticsCollector& other) = delete;
  AnalyticsCollector& operator=(const AnalyticsCollector& other) = delete;

  /**
   * Adds a listener.
   *
   * @param listener The listener to add.
   */
  void addListener(const std::shared_ptr<AnalyticsListener>& listener);

  /**
   * Removes a listener.
   *
   * @param listener The listener to remove.
   */
  void removeListener(const std::shared_ptr<AnalyticsListener>& listener);

  /** Returns the number of registered listeners. */
  [[nodiscard]] int getListenerCount() const;

  /**
   * Records the state that the {@link EventTime} of the events is built from.
   *
   * <p>Called by the player on its playback thread whenever the timeline or the position changes, so
   * that an event that is reported later carries the context it happened in. Only the values that
   * changed are given: the others keep the value of the previous call.
   *
   * @param timeline The current timeline, or null to keep the one the collector has.
   * @param windowIndex The current window index.
   * @param mediaPeriodId The current period, or null when there is none.
   * @param positionMs The current playback position in milliseconds.
   * @param bufferedDurationMs The total buffered duration in milliseconds.
   */
  void setState(const std::shared_ptr<Timeline>& timeline, int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId, int64_t positionMs,
      int64_t bufferedDurationMs);

  // The high level events. The player reports them to its listeners, and this collector is one of
  // them, so every one of these is at once a Player::EventListener callback and the fan out of the
  // matching AnalyticsListener callback.
  void onPlaybackStateChanged(const Player::State& state) override;
  void onPlayWhenReadyChanged(bool playWhenReady) override;
  void onIsLoadingChanged(bool isLoading) override;
  void onPositionDiscontinuity(int64_t positionUs) override;
  void onPlaybackParametersChanged(const Parameters& parameters) override;
  void onRepeatModeChanged(const RepeatMode& repeatMode) override;
  void onVolumeChanged(float volume) override;
  void onVideoSizeChanged(int width, int height) override;
  void onPlayerError(const std::string& errorMessage) override;

  // MediaSourceEventListener.
  void onMediaPeriodCreated(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId) override;
  void onMediaPeriodReleased(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId) override;
  void onLoadStarted(int windowIndex, const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo, const MediaLoadData& mediaLoadData) override;
  void onLoadCompleted(int windowIndex, const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo, const MediaLoadData& mediaLoadData) override;
  void onLoadCanceled(int windowIndex, const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo, const MediaLoadData& mediaLoadData) override;
  void onLoadError(int windowIndex, const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo, const MediaLoadData& mediaLoadData,
      const Error* error, bool wasCanceled) override;
  void onDownstreamFormatChanged(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const MediaLoadData& mediaLoadData) override;
  void onUpstreamDiscarded(int windowIndex, const MediaPeriodId& mediaPeriodId,
      const MediaLoadData& mediaLoadData) override;

private:
  /** Builds the event time of an event, overriding the window and the period when they are known. */
  [[nodiscard]] EventTime createEventTime(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId) const;

  /** Runs `dispatch` for every registered listener, on a snapshot of the list. */
  void dispatch(const std::function<void(AnalyticsListener&)>& action);

  mutable Mutex listenersLock;
  std::vector<std::shared_ptr<AnalyticsListener>> listeners; //!< Guarded by `listenersLock`.

  mutable Mutex stateLock;
  std::shared_ptr<Timeline> timeline; //!< Guarded by `stateLock`.
  int windowIndex = Definitions::kIndexUnset; //!< Guarded by `stateLock`.
  std::optional<MediaPeriodId> mediaPeriodId; //!< Guarded by `stateLock`.
  int64_t positionMs = 0; //!< Guarded by `stateLock`.
  int64_t bufferedDurationMs = 0; //!< Guarded by `stateLock`.
};

} // namespace coral

#endif // CORAL_PLAYBACK_ANALYTICS_ANALYTICS_COLLECTOR_H
