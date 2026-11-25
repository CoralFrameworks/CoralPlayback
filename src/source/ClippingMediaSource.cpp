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
#include <coral/source/ClippingMediaSource.h>

#include <media/Definitions.h>

namespace coral {

ClippingTimeline::ClippingTimeline(const std::shared_ptr<Timeline>& timeline,
    int64_t startUs,
    int64_t endUs) noexcept
    : ForwardingTimeline(timeline) {
  auto window = timeline->getWindow(0, std::make_shared<Window>());
  startUs = std::max(int64_t(0), startUs);
  if (!window->isPlaceholder && startUs != 0 && !window->isSeekable) {

  }
  long resolvedEndUs =
      endUs == Definitions::kTimeEndOfSource ? window->durationUs
                                             : std::max(int64_t(0), endUs);
  if (window->durationUs != Definitions::kTimeUnset) {
    if (resolvedEndUs > window->durationUs) {
      resolvedEndUs = window->durationUs;
    }
    if (startUs > resolvedEndUs) {
//        throw new IllegalClippingException(IllegalClippingException.REASON_START_EXCEEDS_END);
    }
  }
  this->startUs = startUs;
  this->endUs = resolvedEndUs;
  durationUs =
      resolvedEndUs == Definitions::kTimeUnset ? Definitions::kTimeUnset
                                               : (resolvedEndUs - startUs);
  isDynamic =
      window->isDynamic
      && (
          resolvedEndUs == Definitions::kTimeUnset
          || (
              window->durationUs != Definitions::kTimeUnset
              && resolvedEndUs == window->durationUs));
}

std::shared_ptr<Timeline::Window>
ClippingTimeline::getWindow(int windowIndex, const std::shared_ptr<Window>& window,
    int64_t defaultPositionProjectionUs) {
  timeline->getWindow(/* windowIndex= */ 0, window, /* defaultPositionProjectionUs= */ 0);
  window->positionInFirstPeriodUs += startUs;
  window->durationUs = durationUs;
  window->isDynamic = isDynamic;
  if (window->defaultPositionUs != Definitions::kTimeUnset) {
    window->defaultPositionUs = std::max(window->defaultPositionUs, startUs);
    window->defaultPositionUs =
        endUs == Definitions::kTimeUnset ? window->defaultPositionUs
                                         : std::min(window->defaultPositionUs, endUs);
    window->defaultPositionUs -= startUs;
  }
  auto startMs = startUs / 1000;
  if (window->presentationStartTimeMs != Definitions::kTimeUnset) {
    window->presentationStartTimeMs += startMs;
  }
  if (window->windowStartTimeMs != Definitions::kTimeUnset) {
    window->windowStartTimeMs += startMs;
  }
  return window;
}

std::shared_ptr<Timeline::Period>
ClippingTimeline::getPeriod(
    int periodIndex, const std::shared_ptr<Period>& period, bool setIds) {
  timeline->getPeriod(/* periodIndex= */ 0, period, setIds);
  auto positionInClippedWindowUs = period->getPositionInWindowUs() - startUs;
  auto periodDurationUs =
      durationUs == Definitions::kTimeUnset ? Definitions::kTimeUnset
                                            : durationUs - positionInClippedWindowUs;
  period->set(period->id, period->uid, 0, periodDurationUs, positionInClippedWindowUs);
  return period;
}

//--------------------------------------------------------------------------------------------------

ClippingMediaSource::ClippingMediaSource(const std::shared_ptr<MediaSource>& mediaSource,
    int64_t startPositionUs,
    int64_t endPositionUs) noexcept
    : ClippingMediaSource(mediaSource, startPositionUs, endPositionUs, true, false, false) {

}

ClippingMediaSource::ClippingMediaSource(const std::shared_ptr<MediaSource>& mediaSource,
    int64_t durationUs) noexcept
    : ClippingMediaSource(mediaSource, 0, durationUs, true, false, false) {

}

ClippingMediaSource::ClippingMediaSource(
    const std::shared_ptr<MediaSource>& mediaSource,
    int64_t startPositionUs,
    int64_t endPositionUs,
    bool enableInitialDiscontinuity,
    bool allowDynamicClippingUpdates,
    bool relativeToDefaultPosition) noexcept
    : mediaSource(mediaSource),
      startUs(startPositionUs),
      endUs(endPositionUs),
      enableInitialDiscontinuity(enableInitialDiscontinuity),
      allowDynamicClippingUpdates(allowDynamicClippingUpdates),
      relativeToDefaultPosition(relativeToDefaultPosition),
      periodStartUs(0),
      periodEndUs(0) {
  window = std::make_shared<Timeline::Window>();
  caller = std::make_shared<MediaSourceCallerImpl>(this);
}

ClippingMediaSource::~ClippingMediaSource() {
  // The wrapped source was given the caller of this object, which points back at it. Releasing it
  // here is what keeps a source that is destroyed without `releaseSource` from leaving a callback
  // into freed memory behind. `releaseSource` is idempotent.
  if (mediaSource != nullptr && caller != nullptr) {
    mediaSource->releaseSource(caller);
  }
}

void ClippingMediaSource::prepareSourceInternal() {
  prepareChildSource();
}

void ClippingMediaSource::prepareChildSource() {
  mediaSource->prepareSource(caller);
  if (!isEnabled()) {
    mediaSource->disable(caller);
  }
}

std::shared_ptr<MediaPeriod>
ClippingMediaSource::createPeriod(MediaPeriodId id,
    const std::shared_ptr<Allocator>& allocator,
    int64_t startPositionUs) {
  auto mediaPeriod =
      std::make_shared<ClippingMediaPeriod>(
          mediaSource->createPeriod(id, allocator, startPositionUs),
          enableInitialDiscontinuity,
          periodStartUs,
          periodEndUs);
  mediaPeriods.push_back(mediaPeriod);
  return mediaPeriod;
}

void ClippingMediaSource::releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) {
  auto position = std::find(mediaPeriods.begin(), mediaPeriods.end(), mediaPeriod);
  if (position != mediaPeriods.end()) {
    mediaPeriods.erase(position);
    auto clippingPeriod = std::dynamic_pointer_cast<ClippingMediaPeriod>(mediaPeriod);
    auto childPeriod = clippingPeriod->getMediaPeriod();
    mediaSource->releasePeriod(childPeriod);
    if (mediaPeriods.empty() && !allowDynamicClippingUpdates) {
      refreshClippedTimeline(clippingTimeline->getTimeline());
    }
  }
}

void ClippingMediaSource::releaseSourceInternal() {
  clippingTimeline.reset();
}

void ClippingMediaSource::onChildSourceInfoRefreshed(
    const std::shared_ptr<MediaSource>& source,
    const std::shared_ptr<Timeline>& timeline) {
  refreshClippedTimeline(timeline);
}

int64_t ClippingMediaSource::getMediaTimeForChildMediaTime(int64_t mediaTimeMs) const {
  if (mediaTimeMs == Definitions::kTimeUnset) {
    return Definitions::kTimeUnset;
  }
  auto const startMs = startUs / 1000;
  auto clippedTimeMs = std::max(int64_t(0), mediaTimeMs - startMs);
  if (endUs != Definitions::kTimeEndOfSource) {
    clippedTimeMs = std::min(endUs / 1000 - startMs, clippedTimeMs);
  }
  return clippedTimeMs;
}

void ClippingMediaSource::refreshClippedTimeline(const std::shared_ptr<Timeline>& timeline) {
  int64_t windowStartUs;
  int64_t windowEndUs;
  timeline->getWindow(/* windowIndex= */ 0, window);
  int64_t windowPositionInPeriodUs = window->getPositionInFirstPeriodUs();
  if (clippingTimeline == nullptr || mediaPeriods.empty() || allowDynamicClippingUpdates) {
    windowStartUs = startUs;
    windowEndUs = endUs;
    if (relativeToDefaultPosition) {
      long windowDefaultPositionUs = window->getDefaultPositionUs();
      windowStartUs += windowDefaultPositionUs;
      windowEndUs += windowDefaultPositionUs;
    }
    periodStartUs = windowPositionInPeriodUs + windowStartUs;
    periodEndUs =
        endUs == Definitions::kTimeEndOfSource ? Definitions::kTimeEndOfSource
                                               : windowPositionInPeriodUs + windowEndUs;
    for (const auto& mediaPeriod: mediaPeriods) {
      mediaPeriod->updateClipping(periodStartUs, periodEndUs);
    }
  } else {
    // Keep window fixed at previous period position.
    windowStartUs = periodStartUs - windowPositionInPeriodUs;
    windowEndUs =
        endUs == Definitions::kTimeEndOfSource ? Definitions::kTimeEndOfSource
                                               : periodEndUs - windowPositionInPeriodUs;
  }
  clippingTimeline = std::make_shared<ClippingTimeline>(timeline, windowStartUs, windowEndUs);
  refreshSourceInfo(clippingTimeline);
}

//--------------------------------------------------------------------------------------------------

void ClippingMediaSource::MediaSourceCallerImpl::onSourceInfoRefreshed(
    const std::shared_ptr<MediaSource>& source, const std::shared_ptr<Timeline>& timeline) {
  mediaSource->onChildSourceInfoRefreshed(source, timeline);
}

} // namespace coral
