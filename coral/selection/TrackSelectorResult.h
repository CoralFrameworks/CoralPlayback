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

#ifndef CORAL_PLAYBACK_SELECTION_TRACK_SELECTOR_RESULT_H
#define CORAL_PLAYBACK_SELECTION_TRACK_SELECTOR_RESULT_H

#include <coral/selection/Configuration.h>
#include <coral/selection/TrackSelection.h>

namespace coral {

class TrackInfo;

/**
 * The result of a {@link TrackSelector} operation.
 */
class TrackSelectorResult final {
private:
  /**
   * A {@link Configuration} for each renderer. A null entry indicates
   * the corresponding renderer should be disabled.
   */
  std::vector<std::shared_ptr<Configuration>> configurations;

  /**
   * A {@link TrackSelectionArray} containing the track selection for each
   * renderer.
   */
  std::shared_ptr<TrackSelectionArray> selections;

  /**
   * An opaque object that will be returned to {@link
   * TrackSelector#onSelectionActivated(Object)} should the selections be
   * activated.
   */
  std::shared_ptr<TrackInfo> trackInfo;

public:
  explicit TrackSelectorResult(
      const std::vector<std::shared_ptr<Configuration>>& configurations,
      const std::vector<std::shared_ptr<TrackSelection>>& selections,
      const std::shared_ptr<TrackInfo>& info) noexcept
      : configurations(configurations),
        selections(std::make_shared<TrackSelectionArray>(selections)),
        trackInfo(info) {}

  ~TrackSelectorResult() = default;

  TrackSelectorResult(const TrackSelectorResult& other) = default;
  TrackSelectorResult& operator=(const TrackSelectorResult& other) = default;

  /**
   * Returns the number of selections in the result. Greater than or equal to
   * zero.
   */
  [[nodiscard]] size_t length() const { return configurations.size(); }

  /**
   * A {@link TrackSelectionArray} containing the track selection for each
   * renderer.
   */
  [[nodiscard]] const std::shared_ptr<TrackSelectionArray>& getSelections() const {
    return selections;
  }

  [[nodiscard]] const std::shared_ptr<TrackSelection>& getSelection(size_t index) const {
    if (index >= selections->length()) {
      static const std::shared_ptr<TrackSelection> sEmptySelection = nullptr;
      return sEmptySelection;
    }
    return selections->get(index);
  }

  [[nodiscard]] const std::shared_ptr<TrackInfo>& getInfo() const { return trackInfo; }

  [[nodiscard]] const std::shared_ptr<Configuration>&
  getConfiguration(size_t index) const {
    return configurations[index];
  }

  /**
   * Returns whether the configuration at the specified index is enabled.
   */
  bool isEnabled(size_t index) {
    return configurations[index] != nullptr;
  }

  bool isEquivalent(const std::shared_ptr<TrackSelectorResult>& other) {
    if (other == nullptr ||
        other->selections->length() != selections->length()) {
      return false;
    }
    for (size_t i = 0; i < selections->length(); ++i) {
      if (!isEquivalent(other, i)) {
        return false;
      }
    }
    return true;
  }

  /**
   * Returns whether this result is equivalent to {@code other} for the renderer
   * at the given index. The results are equivalent if they have equal track
   * selections and configurations for the renderer.
   *
   * @param other The other {@link TrackSelectorResult}. May be null, in which
   * case {@code false} will be returned.
   * @param index The renderer index to check for equivalence.
   * @return Whether this result is equivalent to {@code other} for the renderer
   * at the specified index.
   */
  bool isEquivalent(const std::shared_ptr<TrackSelectorResult>& other, size_t index) {
    if (!other) {
      return false;
    }
    return (configurations[index] == other->configurations[index])
           && (selections->get(index) == other->selections->get(index));
  }

  friend bool operator==(const TrackSelectorResult& lhs, const TrackSelectorResult& rhs) {
    if (lhs.selections->length() != rhs.selections->length()) {
      return false;
    }
    auto length = lhs.selections->length();
    for (size_t i = 0; i < length; ++i) {
      if (lhs.configurations[i] != rhs.configurations[i]
          || lhs.selections->get(i) != rhs.selections->get(i)) {
        return false;
      }
    }
    return true;
  }

  friend bool operator!=(const TrackSelectorResult& lhs, const TrackSelectorResult& rhs) {
    return !(lhs == rhs);
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_TRACK_SELECTOR_RESULT_H
