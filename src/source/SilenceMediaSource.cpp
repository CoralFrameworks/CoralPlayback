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
#include <coral/source/SilenceMediaSource.h>

#include <coral/source/SilenceMediaPeriod.h>
#include <coral/source/SinglePeriodTimeline.h>

#include <cassert>
#include <utility>

namespace coral {

SilenceMediaSource::SilenceMediaSource(const int64_t durationUs,
    std::shared_ptr<MediaItem> mediaItem) noexcept
    : durationUs(durationUs < 0 ? 0 : durationUs),
      mediaItem(std::move(mediaItem)) {
  // The duration is known before anything is loaded, so the timeline is built once, here, and is
  // both the initial timeline and the one the source reports once it is prepared.
  timeline = std::make_shared<SinglePeriodTimeline>(
      this->durationUs,
      /* isSeekable= */ true,
      /* isDynamic= */ false,
      /* isLive= */ false,
      this->mediaItem);
}

const std::shared_ptr<Timeline>& SilenceMediaSource::getInitialTimeline() const {
  return timeline;
}

std::shared_ptr<MediaPeriod> SilenceMediaSource::createPeriod(MediaPeriodId id,
    const std::shared_ptr<Allocator>& allocator,
    int64_t startPositionUs) {
  static_cast<void>(id);
  static_cast<void>(allocator);
  // Silence is generated, so the period needs no allocator and ignores the position it starts from:
  // the first sample it delivers is the one at the position that was asked for.
  static_cast<void>(startPositionUs);
  return std::make_shared<SilenceMediaPeriod>(durationUs);
}

void SilenceMediaSource::releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) {
  auto silencePeriod = std::dynamic_pointer_cast<SilenceMediaPeriod>(mediaPeriod);
  assert(silencePeriod != nullptr);
  if (silencePeriod != nullptr) {
    silencePeriod->release();
  }
}

void SilenceMediaSource::prepareSourceInternal() {
  refreshSourceInfo(timeline);
}

void SilenceMediaSource::releaseSourceInternal() {
  // The source owns nothing but its timeline, which is reused when it is prepared again.
}

} // namespace coral
