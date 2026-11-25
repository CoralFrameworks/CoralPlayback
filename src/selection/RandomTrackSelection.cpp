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

#include <coral/selection/RandomTrackSelection.h>
#include <coral/selection/FixedTrackSelection.h>

#include <random>

namespace coral {

namespace internal {

/**
 * Returns a random number in [0, max), like ExoPlayer's `Random.nextInt(max)`: the caller uses it as
 * an index into the track list, so an inclusive interval could select the element after the last
 * one.
 */
static int randomNumber(int max) {
  if (max <= 0) {
    return 0;
  }
  static std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<> dis(0, max - 1);
  return dis(gen);
}

} // namespace internal

std::vector<std::shared_ptr<TrackSelection>>
RandomTrackSelection::Factory::createTrackSelections(
    const std::vector<std::unique_ptr<Definition>>& definitions,
    const std::shared_ptr<BandwidthMeter>& meter) {
  auto selections =
      std::vector<std::shared_ptr<TrackSelection>>(definitions.size());
  auto createdAdaptiveTrackSelection = false;
  for (size_t i = 0; i < definitions.size(); ++i) {
    auto const& definition = definitions[i];
    if (definition == nullptr) {
      continue;
    }
    if (definition->tracks.size() > 1 && !createdAdaptiveTrackSelection) {
      createdAdaptiveTrackSelection = true;
      selections[i] = std::make_shared<RandomTrackSelection>(
          definition->group, definition->tracks);
    } else {
      selections[i] = std::make_shared<FixedTrackSelection>(
          definition->group, definition->tracks[0]);
    }
  }
  return selections;
}

RandomTrackSelection::RandomTrackSelection(
    const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& tracks) noexcept
    : BaseTrackSelection(group, tracks) {
  selectedIndex = internal::randomNumber((int)tracks.size());
}

} // namespace coral
