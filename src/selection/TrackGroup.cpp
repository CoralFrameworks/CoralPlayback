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

#include <coral/selection/TrackGroup.h>

namespace coral {

std::vector<int> TrackGroup::getTrackIds() const {
  // One identifier per track: the vector is filled by index rather than appended to a vector that
  // was already sized, which would return twice as many entries as there are tracks.
  auto trackIds = std::vector<int>(formats.size());
  for (size_t index = 0; index < formats.size(); ++index) {
    trackIds[index] = formats[index] != nullptr ? formats[index]->getTrackId()
                                                : Definitions::kIndexUnset;
  }
  return trackIds;
}

const std::shared_ptr<MediaFormat>& TrackGroup::getFormat(int index) const {
  static const std::shared_ptr<MediaFormat> sEmptyFormat = nullptr;
  if (index < 0 || index >= static_cast<int>(formats.size())) {
    return sEmptyFormat;
  }
  return formats[index];
}

int TrackGroup::indexOf(const std::shared_ptr<MediaFormat>& format) const {
  for (size_t index = 0; index < formats.size(); ++index) {
    if (format == formats[index]) {
      return index;
    }
  }
  return Definitions::kIndexUnset;
}

//--------------------------------------------------------------------------------------------------

TrackGroupArray::~TrackGroupArray() {
  trackGroups.erase(trackGroups.begin(), trackGroups.end());
  trackGroups.clear();
}

int TrackGroupArray::indexOf(const std::shared_ptr<TrackGroup>& group) const {
  if (trackGroups.empty()) {
    return Definitions::kIndexUnset;
  }
  for (int i = 0; i < trackGroups.size(); ++i) {
    if (group == trackGroups[i]) {
      return i;
    }
  }
  return Definitions::kIndexUnset;
}

} // namespace coral
