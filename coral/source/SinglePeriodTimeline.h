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
#ifndef CORAL_PLAYBACK_SOURCE_SINGLE_PERIOD_TIMELINE_H
#define CORAL_PLAYBACK_SOURCE_SINGLE_PERIOD_TIMELINE_H

#include <coral/source/Timeline.h>

#include <memory>

namespace coral {

class SinglePeriodTimeline : public Timeline {
private:
  const int64_t presentationStartTimeMs;
  const int64_t windowStartTimeMs;
  const int64_t elapsedRealtimeEpochOffsetMs;
  const int64_t periodDurationUs;
  const int64_t windowDurationUs;
  const int64_t windowPositionInPeriodUs;
  const int64_t windowDefaultStartPositionUs;
  const bool isSeekable;
  const bool isDynamic;
  const bool isLive;
  std::shared_ptr<MediaItem> mediaItem;
public:
  explicit SinglePeriodTimeline(
      int64_t durationUs,
      bool isSeekable,
      bool isDynamic,
      bool isLive,
      const std::shared_ptr<MediaItem>& mediaItem) noexcept;

  explicit SinglePeriodTimeline(
      int64_t periodDurationUs,
      int64_t windowDurationUs,
      int64_t windowPositionInPeriodUs,
      int64_t windowDefaultStartPositionUs,
      bool isSeekable,
      bool isDynamic,
      bool isLive,
      const std::shared_ptr<MediaItem>& mediaItem) noexcept;

  explicit SinglePeriodTimeline(
      int64_t presentationStartTimeMs,
      int64_t windowStartTimeMs,
      int64_t elapsedRealtimeEpochOffsetMs,
      int64_t periodDurationUs,
      int64_t windowDurationUs,
      int64_t windowPositionInPeriodUs,
      int64_t windowDefaultStartPositionUs,
      bool isSeekable,
      bool isDynamic,
      bool isLive,
      const std::shared_ptr<MediaItem>& mediaItem) noexcept;

  [[nodiscard]] int getWindowCount() const override {
    return 1;
  }

  std::shared_ptr<Window>
  getWindow(int windowIndex, const std::shared_ptr<Window>& window,
      int64_t defaultPositionProjectionUs) override;

  [[nodiscard]] int getPeriodCount() const override {
    return 1;
  }

  std::shared_ptr<Period>
  getPeriod(int periodIndex, const std::shared_ptr<Period>& period, bool setIds) override;

  int getIndexOfPeriod(const std::optional<PeriodUid>& uid) override;

  std::optional<PeriodUid> getUidOfPeriod(int periodIndex) override;

private:
  static const PeriodUid& getUid();
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_SINGLE_PERIOD_TIMELINE_H
