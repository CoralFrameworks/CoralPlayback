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

#ifndef CORAL_PLAYBACK_SOURCE_SILENCE_MEDIA_PERIOD_H
#define CORAL_PLAYBACK_SOURCE_SILENCE_MEDIA_PERIOD_H

#include <coral/source/MediaPeriod.h>

#include <memory>
#include <vector>

namespace coral {

class SilenceMediaPeriod final : public MediaPeriod,
                                 public std::enable_shared_from_this<SilenceMediaPeriod> {
public:
  explicit SilenceMediaPeriod(int64_t durationUs) noexcept;

  void release();

  void prepare(Callback* callback, int64_t positionUs) override;

  const std::shared_ptr<TrackGroupArray>& getTrackGroups() const override;

  int64_t selectTracks(const std::vector<std::shared_ptr<TrackSelection>>& selections,
      const std::vector<bool>& mayRetainStreamFlags,
      std::vector<std::shared_ptr<SampleStream>>& streams,
      std::vector<bool>& streamResetFlags,
      int64_t positionUs) override;

  int64_t getAdjustedSeekPositionUs(int64_t positionUs, const SeekParameters& parameters) override;

  int64_t getBufferedPositionUs() override;

  int64_t getNextLoadPositionUs() override;

  bool continueLoading(int64_t positionUs) override;

  bool isLoading() override;

  void discardBuffer(int64_t positionUs, bool toKeyframe) override;

  int64_t readDiscontinuity() override;

  int64_t seekToUs(int64_t positionUs) override;

  void reevaluateBuffer(int64_t positionUs) override;

private:
  int64_t constrainSeekPosition(int64_t positionUs);

  int64_t durationUs;
  std::vector<std::shared_ptr<SampleStream>> sampleStreams;
  std::shared_ptr<TrackGroupArray> tracks;
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_SILENCE_MEDIA_PERIOD_H
