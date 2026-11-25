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
#include "PlaylistTimeline.h"

namespace coral {

PlaylistTimeline::PlaylistTimeline(const std::vector<std::shared_ptr<MediaSourceHolder>>& holders,
  const std::shared_ptr<ShuffleOrder>& shuffleOrder) noexcept
  : AbstractConcatenatedTimeline(false, shuffleOrder) {
  const auto childCount = holders.size();
  firstPeriodInChildIndices.resize(childCount);
  firstWindowInChildIndices.resize(childCount);
  uids.resize(childCount);
  int index = 0;
  int windowSize = 0;
  int periodSize = 0;
  for (auto const& holder: holders) {
    auto const& timeline = holder->getTimeline();
    timelines.push_back(timeline);
    firstWindowInChildIndices[index] = windowSize;
    firstPeriodInChildIndices[index] = periodSize;
    windowSize += timeline->getWindowCount();
    periodSize += timeline->getPeriodCount();
    auto const& uid = holder->getUid();
    uids[index] = uid;
    childIndexByUid[uid] = index++;
  }
  windowCount = windowSize;
  periodCount = periodSize;
}

int PlaylistTimeline::getChildIndexByPeriodIndex(const int periodIndex) {
  // The child that owns a period is the last one whose first period index is not greater than it,
  // which is ExoPlayer's `binarySearchFloor(firstPeriodInChildIndices, periodIndex + 1, false,
  // false)`.
  return indexOfChildOwning(firstPeriodInChildIndices, periodIndex);
}

int PlaylistTimeline::getChildIndexByWindowIndex(int windowIndex) {
  return indexOfChildOwning(firstWindowInChildIndices, windowIndex);
}

int PlaylistTimeline::indexOfChildOwning(const std::vector<int>& firstIndicesInChild,
    const int indexInPlaylist) {
  // `upper_bound` points one past the last child whose first index is not greater than the element,
  // so the child that owns the element is the one before it. The first child always starts at 0, so
  // the result is never negative.
  const auto position = std::upper_bound(firstIndicesInChild.begin(), firstIndicesInChild.end(),
      indexInPlaylist);
  if (position == firstIndicesInChild.begin()) {
    return 0;
  }
  return static_cast<int>(std::distance(firstIndicesInChild.begin(), position)) - 1;
}

int PlaylistTimeline::getChildIndexByChildUid(const std::optional<PeriodUid>& childUid) {
  if (childUid != std::nullopt && childIndexByUid.count(childUid.value())) {
    return childIndexByUid[childUid.value()];
  }
  return 0;
}

} // namespace coral
