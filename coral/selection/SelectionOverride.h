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
#ifndef CORAL_PLAYBACK_SELECTION_SELECTION_OVERRIDE_H
#define CORAL_PLAYBACK_SELECTION_SELECTION_OVERRIDE_H

#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

namespace coral {

/**
 * The tracks a renderer has to select, instead of the tracks the selection would pick.
 *
 * <p>Mirrors ExoPlayer's {@code DefaultTrackSelector.SelectionOverride}. An application that knows
 * better than the selector, e.g. because the user chose an audio track, sets one of these on the
 * parameters of the selector.
 *
 * <p>An override that names no track deselects the renderer while leaving it enabled, which is
 * different from the renderer being disabled: the renderer is still there, it just has nothing to
 * play.
 */
class SelectionOverride final {
public:
  /** The index of the track group in the track group array of the renderer. */
  int groupIndex;

  /** The indices of the selected tracks in the group. */
  std::vector<int> tracks;

  /**
   * @param groupIndex The index of the track group in the track group array of the renderer.
   * @param tracks The indices of the selected tracks in the group. Empty deselects the renderer.
   */
  SelectionOverride(const int groupIndex, std::vector<int> tracks) noexcept
      : groupIndex(groupIndex),
        tracks(std::move(tracks)) {
  }

  /** Overrides the selection with a single track of the group. */
  SelectionOverride(const int groupIndex, const int trackIndex) noexcept
      : groupIndex(groupIndex),
        tracks{ trackIndex } {
  }

  /** Overrides the selection with several tracks of the group. */
  SelectionOverride(const int groupIndex, std::initializer_list<int> trackIndices) noexcept
      : groupIndex(groupIndex),
        tracks(trackIndices) {
  }

  /**
   * Returns whether this override describes the same tracks as another one.
   */
  [[nodiscard]] bool operator==(const SelectionOverride& other) const {
    return groupIndex == other.groupIndex && tracks == other.tracks;
  }

  [[nodiscard]] bool operator!=(const SelectionOverride& other) const {
    return !(*this == other);
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_SELECTION_OVERRIDE_H
