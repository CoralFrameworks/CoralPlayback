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

#ifndef CORAL_PLAYBACK_SELECTION_BASE_TRACK_SELECTION_H
#define CORAL_PLAYBACK_SELECTION_BASE_TRACK_SELECTION_H

#include <coral/selection/TrackSelection.h>

#include <memory>
#include <vector>

namespace coral {

/**
 * An abstract base class suitable for most {@link TrackSelection}
 * implementations.
 */
class BaseTrackSelection : public TrackSelection {
private:
  /** The selected {@link TrackGroup}. */
  std::shared_ptr<TrackGroup> trackGroup;

  /** The indices of the selected tracks in {@link #group}, in order of decreasing bandwidth. */
  std::vector<int> tracks;

  /** The {@link Format}s of the selected tracks, in order of decreasing bandwidth. */
  std::vector<std::shared_ptr<MediaFormat>> formats;

public:
  explicit BaseTrackSelection(const std::shared_ptr<TrackGroup>& group, int track) noexcept;

  explicit BaseTrackSelection(const std::shared_ptr<TrackGroup>& group,
      const std::vector<int>& tracks) noexcept;

  BaseTrackSelection(const BaseTrackSelection& other) = delete;
  BaseTrackSelection(BaseTrackSelection&& other) = delete;
  BaseTrackSelection& operator=(const BaseTrackSelection& other) = delete;
  BaseTrackSelection& operator=(BaseTrackSelection&& other) = delete;

  void enable() override {
    // Do nothing.
  }

  void disable() override {
    // Do nothing.
  }

  [[nodiscard]] const std::shared_ptr<TrackGroup>& getTrackGroup() const override {
    return trackGroup;
  }

  int length() override { return (int)tracks.size(); }

  [[nodiscard]] const std::shared_ptr<MediaFormat>& getFormat(int index) const override {
    return formats[index];
  }

  int getIndexInTrackGroup(int index) override { return tracks[index]; }

  int indexOf(const std::shared_ptr<MediaFormat>& format) override {
    for (int i = 0; i < length(); i++) {
      if (formats[i] == format) {
        return i;
      }
    }
    return Definitions::kIndexUnset;
  }

  int indexOf(int indexInTrackGroup) override {
    for (int i = 0; i < length(); i++) {
      if (tracks[i] == indexInTrackGroup) {
        return i;
      }
    }
    return Definitions::kIndexUnset;
  }

  [[nodiscard]] const std::shared_ptr<MediaFormat>& getSelectedFormat() const override {
    return formats[getSelectedIndex()];
  }

  [[nodiscard]] int getSelectedIndexInTrackGroup() const override {
    return tracks[getSelectedIndex()];
  }

  void onPlaybackSpeed(float speed) override {
    // Do nothing.
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_BASE_TRACK_SELECTION_H
