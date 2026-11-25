/*
 * Copyright (C) 2024 The Coral Project
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

#ifndef CORAL_PLAYBACK_PLAYBACK_INFO_H
#define CORAL_PLAYBACK_PLAYBACK_INFO_H

#include <coral/playback/Player.h>
#include <coral/selection/TrackGroup.h>
#include <coral/selection/TrackSelectorResult.h>
#include <coral/source/MediaPeriodId.h>
#include <coral/source/Timeline.h>

#include <media/Definitions.h>

#include <memory>
#include <string>
#include <utility>

namespace coral {

/** Information about an ongoing playback. */
struct PlaybackInfo {

  /** The current {@link Timeline}. */
  std::shared_ptr<Timeline> timeline;

  /** The {@link MediaPeriodId} of the currently playing media period. */
  MediaPeriodId periodId;

  /**
   * The requested next start position for the current period in the {@link #timeline}, in
   * microseconds, or {@link Definitions::kTimeUnset} if the period was requested to start at its
   * default position.
   *
   * Note that if {@link #periodId} refers to an ad, this is the requested start position for the
   * suspended content.
   */
  int64_t requestedContentPositionUs;

  /** The current playback state. One of the {@link Player}.STATE_ constants. */
  Player::State playbackState;

  /**
   * The source error that stopped playback, or an empty string when there is none.
   *
   * Mirrors ExoPlayer's {@code PlaybackInfo.playbackError}: it is recorded when a media period reports
   * a fatal error through {@link MediaPeriod::maybeThrowPrepareError} and cleared when the player is
   * reset.
   */
  std::string playbackErrorMessage;

  /**
   * Indicates whether the player is loading
   */
  bool isLoading;

  /** The currently available track groups. */
  std::shared_ptr<TrackGroupArray> trackGroups;

  /** The result of the current track selection. */
  std::shared_ptr<TrackSelectorResult> trackSelectorResult;

  /** The {@link MediaPeriodId} of the currently loading media period. */
  MediaPeriodId loadingMediaPeriodId;

  /**
   * Whether playback should proceed when {@link #playbackState} == {@link Player::STATE_READY}.
   */
  bool playWhenReady;

  /**
   * The playback parameters
   */
  Parameters parameters;

  /** Whether offload scheduling is enabled for the main player loop. */
  bool offloadSchedulingEnabled;

  /**
   * Position up to which media is buffered, in microseconds.
   */
  int64_t bufferedPositionUs;

  /**
   * Total duration of buffered media from {@link #positionUs} to {@link #bufferedPositionUs}.
   */
  int64_t totalBufferedDurationUs;

  /**
   * Current playback position, in microseconds.
   */
  int64_t positionUs;

  PlaybackInfo()
      : timeline(std::make_shared<EmptyTimeline>()),
        periodId(),
        requestedContentPositionUs(Definitions::kTimeUnset),
        playbackState(Player::State::STATE_IDLE),
        playbackErrorMessage(),
        isLoading(false),
        loadingMediaPeriodId(),
        playWhenReady(false),
        parameters(),
        offloadSchedulingEnabled(false),
        bufferedPositionUs(0),
        totalBufferedDurationUs(0),
        positionUs(0) {}

  explicit PlaybackInfo(const std::shared_ptr<Timeline>& timeline,
      MediaPeriodId periodId,
      int64_t requestedContentPositionUs,
      Player::State state,
      const std::string& playbackError,
      bool isLoading,
      const std::shared_ptr<TrackGroupArray>& trackGroups,
      const std::shared_ptr<TrackSelectorResult>& trackSelectorResult,
      MediaPeriodId loadingMediaPeriodId,
      bool playWhenReady,
      const Parameters& parameters,
      int64_t bufferedPositionUs,
      int64_t totalBufferedDurationUs,
      int64_t positionUs,
      bool offloadSchedulingEnabled)
      : timeline(timeline),
        periodId(std::move(periodId)),
        requestedContentPositionUs(requestedContentPositionUs),
        playbackState(state),
        playbackErrorMessage(playbackError),
        isLoading(isLoading),
        trackGroups(trackGroups),
        trackSelectorResult(trackSelectorResult),
        loadingMediaPeriodId(std::move(loadingMediaPeriodId)),
        playWhenReady(playWhenReady),
        parameters(parameters),
        offloadSchedulingEnabled(offloadSchedulingEnabled),
        bufferedPositionUs(bufferedPositionUs),
        totalBufferedDurationUs(totalBufferedDurationUs),
        positionUs(positionUs) {}

  PlaybackInfo(const PlaybackInfo& other) = default;
  PlaybackInfo& operator=(const PlaybackInfo& other) = default;

  static MediaPeriodId getDummyPeriodForEmptyTimeline() {
    static const MediaPeriodId emptyPeriodId = MediaPeriodId();
    return emptyPeriodId;
  }

  /**
   * Returns whether this object represents a playing state.
   *
   * Returns true if the following conditions are met:
   *
   * <ul>
   *   <li>{@link #playbackState} is {@link Player::STATE_READY}
   *   <li>{@link #playWhenReady} is true.
   * </ul>
   *
   * @return Whether the playbackInfo represents a playing state.
   */
  [[nodiscard]] bool isPlaying() const {
    return playbackState == Player::State::STATE_READY && playWhenReady;
  }

  /**
   * Copies playback info with new playing position.
   *
   * @param periodId New playing media period. See {@link #periodId}.
   * @param positionUs New position. See {@link #positionUs}.
   * @param requestedContentPositionUs New requested content position. See {@link
   *     #requestedContentPositionUs}.
   * @param totalBufferedDurationUs New buffered duration. See {@link #totalBufferedDurationUs}.
   * @param trackGroups The track groups for the new position. See {@link #trackGroups}.
   * @param trackSelectorResult The track selector result for the new position. See {@link
   *     #trackSelectorResult}.
   * @return Copied playback info with new playing position.
   */
  [[nodiscard]] PlaybackInfo copyWithNewPosition(
      MediaPeriodId newPeriodId,
      int64_t newPositionUs,
      int64_t newRequestedContentPositionUs,
      int64_t newTotalBufferedDurationUs,
      const std::shared_ptr<TrackGroupArray>& newTrackGroups,
      const std::shared_ptr<TrackSelectorResult>& newTrackSelectorResult) const {
    return PlaybackInfo(timeline,
        std::move(newPeriodId),
        newRequestedContentPositionUs,
        playbackState,
        playbackErrorMessage,
        isLoading,
        newTrackGroups,
        newTrackSelectorResult,
        loadingMediaPeriodId,
        playWhenReady,
        parameters,
        bufferedPositionUs,
        newTotalBufferedDurationUs,
        newPositionUs,
        offloadSchedulingEnabled);
  }

  /**
   * Copies playback info with the new timeline.
   *
   * @param timeline New timeline. See {@link #timeline}.
   * @return Copied playback info with the new timeline.
   */
  [[nodiscard]] PlaybackInfo
  copyWithTimeline(const std::shared_ptr<Timeline>& newTimeline) const {
    return PlaybackInfo(
        newTimeline,
        periodId,
        requestedContentPositionUs,
        playbackState,
        playbackErrorMessage,
        isLoading,
        trackGroups,
        trackSelectorResult,
        loadingMediaPeriodId,
        playWhenReady,
        parameters,
        bufferedPositionUs,
        totalBufferedDurationUs,
        positionUs,
        offloadSchedulingEnabled);
  }

  /**
   * Copies the playback info with a new playback error.
   *
   * Mirrors ExoPlayer's {@code PlaybackInfo.copyWithPlaybackError}. An empty message clears the
   * error.
   *
   * @param errorMessage The new playback error message.
   * @return Copied playback info with the new playback error.
   */
  [[nodiscard]] PlaybackInfo copyWithPlaybackError(const std::string& errorMessage) const {
    auto info = *this;
    info.playbackErrorMessage = errorMessage;
    return info;
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_PLAYBACK_INFO_H
