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

#ifndef CORAL_PLAYBACK_LOAD_CONTROL_H
#define CORAL_PLAYBACK_LOAD_CONTROL_H

#include <coral/selection/TrackSelection.h>
#include <coral/source/Timeline.h>
#include <coral/upstream/Allocator.h>

#include <media/Definitions.h>

#include <memory>

namespace coral {

/**
 * Controls buffering of media.
 */
class LoadControl {
public:
  virtual ~LoadControl() = default;

  static constexpr int64_t kTimeUnset = Definitions::kTimeUnset;

  struct Parameters {
    /** The current {@link Timeline} of the player. */
    std::weak_ptr<Timeline> timeline;

    /**
     * The {@link MediaPeriodId} of the affected {@link MediaPeriod} in the current {@link timeline}.
     */
    MediaPeriodId mediaPeriodId;

    /**
     * The current playback position in microseconds, relative to the start of the affected {@link
     * MediaPeriod} identified by {@link mediaPeriodId}. If playback of this period has not yet
     * started, the value will be negative and equal in magnitude to the duration of any media in
     * previous periods still to be played.
     */
    int64_t playbackPositionUs;

    /**
     * The total duration of media that's currently buffered, or {@link kTimeUnset} when it is not
     * known, e.g. because the player has not started playing the period yet. It is not a duration that
     * is longer than any buffer: an implementation has to read it as an unknown duration.
     */
    int64_t bufferedDurationUs;

    /** The current factor by which playback is sped up. */
    float playbackSpeed;

    /** Whether playback should proceed when {@link Player::State::STATE_READY}. */
    bool playWhenReady;

    /**
     * Whether the player is rebuffering. A rebuffer is defined to be caused by buffer depletion
     * rather than a user action. Hence this parameter is false during initial buffering and when
     * buffering as a result of a seek operation.
     */
    bool rebuffering;
    /**
     * The desired playback position offset to the live edge in microseconds, or {@link kTimeUnset}
     * if the media is not a live stream or no offset is configured.
     */
    int64_t targetLiveOffsetUs;

    /**
     * Sets the time at which the last rebuffering occurred, in milliseconds since boot including
     * time spent in sleep.
     *
     * <p>The time base used is the same as that measured by {@link SystemClock::ElapsedRealtime}.
     *
     * <p><b>Note:</b> If rebuffer events are not known when the load is started or continued, or if
     * no rebuffering has occurred, or if there have been any user interactions such as seeking or
     * stopping the player, the value will be set to {@link kTimeUnset}.
     */
    int64_t lastRebufferRealtimeMs;
  };


  /**
   * Called by the player when prepared with a new source.
   */
  virtual void onPrepared() = 0;

  /**
   * Called by the player when a track selection occurs.
   *
   * @param parameters The parameters containing the current {@link Timeline} in LivePlayer, and the
   *                   {@link MediaPeriod} for which the selection was made.
   * @param trackGroups The {@link TrackGroup}s from which the selection was
   * made.
   * @param selections The track selections that were made.
   */
  virtual void onTracksSelected(Parameters parameters,
    const std::shared_ptr<TrackGroupArray>& trackGroups,
    const std::shared_ptr<TrackSelectionArray>& selections) = 0;

  /**
   * Called by the player when stopped.
   */
  virtual void onStopped() = 0;

  /**
   * Called by the player when released.
   */
  virtual void onReleased() = 0;

  /**
   * Returns the {@link Allocator} that should be used to obtain media buffer
   * allocations.
   */
  virtual const std::shared_ptr<Allocator>& getAllocator() const = 0;

  /**
   * Returns the duration of media to retain in the buffer prior to the current
   * playback position, for fast backward seeking. <p> Note: If {@link
   * #retainBackBufferFromKeyframe()} is false then seeking in the back-buffer
   * will only be fast if the back-buffer contains a keyframe prior to the seek
   * position. <p> Note: Implementations should return a single value. Dynamic
   * changes to the back-buffer are not currently supported.
   *
   * @return The duration of media to retain in the buffer prior to the current
   * playback position, in microseconds.
   */
  virtual int64_t getBackBufferDurationUs() = 0;

  /**
   * Returns whether media should be retained from the keyframe before the
   * current playback position minus {@link getBackBufferDurationUs()}, rather
   * than any sample before or at that position. <p> Warning: Returning true
   * will cause the back-buffer size to depend on the spacing of keyframes in
   * the media being played. Returning true is not recommended unless you
   * control the media and are comfortable with the back-buffer size exceeding
   * {@link getBackBufferDurationUs()} by as much as the maximum duration
   * between adjacent keyframes in the media. <p> Note: Implementations should
   * return a single value. Dynamic changes to the back-buffer are not currently
   * supported.
   *
   * @return Whether media should be retained from the keyframe before the
   * current playback position minus {@link getBackBufferDurationUs()}, rather
   * than any sample before or at that position.
   */
  virtual bool retainBackBufferFromKeyframe() = 0;

  /**
  * Called by the player to determine whether it should continue to load the source. If this method
   * returns true, the {@link MediaPeriod} identified in the most recent {@link onTracksSelected}
   * call will continue being loaded
   *
   * @param parameters Information about the playback context and the {@link MediaPeriod} that will
   *                   continue to load if this method returns {@code true}.
   *
   * @return Whether the loading should continue.
   */
  virtual bool shouldContinueLoading(Parameters parameters) = 0;

  /**
  * Called repeatedly by the player when it's loading the source, has yet to start playback, and
   * has the minimum amount of data necessary for playback to be started. The value returned
   * determines whether playback is actually started. The load control may opt to return {@code
   * false} until some condition has been met (e.g. a certain amount of media is buffered).
   *
   * @param parameters Information about the playback context and the {@link MediaPeriod} that will
   *                   start playing if this method returns {@code true}.
   * @return Whether playback should be allowed to start or resume.
   */
  virtual bool shouldStartPlayback(Parameters parameters) = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_LOAD_CONTROL_H
