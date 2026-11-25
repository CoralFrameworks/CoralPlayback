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

#ifndef CORAL_PLAYBACK_SELECTION_RANDOM_TRACK_SELECTION_H
#define CORAL_PLAYBACK_SELECTION_RANDOM_TRACK_SELECTION_H

#include <coral/selection/BaseTrackSelection.h>

namespace coral {

/**
 * A {@link TrackSelection} whose selected track is updated randomly.
 */
class RandomTrackSelection final : public BaseTrackSelection {
public:
  class Factory final : public TrackSelection::Factory {
  public:
    std::vector<std::shared_ptr<TrackSelection>> createTrackSelections(
        const std::vector<std::unique_ptr<Definition>>& definitions,
        const std::shared_ptr<BandwidthMeter>& bandwidthMeter) override;
  };

  explicit RandomTrackSelection(const std::shared_ptr<TrackGroup>& group,
      const std::vector<int>& tracks) noexcept;

  int getSelectedIndex() const override { return selectedIndex; }

private:
  int selectedIndex;
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_RANDOM_TRACK_SELECTION_H
