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
#ifndef CORAL_PLAYBACK_DETAILS_PLAYLIST_TIMELINE_H
#define CORAL_PLAYBACK_DETAILS_PLAYLIST_TIMELINE_H

#include "MediaSourceList.h"

#include <coral/source/AbstractConcatenatedTimeline.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace coral {

/** Timeline exposing concatenated timelines of playlist media sources. */
class PlaylistTimeline final : public AbstractConcatenatedTimeline {
private:
  int windowCount;
  int periodCount;
  std::vector<int> firstPeriodInChildIndices;
  std::vector<int> firstWindowInChildIndices;
  std::vector<std::shared_ptr<Timeline>> timelines;
  std::vector<PeriodUid> uids;
  std::unordered_map<PeriodUid, int> childIndexByUid;

public:
  explicit PlaylistTimeline(const std::vector<std::shared_ptr<MediaSourceHolder>>& holders,
    const std::shared_ptr<ShuffleOrder>& shuffleOrder) noexcept;

  [[nodiscard]] const std::vector<std::shared_ptr<Timeline>>& getChildTimelines() const {
    return timelines;
  }

protected:
  int getChildIndexByPeriodIndex(int periodIndex) override;

  int getChildIndexByWindowIndex(int windowIndex) override;

  int getChildIndexByChildUid(const std::optional<PeriodUid>& childUid) override;

  /**
   * Returns the index of the child that owns an index of the playlist.
   *
   * @param firstIndicesInChild The index of the first period, or of the first window, of every
   *                            child, ordered by child.
   * @param indexInPlaylist The index of the element in the playlist.
   */
  static int indexOfChildOwning(const std::vector<int>& firstIndicesInChild, int indexInPlaylist);

  [[nodiscard]] const std::shared_ptr<Timeline>& getTimelineByChildIndex(const size_t childIndex) const override {
    if (childIndex < timelines.size()) {
      return timelines[childIndex];
    }
    static std::shared_ptr<Timeline> sEmptyTimeline = nullptr;
    return sEmptyTimeline;
  }

  int getFirstPeriodIndexByChildIndex(const size_t childIndex) override {
    if (childIndex < firstPeriodInChildIndices.size()) {
      return firstPeriodInChildIndices[childIndex];
    }
    return Definitions::kIndexUnset;
  }

  int getFirstWindowIndexByChildIndex(const size_t childIndex) override {
    if (childIndex < firstWindowInChildIndices.size()) {
      return firstWindowInChildIndices[childIndex];
    }
    return Definitions::kIndexUnset;
  }

  [[nodiscard]] std::optional<PeriodUid> getChildUidByChildIndex(const size_t childIndex) const override {
    if (childIndex >= uids.size()) {
      return std::nullopt;
    }
    return uids[childIndex];
  }

  [[nodiscard]] int getWindowCount() const override {
    return windowCount;
  }

  [[nodiscard]] int getPeriodCount() const override {
    return periodCount;
  }
};

} // namespace coral

#endif //CORAL_PLAYBACK_DETAILS_PLAYLIST_TIMELINE_H
