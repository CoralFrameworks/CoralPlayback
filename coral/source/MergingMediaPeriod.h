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
#ifndef CORAL_PLAYBACK_SOURCE_MERGING_MEDIA_PERIOD_H
#define CORAL_PLAYBACK_SOURCE_MERGING_MEDIA_PERIOD_H

#include <coral/source/MediaPeriod.h>
#include <coral/source/MergingMediaSource.h>

#include <memory>
#include <vector>

namespace coral {

/**
 * A media period that merges the track groups and sample streams of child periods.
 *
 * <p>Minimal form for one period of each child: the merged track group array is the concatenation
 * of the children's groups, and a selection is routed to the child that owns its group.
 */
class MergingMediaPeriod final : public MediaPeriod,
                                 public std::enable_shared_from_this<MergingMediaPeriod> {
public:
  MergingMediaPeriod(std::vector<std::shared_ptr<MediaPeriod>> periods,
      std::shared_ptr<TrackGroupArray> trackGroups,
      std::vector<SubtitleMetadata> metadata = {});

  ~MergingMediaPeriod() override = default;

  void prepare(Callback* callback, int64_t positionUs) override;
  void maybeThrowPrepareError() override;

  /**
   * Returns the first failure of the merged periods, or an empty string while they can be prepared.
   */
  [[nodiscard]] std::string getPrepareError() const override;
  [[nodiscard]] const std::shared_ptr<TrackGroupArray>& getTrackGroups() const override;
  /** Drops the child periods after the source has released them. */
  void releaseChildPeriods();
  int64_t selectTracks(const std::vector<std::shared_ptr<TrackSelection>>& selections,
      const std::vector<bool>& mayRetainStreamFlags,
      std::vector<std::shared_ptr<SampleStream>>& streams,
      std::vector<bool>& streamResetFlags, int64_t positionUs) override;
  void discardBuffer(int64_t positionUs, bool toKeyframe) override;
  int64_t readDiscontinuity() override;
  int64_t seekToUs(int64_t positionUs) override;
  int64_t getAdjustedSeekPositionUs(int64_t positionUs,
      const SeekParameters& seekParameters) override;

  int64_t getBufferedPositionUs() override;
  int64_t getNextLoadPositionUs() override;
  bool continueLoading(int64_t positionUs) override;
  bool isLoading() override;
  void reevaluateBuffer(int64_t positionUs) override;

private:
  friend class MergingMediaSource;
  class ChildCallback;
  void onChildPrepared();

  std::vector<std::shared_ptr<MediaPeriod>> periods;
  std::vector<SubtitleMetadata> metadata;
  /** The child that owns each stream index, or -1 when no child owns it. */
  std::vector<int> streamOwners;
  std::shared_ptr<TrackGroupArray> trackGroups;
  std::vector<std::shared_ptr<ChildCallback>> childCallbacks;
  Callback* callback = nullptr;
  int preparedCount = 0;
  int64_t preparePositionUs = 0;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MERGING_MEDIA_PERIOD_H
