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

#include <coral/selection/BaseTrackSelection.h>

#include <cassert>

namespace coral {

BaseTrackSelection::BaseTrackSelection(const std::shared_ptr<TrackGroup>& group, int track) noexcept
    : trackGroup(group) {
  formats.push_back(trackGroup->getFormat(track));
  tracks.push_back(trackGroup->indexOf(formats[0]));
}

BaseTrackSelection::BaseTrackSelection(const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& tracks) noexcept
    : trackGroup(group) {
  assert(!tracks.empty());
  // Set the formats, sorted in order of decreasing bandwidth.
  for (auto const& track: tracks) {
    formats.push_back(trackGroup->getFormat(track));
  }
  for (size_t i = 0; i < tracks.size(); ++i) {
    this->tracks.push_back(trackGroup->indexOf(formats[i]));
  }
}

} // namespace coral
