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

#ifndef CORAL_PLAYBACK_SELECTION_TRACK_SELECTION_H
#define CORAL_PLAYBACK_SELECTION_TRACK_SELECTION_H

#include <coral/selection/TrackGroup.h>
#include <coral/upstream/BandwidthMeter.h>

namespace coral {

/**
 * A track selection consisting of a static subset of selected tracks belonging
 * to a {@link TrackGroup}, and a possibly varying individual selected track
 * from the subset.
 */
class TrackSelection {
public:
  virtual ~TrackSelection() = default;

  /**
   * Contains of a subset of selected tracks belonging to a {@link TrackGroup}.
   */
  class Definition final {
  public:
    /** The {@link TrackGroup} which tracks belong to. */
    std::shared_ptr<TrackGroup> group;

    /** The indices of the selected tracks in {@link group}. */
    std::vector<int> tracks;

    explicit Definition(const std::shared_ptr<TrackGroup>& group, int track) noexcept
        : group(group) {
      tracks.push_back(track);
    }

    explicit Definition(const std::shared_ptr<TrackGroup>& group,
        const std::vector<int>& tracks) noexcept
        : group(group),
          tracks(tracks) {}

    ~Definition() = default;

    Definition(const Definition& other) = default;
    Definition& operator=(const Definition& other) = default;
  };

  /**
   * Factory for {@link TrackSelection} instances.
   */
  class Factory {
  public:
    virtual ~Factory() = default;

    /**
     * Creates track selections for the provided {@link Definition Definitions}.
     *
     * <p>Implementations that create at most one adaptive track selection may
     * use {@link TrackSelectionUtil#createTrackSelectionsForDefinitions}.
     *
     * @param definitions A {@link Definition} array. May include null values.
     * @param bandwidthMeter A {@link BandwidthMeter} which can be used to
     * select tracks.
     * @return The created selections. Must have the same length as {@code
     * definitions} and may include null values.
     */
    virtual std::vector<std::shared_ptr<TrackSelection>> createTrackSelections(
        const std::vector<std::unique_ptr<Definition>>& definitions,
        const std::shared_ptr<BandwidthMeter>& bandwidthMeter) = 0;
  };

  /**
   * Enables the track selection.
   *
   * <p>This method may not be called when the track selection is already
   * enabled.
   */
  virtual void enable() = 0;

  /**
   * Disables this track selection.
   *
   * <p>This method may only be called when the track selection is already
   * enabled.
   */
  virtual void disable() = 0;

  /**
   * Returns the {@link TrackGroup} to which the selected tracks belong.
   */
  virtual const std::shared_ptr<TrackGroup>& getTrackGroup() const = 0;

  /**
   * Returns the number of tracks in the selection.
   */
  virtual int length() = 0;

  /**
   * Returns the format of the track at a given index in the selection.
   *
   * @param index The index in the selection.
   * @return The format of the selected track.
   */
  virtual const std::shared_ptr<MediaFormat>& getFormat(int index) const = 0;

  /**
   * Returns the index in the track group of the track at a given index in the
   * selection.
   *
   * @param index The index in the selection.
   * @return The index of the selected track.
   */
  virtual int getIndexInTrackGroup(int index) = 0;

  /**
   * Returns the index in the selection of the track with the specified format.
   * The format is located by identity so, for example, {@code
   * selection.indexOf(selection.getFormat(index)) == index} even if multiple
   * selected tracks have formats that contain the same values.
   *
   * @param format The format.
   * @return The index in the selection, or {@link Definitions::kIndexUnset} if the track
   * with the specified format is not part of the selection.
   */
  virtual int indexOf(const std::shared_ptr<MediaFormat>& format) = 0;

  /**
   * Returns the index in the selection of the track with the specified index in
   * the track group.
   *
   * @param indexInTrackGroup The index in the track group.
   * @return The index in the selection, or {@link Definitions::kIndexUnset} if the track
   * with the specified index is not part of the selection.
   */
  virtual int indexOf(int indexInTrackGroup) = 0;

  /**
   * Returns the {@link Format} of the individual selected track.
   */
  virtual const std::shared_ptr<MediaFormat>& getSelectedFormat() const = 0;

  /**
   * Returns the index in the track group of the individual selected track.
   */
  virtual int getSelectedIndexInTrackGroup() const = 0;

  /**
   * Returns the index of the selected track.
   */
  virtual int getSelectedIndex() const = 0;

  /**
   * Called to notify the selection of the current playback speed. The playback
   * speed may affect adaptive track selection.
   *
   * @param speed The playback speed.
   */
  virtual void onPlaybackSpeed(float speed) = 0;

  /**
   * Re-evaluates the selected track for the state of the playback.
   *
   * <p>Mirrors ExoPlayer's {@code ExoTrackSelection.updateSelectedTrack}. A selection that does not
   * adapt, and an adaptive selection whose source does not read chunks, ignore it.
   *
   * @param playbackPositionUs The current playback position.
   * @param bufferedDurationUs The duration of the media that is buffered.
   * @param availableDurationUs The duration of the media that is available, or
   *                            {@link Definitions::kTimeEndOfSource} when it is unknown.
   * @param queueSize The number of chunks being read.
   */
  virtual void updateSelectedTrack(int64_t playbackPositionUs, int64_t bufferedDurationUs,
      int64_t availableDurationUs, int queueSize) {
    static_cast<void>(playbackPositionUs);
    static_cast<void>(bufferedDurationUs);
    static_cast<void>(availableDurationUs);
    static_cast<void>(queueSize);
  }

  /**
   * Called to notify the selection of a position discontinuity.
   *
   * <p>This happens when the playback position jumps, e.g., as a result of a seek being performed.
   */
  void onDiscontinuity() {}

  /**
   * Called to notify when the playback is paused or resumed.
   *
   * @param playWhenReady Whether playback will proceed when ready.
   */
  virtual void onPlayWhenReadyChanged(bool playWhenReady) {}

  /**
   * Returns whether the media that the source read ahead should be discarded from the back of its
   * buffer.
   *
   * <p>Mirrors ExoPlayer's {@code ExoTrackSelection.evaluateQueueSize}. A selection that does not
   * evaluate its queue returns false, and the caller keeps the whole buffer.
   *
   * @param playbackPositionUs The current playback position.
   * @param bufferedDurationUs The duration of the media that is buffered from the playback position.
   */
  virtual bool evaluateQueueSize(int64_t playbackPositionUs, int64_t bufferedDurationUs) {
    static_cast<void>(playbackPositionUs);
    static_cast<void>(bufferedDurationUs);
    return false;
  }

  /**
   * Returns the duration of media that has to be kept behind the playback position when the back
   * buffer is discarded, or {@link Definitions::kTimeUnset} when the selection does not ask for a
   * discard.
   *
   * <p>Mirrors ExoPlayer's {@code AdaptiveTrackSelection.getMinDurationToRetainAfterDiscardUs}.
   */
  [[nodiscard]] virtual int64_t getMinDurationToRetainAfterDiscardUs() const {
    return Definitions::kTimeUnset;
  }

  /**
   * Returns whether a track of the selection is excluded from being selected for the time being.
   *
   * <p>Mirrors ExoPlayer's {@code ExoTrackSelection.isTrackExcluded}: a track whose media could not
   * be read is not selected again until the exclusion expires.
   *
   * @param index The index of the track in the selection.
   * @param nowMs The current time.
   */
  [[nodiscard]] virtual bool isTrackExcluded(int index, int64_t nowMs) const {
    static_cast<void>(index);
    static_cast<void>(nowMs);
    return false;
  }

};

/**
 * An array of {@link TrackSelection}s.
 */
class TrackSelectionArray final {
private:
  const std::vector<std::shared_ptr<TrackSelection>> trackSelections;

public:
  explicit TrackSelectionArray(
      const std::vector<std::shared_ptr<TrackSelection>>& trackSelections) noexcept
      : trackSelections(trackSelections) {}

  ~TrackSelectionArray() = default;

  TrackSelectionArray(const TrackSelectionArray& other) = delete;
  TrackSelectionArray& operator=(const TrackSelectionArray& other) = delete;

  [[nodiscard]] const std::shared_ptr<TrackSelection>& get(size_t index) const {
    return trackSelections[index];
  }

  [[nodiscard]] const std::vector<std::shared_ptr<TrackSelection>>& getAll() const {
    return trackSelections;
  }

  [[nodiscard]] size_t length() const { return trackSelections.size(); }
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_TRACK_SELECTION_H
