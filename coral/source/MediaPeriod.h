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

#ifndef CORAL_PLAYBACK_SOURCE_MEDIA_PERIOD_H
#define CORAL_PLAYBACK_SOURCE_MEDIA_PERIOD_H

#include <coral/selection/TrackGroup.h>
#include <coral/selection/TrackSelection.h>
#include <coral/source/SampleStream.h>
#include <coral/source/SeekParameters.h>
#include <coral/source/SequenceableLoader.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace coral {

/**
 * Thrown by {@link MediaPeriod::maybeThrowPrepareError} when a period cannot be prepared.
 *
 * <p>Mirrors the {@code IOException} that ExoPlayer's media periods throw from
 * {@code maybeThrowPrepareError}: the period that owns the failure reports it, and the player is the
 * one that decides what a fatal source error means for the playback.
 */
class MediaPeriodException final : public std::runtime_error {
public:
  explicit MediaPeriodException(const std::string& message) : std::runtime_error(message) {
  }
};

/**
 * Loads media and allows that media to be read.
 */
class MediaPeriod : public SequenceableLoader {
public:
  /**
   * A callback to be notified of {@link MediaPeriod} events.
   */
  class Callback : public SequenceableLoader::Callback<MediaPeriod> {
  public:
    /**
     * Called when preparation completes.
     *
     * Called on the playback thread.
     *
     * @param mediaPeriod The prepared {@link MediaPeriod}.
     */
    virtual void onPrepared(const std::shared_ptr<MediaPeriod>& mediaPeriod) = 0;
  };

  /**
   * Prepares this media period asynchronously.
   *
   * {@code callback.onPrepared} is called when preparation completes.
   *
   * @param callback Callback to receive updates from this period, including being notified when
   *     preparation completes.
   * @param positionUs The expected starting position, in microseconds.
   */
  virtual void prepare(Callback* callback, int64_t positionUs) = 0;
  /**
   * Releases the period. Called by its owner before the period is dropped, so the period can stop
   * its loading and forget the callback it reports to. A period that a wrapping period (such as the
   * merging period) owns has to be released the same way, or a load that finishes after the wrapper
   * is gone would call into the wrapper.
   */
  virtual void release() {
  }

  /**
   * Throws if this period cannot be prepared.
   *
   * <p>Mirrors ExoPlayer's {@code MediaPeriod.maybeThrowPrepareError}. Called on the playback thread
   * while the period is being prepared, which is where a fatal source error is discovered: the period
   * reports it here instead of calling back into the player, so that the player is the one that decides
   * what the failure means for the playback.
   *
   * @throws MediaPeriodException If the period cannot be prepared.
   */
  virtual void maybeThrowPrepareError() {
  }

  /**
   * Returns why this period cannot be prepared, or an empty string while it can.
   *
   * <p>This is the form of {@link maybeThrowPrepareError} for the player, which reports a failure
   * rather than processes it as an exception: the reason travels as a value, so that it reaches the
   * error of the playback even on a platform whose frames a throw cannot unwind through.
   *
   * @return the failure of the period, or an empty string.
   */
  [[nodiscard]] virtual std::string getPrepareError() const {
    return {};
  }

  /**
   * Returns the {@link TrackGroup}s exposed by the source.
   *
   * This method is only called after the source has been prepared.
   *
   * @return The {@link TrackGroup}s.
   */
  [[nodiscard]] virtual const std::shared_ptr<TrackGroupArray>& getTrackGroups() const = 0;

  /**
   * Performs a track selection.
   *
   * The call receives track {@code selections} for each renderer, {@code
   * mayRetainStreamFlags} indicating whether the existing {@link SampleStream}
   * can be retained for each selection, and the existing {@code stream}s
   * themselves. The call will update {@code streams} to reflect the provided
   * selections, clearing, setting and replacing entries as required. If an
   * existing sample stream is retained but with the requirement that the
   * consuming renderer be reset, then the corresponding flag in {@code
   * streamResetFlags} will be set to true. This flag will also be set if a new
   * sample stream is created.
   *
   * Note that previously passed {@link TrackSelection TrackSelections} are
   * no longer valid, and any references to them must be updated to point to the
   * new selections.
   *
   * This method is only called after the period has been prepared.
   *
   * @param selections The renderer track selections.
   * @param mayRetainStreamFlags Flags indicating whether the existing sample
   * stream can be retained for each track selection. A {@code true} value
   * indicates that the selection is equivalent to the one that was previously
   * passed, and that the caller does not require that the sample stream be
   * recreated. If a retained sample stream holds any references to the track
   *     selection then they must be updated to point to the new selection.
   * @param streams The existing sample streams, which will be updated to
   * reflect the provided selections.
   * @param streamResetFlags Will be updated to indicate new sample streams, and
   * sample streams that have been retained but with the requirement that the
   * consuming renderer be reset.
   * @param positionUs The current playback position in microseconds. If
   * playback of this period has not yet started, the value will be the starting
   * position.
   * @return The actual position at which the tracks were enabled, in
   * microseconds.
   */
  virtual int64_t
  selectTracks(const std::vector<std::shared_ptr<TrackSelection>>& selections,
      const std::vector<bool>& mayRetainStreamFlags,
      std::vector<std::shared_ptr<SampleStream>>& streams,
      std::vector<bool>& streamResetFlags, int64_t positionUs) = 0;

  /**
   * Discards buffered media up to the specified position.
   *
   * This method is only called after the source has been prepared.
   *
   * @param positionUs The position in microseconds.
   * @param toKeyframe If true then for each track discards samples up to the keyframe before or at
   * the specified position, rather than any sample before or at that position.
   */
  virtual void discardBuffer(int64_t positionUs, bool toKeyframe) = 0;

  /**
   * Attempts to read a discontinuity.
   *
   * After this method has returned a value other than {@link Definitions::kTimeUnset}, all
   * {@link SampleStream}s provided by the source are guaranteed to start from a key frame.
   *
   * This method is only called after the source has been prepared and before reading from any
   * {@link SampleStream}s provided by the source.
   *
   * @return If a discontinuity was read then the playback position in microseconds after the
   * discontinuity. Else {@link Definitions::kTimeUnset}.
   */
  virtual int64_t readDiscontinuity() = 0;

  /**
   * Attempts to seek to the specified position.
   *
   * @param positionUs The seek position in microseconds.
   * @return The actual position to which the source was seeked, in microseconds.
   */
  virtual int64_t seekToUs(int64_t positionUs) = 0;

  /**
   * Returns the position to which a seek will be performed, given the specified
   * seek position and {@link SeekParameters}.
   *
   * This method is only called after the period has been prepared.
   *
   * @param positionUs The seek position in microseconds.
   * @param seekParameters Parameters that control how the seek is performed.
   * Implementations may apply seek parameters on a best effort basis.
   * @return The actual position to which a seek will be performed, in
   * microseconds.
   */
  virtual int64_t getAdjustedSeekPositionUs(int64_t positionUs, const SeekParameters& seekParameters) = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_MEDIA_PERIOD_H
