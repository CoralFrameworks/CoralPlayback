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
#include <coral/source/SinglePeriodTimeline.h>

#include "UidGenerator.h"

namespace coral {

SinglePeriodTimeline::SinglePeriodTimeline(
    int64_t durationUs,
    bool isSeekable,
    bool isDynamic,
    bool isLive,
    const std::shared_ptr<MediaItem>& mediaItem) noexcept
    : SinglePeriodTimeline(durationUs,
    durationUs,
    0,
    0,
    isSeekable,
    isDynamic,
    isLive,
    mediaItem) {}

SinglePeriodTimeline::SinglePeriodTimeline(
    int64_t periodDurationUs,
    int64_t windowDurationUs,
    int64_t windowPositionInPeriodUs,
    int64_t windowDefaultStartPositionUs,
    bool isSeekable,
    bool isDynamic,
    bool isLive,
    const std::shared_ptr<MediaItem>& mediaItem) noexcept
    : SinglePeriodTimeline(Definitions::kTimeUnset,
    Definitions::kTimeUnset,
    Definitions::kTimeUnset,
    periodDurationUs,
    windowDurationUs,
    windowPositionInPeriodUs,
    windowDefaultStartPositionUs,
    isSeekable,
    isDynamic,
    isLive,
    mediaItem) {}

SinglePeriodTimeline::SinglePeriodTimeline(
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
    const std::shared_ptr<MediaItem>& mediaItem) noexcept
    : presentationStartTimeMs(presentationStartTimeMs),
      windowStartTimeMs(windowStartTimeMs),
      elapsedRealtimeEpochOffsetMs(elapsedRealtimeEpochOffsetMs),
      periodDurationUs(periodDurationUs),
      windowDurationUs(windowDurationUs),
      windowPositionInPeriodUs(windowPositionInPeriodUs),
      windowDefaultStartPositionUs(windowDefaultStartPositionUs),
      isSeekable(isSeekable),
      isDynamic(isDynamic),
      isLive(isLive),
      mediaItem(mediaItem) {

}

const PeriodUid& SinglePeriodTimeline::getUid() {
  static const auto sUid = PeriodUid(newUidString());
  return sUid;
}

std::optional<PeriodUid> SinglePeriodTimeline::getUidOfPeriod(int periodIndex) {
  return getUid();
}

int SinglePeriodTimeline::getIndexOfPeriod(const std::optional<PeriodUid>& uid) {
  return getUid() == uid ? 0 : Definitions::kIndexUnset;
}

std::shared_ptr<Timeline::Period>
SinglePeriodTimeline::getPeriod(int periodIndex, const std::shared_ptr<Period>& period,
    bool setIds) {
  auto uid = setIds ? std::optional<PeriodUid>(getUid()) : std::nullopt;
  period->set(std::nullopt, uid, 0, periodDurationUs, -windowPositionInPeriodUs);
  return period;
}

std::shared_ptr<Timeline::Window>
SinglePeriodTimeline::getWindow(int windowIndex, const std::shared_ptr<Window>& window,
    int64_t defaultPositionProjectionUs) {
  auto defaultStartPositionUs = this->windowDefaultStartPositionUs;
  if (isDynamic && defaultPositionProjectionUs != 0) {
    if (windowDurationUs == Definitions::kTimeUnset) {
      // Don't allow projection into a window that has an unknown duration.
      defaultStartPositionUs = Definitions::kTimeUnset;
    } else {
      defaultStartPositionUs += defaultPositionProjectionUs;
      if (defaultStartPositionUs > windowDurationUs) {
        // The projection takes us beyond the end of the window.
        defaultStartPositionUs = Definitions::kTimeUnset;
      }
    }
  }
  window->set(
      Window::getSingleWindowUid(),
      mediaItem,
      presentationStartTimeMs,
      windowStartTimeMs,
      elapsedRealtimeEpochOffsetMs,
      isSeekable,
      isDynamic,
      isLive,
      defaultStartPositionUs,
      windowDurationUs,
      /* firstPeriodIndex= */ 0,
      /* lastPeriodIndex= */ 0,
      windowPositionInPeriodUs);
  return window;
}

} // namespace coral
