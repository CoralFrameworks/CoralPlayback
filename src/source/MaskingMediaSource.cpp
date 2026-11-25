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
#include <coral/source/MaskingMediaSource.h>

#include <media/Definitions.h>

#include "UidGenerator.h"

#include <optional>

namespace coral {

MaskingMediaSource::MaskingMediaSource(const std::shared_ptr<MediaSource>& mediaSource,
    bool useLazyPreparation) noexcept
    : mediaSource(mediaSource),
      useLazyPreparation(useLazyPreparation && mediaSource->isSingleWindow()),
      hasStartedPreparing(false),
      isPrepared(false),
      hasRealTimeline(false) {
  caller = std::make_shared<MediaSourceCallerImpl>(this);
  window = std::make_shared<Timeline::Window>();
  period = std::make_shared<Timeline::Period>();
  auto initialTimeline = mediaSource->getInitialTimeline();
  if (initialTimeline != nullptr) {
    timeline = MaskingTimeline::createWithRealTimeline(initialTimeline, std::nullopt, std::nullopt);
    hasRealTimeline = true;
  } else {
    hasRealTimeline = false;
    timeline = MaskingTimeline::createWithPlaceholderTimeline(mediaSource->getMediaItem());
  }
}

MaskingMediaSource::~MaskingMediaSource() {
  if (mediaSource == nullptr) {
    return;
  }
  // The wrapped source was given the caller and the listener wrappers of this object, both of which
  // point back at it. Releasing them here is what keeps a source that is destroyed without
  // `releaseSource` from leaving callbacks into freed memory behind.
  for (const auto& registration : childEventListeners) {
    if (registration->eventListener != nullptr) {
      mediaSource->removeEventListener(registration->eventListener);
    }
  }
  childEventListeners.clear();
  if (caller != nullptr) {
    mediaSource->releaseSource(caller);
  }
}

void MaskingMediaSource::prepareSourceInternal() {
  if (!useLazyPreparation) {
    hasStartedPreparing = true;
    prepareChildSource();
  }
}

void MaskingMediaSource::prepareChildSource() {
  mediaSource->prepareSource(caller);
  if (!isEnabled()) {
    mediaSource->disable(caller);
  }
}

void MaskingMediaSource::releaseSourceInternal() {
  mediaSource->releaseSource(caller);
  isPrepared = false;
  hasStartedPreparing = false;
}

std::shared_ptr<MediaPeriod>
MaskingMediaSource::createPeriod(MediaPeriodId id, const std::shared_ptr<Allocator>& allocator,
    int64_t startPositionUs) {
  auto mediaPeriod =
      std::make_shared<MaskingMediaPeriod>(mediaSource, id, allocator, startPositionUs);
  if (isPrepared) {
    auto const idInSource = id.copyWithPeriodUid(getInternalPeriodUid(id.periodUid()));
    mediaPeriod->createPeriod(idInSource);
  } else {
    // We should have at most one media period while source is unprepared because the duration is
    // unset and we don't load beyond periods with unset duration. We need to figure out how to
    // handle the prepare positions of multiple deferred media periods, should that ever change.
    unpreparedMaskingMediaPeriod = mediaPeriod;
    if (!hasStartedPreparing) {
      hasStartedPreparing = true;
      prepareChildSource();
    }
  }
  return mediaPeriod;
}

void MaskingMediaSource::releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) {
  auto maskingPeriod = std::dynamic_pointer_cast<MaskingMediaPeriod>(mediaPeriod);
  maskingPeriod->releasePeriod();
  if (maskingPeriod == unpreparedMaskingMediaPeriod) {
    unpreparedMaskingMediaPeriod.reset();
  }
}

void MaskingMediaSource::addEventListener(
    const std::shared_ptr<EventHandler>& eventHandler,
    const std::shared_ptr<MediaSourceEventListener>& eventListener) {
  auto wrapper = std::make_shared<MaskingEventListener>(this, eventListener);
  childEventListeners.push_back(
      std::make_shared<EventListenerAndHandler>(eventHandler, wrapper));
  mediaSource->addEventListener(eventHandler, wrapper);
}

void MaskingMediaSource::removeEventListener(
    const std::shared_ptr<MediaSourceEventListener>& eventListener) {
  for (const auto& registration : childEventListeners) {
    const auto wrapper = std::dynamic_pointer_cast<MaskingEventListener>(registration->eventListener);
    if (wrapper != nullptr && wrapper->getTarget() == eventListener) {
      mediaSource->removeEventListener(wrapper);
    }
  }
  auto position = childEventListeners.begin();
  while (position != childEventListeners.end()) {
    const auto wrapper = std::dynamic_pointer_cast<MaskingEventListener>(position->get()->eventListener);
    if (wrapper != nullptr && wrapper->getTarget() == eventListener) {
      position = childEventListeners.erase(position);
    } else {
      ++position;
    }
  }
}

void MaskingMediaSource::onChildSourceInfoRefreshed(
    const std::shared_ptr<MediaSource>& source,
    const std::shared_ptr<Timeline>& newTimeline) {
  std::optional<MediaPeriodId> idForMaskingPeriodPreparation = std::nullopt;
  if (isPrepared) {
    timeline = timeline->cloneWithUpdatedTimeline(newTimeline);
    if (unpreparedMaskingMediaPeriod != nullptr) {
      // Reset override in case the duration changed and we need to update our override.
      setPreparePositionOverrideToUnpreparedMaskingPeriod(
          unpreparedMaskingMediaPeriod->getPreparePositionOverrideUs());
    }
  } else if (newTimeline->isEmpty()) {
    timeline =
        hasRealTimeline
        ? timeline->cloneWithUpdatedTimeline(newTimeline)
        : MaskingTimeline::createWithRealTimeline(
            newTimeline,
            Timeline::Window::getSingleWindowUid(),
            MaskingTimeline::getMaskingExternalPeriodUid());
  } else {
    // Determine first period and the start position.
    // This will be:
    //  1. The default window start position if no deferred period has been created yet.
    //  2. The non-zero prepare position of the deferred period under the assumption that this is
    //     a non-zero initial seek position in the window.
    //  3. The default window start position if the deferred period has a prepare position of zero
    //     under the assumption that the prepare position of zero was used because it's the
    //     default position of the PlaceholderTimeline window. Note that this will override an
    //     intentional seek to zero for a window with a non-zero default position. This is
    //     unlikely to be a problem as a non-zero default position usually only occurs for live
    //     playbacks and seeking to zero in a live window would cause BehindLiveWindowExceptions
    //     anyway.
    newTimeline->getWindow(/* windowIndex= */ 0, window);
    auto windowStartPositionUs = window->getDefaultPositionUs();
    if (unpreparedMaskingMediaPeriod != nullptr) {
      auto periodPreparePositionUs = unpreparedMaskingMediaPeriod->getPreparePositionUs();
      if (periodPreparePositionUs != 0) {
        windowStartPositionUs = periodPreparePositionUs;
      }
    }
    auto windowUid = window->uid;
    auto periodPosition =
        newTimeline->getPeriodPosition(
            window, period, /* windowIndex= */ 0, windowStartPositionUs);
    auto periodUid = periodPosition.first;
    auto periodPositionUs = periodPosition.second;
    timeline =
        hasRealTimeline
        ? timeline->cloneWithUpdatedTimeline(newTimeline)
        : MaskingTimeline::createWithRealTimeline(newTimeline, windowUid, periodUid);
    if (unpreparedMaskingMediaPeriod != nullptr) {
      auto const& maskingPeriod = unpreparedMaskingMediaPeriod;
      setPreparePositionOverrideToUnpreparedMaskingPeriod(periodPositionUs);
      idForMaskingPeriodPreparation =
          maskingPeriod->id.copyWithPeriodUid(getInternalPeriodUid(maskingPeriod->id.periodUid()));
    }
  }
  hasRealTimeline = true;
  isPrepared = true;
  refreshSourceInfo(timeline);
  if (idForMaskingPeriodPreparation != std::nullopt) {
    unpreparedMaskingMediaPeriod->createPeriod(idForMaskingPeriodPreparation.value());
  }
}

PeriodUid MaskingMediaSource::getInternalPeriodUid(const PeriodUid& externalPeriodUid) {
  return timeline->replacedInternalPeriodUid != std::nullopt
         && externalPeriodUid == MaskingTimeline::getMaskingExternalPeriodUid()
         ? timeline->replacedInternalPeriodUid.value()
         : externalPeriodUid;
}

PeriodUid MaskingMediaSource::getExternalPeriodUid(const PeriodUid& internalPeriodUid) {
  return timeline->replacedInternalPeriodUid != std::nullopt
         && timeline->replacedInternalPeriodUid == internalPeriodUid
         ? MaskingTimeline::getMaskingExternalPeriodUid()
         : internalPeriodUid;
}

void MaskingMediaSource::setPreparePositionOverrideToUnpreparedMaskingPeriod(
    int64_t preparePositionOverrideUs) {
  auto maskingPeriod = unpreparedMaskingMediaPeriod;
  auto maskingPeriodIndex = timeline->getIndexOfPeriod(maskingPeriod->id.periodUid());
  if (maskingPeriodIndex == Definitions::kIndexUnset) {
    // The new timeline doesn't contain this period anymore. This can happen if the media source
    // has multiple periods and removed the first period with a timeline update. Ignore the
    // update, as the non-existing period will be released anyway as soon as the player receives
    // this new timeline.
    return;
  }
  auto periodDurationUs = timeline->getPeriod(maskingPeriodIndex, period, false)->durationUs;
  if (periodDurationUs != Definitions::kTimeUnset) {
    // Ensure the overridden position doesn't exceed the period duration.
    if (preparePositionOverrideUs >= periodDurationUs) {
      preparePositionOverrideUs = std::max(int64_t(0), periodDurationUs - 1);
    }
  }
  maskingPeriod->overridePreparePositionUs(preparePositionOverrideUs);
}

//--------------------------------------------------------------------------------------------------

void MaskingMediaSource::MaskingEventListener::onLoadStarted(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId,
    const LoadEventInfo& loadEventInfo,
    const MediaLoadData& mediaLoadData) {
  target->onLoadStarted(windowIndex, toExternalMediaPeriodId(mediaPeriodId), loadEventInfo,
      mediaLoadData);
}

void MaskingMediaSource::MaskingEventListener::onMediaPeriodCreated(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId) {
  target->onMediaPeriodCreated(windowIndex, toExternalMediaPeriodId(mediaPeriodId));
}

void MaskingMediaSource::MaskingEventListener::onMediaPeriodReleased(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId) {
  target->onMediaPeriodReleased(windowIndex, toExternalMediaPeriodId(mediaPeriodId));
}

void MaskingMediaSource::MaskingEventListener::onLoadCompleted(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId,
    const LoadEventInfo& loadEventInfo,
    const MediaLoadData& mediaLoadData) {
  target->onLoadCompleted(windowIndex, toExternalMediaPeriodId(mediaPeriodId), loadEventInfo,
      mediaLoadData);
}

void MaskingMediaSource::MaskingEventListener::onLoadCanceled(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId,
    const LoadEventInfo& loadEventInfo,
    const MediaLoadData& mediaLoadData) {
  target->onLoadCanceled(windowIndex, toExternalMediaPeriodId(mediaPeriodId), loadEventInfo,
      mediaLoadData);
}

void MaskingMediaSource::MaskingEventListener::onLoadError(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId,
    const LoadEventInfo& loadEventInfo,
    const MediaLoadData& mediaLoadData,
    const Error* error,
    const bool wasCanceled) {
  target->onLoadError(windowIndex, toExternalMediaPeriodId(mediaPeriodId), loadEventInfo,
      mediaLoadData, error, wasCanceled);
}

void MaskingMediaSource::MaskingEventListener::onUpstreamDiscarded(const int windowIndex,
    const MediaPeriodId& mediaPeriodId, const MediaLoadData& mediaLoadData) {
  const auto externalMediaPeriodId =
      toExternalMediaPeriodId(std::optional<MediaPeriodId>(mediaPeriodId));
  if (!externalMediaPeriodId.has_value()) {
    return;
  }
  target->onUpstreamDiscarded(windowIndex, externalMediaPeriodId.value(), mediaLoadData);
}

void MaskingMediaSource::MaskingEventListener::onDownstreamFormatChanged(const int windowIndex,
    const std::optional<MediaPeriodId>& mediaPeriodId, const MediaLoadData& mediaLoadData) {
  target->onDownstreamFormatChanged(windowIndex, toExternalMediaPeriodId(mediaPeriodId),
      mediaLoadData);
}

std::optional<MediaPeriodId> MaskingMediaSource::MaskingEventListener::toExternalMediaPeriodId(
    const std::optional<MediaPeriodId>& internalMediaPeriodId) const {
  if (!internalMediaPeriodId.has_value()) {
    return std::nullopt;
  }
  return internalMediaPeriodId->copyWithPeriodUid(
      mediaSource->getExternalPeriodUid(internalMediaPeriodId->periodUid()));
}

//--------------------------------------------------------------------------------------------------

void MaskingMediaSource::MediaSourceCallerImpl::onSourceInfoRefreshed(
    const std::shared_ptr<MediaSource>& source, const std::shared_ptr<Timeline>& newTimeline) {
  mediaSource->onChildSourceInfoRefreshed(source, newTimeline);
}

//--------------------------------------------------------------------------------------------------

/** A timeline with one dynamic window with a period of indeterminate duration. */
class PlaceholderTimeline final : public Timeline {
private:
  std::shared_ptr<MediaItem> mediaItem;

public:
  explicit PlaceholderTimeline(const std::shared_ptr<MediaItem>& mediaItem) noexcept
      : mediaItem(mediaItem) {}

  [[nodiscard]] int getWindowCount() const override {
    return 1;
  }

  std::shared_ptr<Window> getWindow(int windowIndex,
      const std::shared_ptr<Window>& window,
      int64_t defaultPositionProjectionUs) override {
    window->set(
        Window::getSingleWindowUid(),
        mediaItem,
        /* presentationStartTimeMs= */ Definitions::kTimeUnset,
        /* windowStartTimeMs= */ Definitions::kTimeUnset,
        /* elapsedRealtimeEpochOffsetMs= */ Definitions::kTimeUnset,
        /* isSeekable= */ false,
        // Dynamic window to indicate pending timeline updates.
        /* isDynamic= */ true,
        /* isLive= */ false,
        /* defaultPositionUs= */ 0,
        /* durationUs= */ Definitions::kTimeUnset,
        /* firstPeriodIndex= */ 0,
        /* lastPeriodIndex= */ 0,
        /* positionInFirstPeriodUs= */ 0);
    window->isPlaceholder = true;
    return window;
  }

  [[nodiscard]] int getPeriodCount() const override {
    return 1;
  }

  std::shared_ptr<Period>
  getPeriod(int periodIndex, const std::shared_ptr<Period>& period, bool setIds) override;

  int getIndexOfPeriod(const std::optional<PeriodUid>& uid) override {
    return uid == MaskingTimeline::getMaskingExternalPeriodUid() ? 0 : Definitions::kIndexUnset;
  }

  std::optional<PeriodUid> getUidOfPeriod(int periodIndex) override {
    return MaskingTimeline::getMaskingExternalPeriodUid();
  }
};

std::shared_ptr<Timeline::Period>
PlaceholderTimeline::getPeriod(int periodIndex, const std::shared_ptr<Period>& period,
    bool setIds) {
  period->set(setIds ? std::optional<PeriodUid>(PeriodUid(std::to_string(0)))
                     : std::nullopt,
      setIds ? std::optional<PeriodUid>(MaskingTimeline::getMaskingExternalPeriodUid())
             : std::nullopt,
      0, Definitions::kTimeUnset, 0);
  return period;
}

//--------------------------------------------------------------------------------------------------

const PeriodUid& MaskingTimeline::getMaskingExternalPeriodUid() {
  static const PeriodUid sExternalUid = PeriodUid(newUidString());
  return sExternalUid;
}

std::shared_ptr<MaskingTimeline>
MaskingTimeline::createWithPlaceholderTimeline(const std::shared_ptr<MediaItem>& mediaItem) {
  auto timeline = std::make_shared<PlaceholderTimeline>(mediaItem);
  return std::make_shared<MaskingTimeline>(timeline, Window::getSingleWindowUid(),
      getMaskingExternalPeriodUid());
}

std::shared_ptr<MaskingTimeline>
MaskingTimeline::createWithRealTimeline(const std::shared_ptr<Timeline>& timeline,
    const std::optional<PeriodUid>& firstWindowUid,
    const std::optional<PeriodUid>& firstPeriodUid) {
  return std::make_shared<MaskingTimeline>(timeline, firstWindowUid, firstPeriodUid);
}

MaskingTimeline::MaskingTimeline(
    const std::shared_ptr<Timeline>& timeline,
    const std::optional<PeriodUid>& replacedInternalWindowUid,
    const std::optional<PeriodUid>& replacedInternalPeriodUid) noexcept
    : ForwardingTimeline(timeline),
      replacedInternalWindowUid(replacedInternalWindowUid),
      replacedInternalPeriodUid(replacedInternalPeriodUid) {
}

std::shared_ptr<MaskingTimeline>
MaskingTimeline::cloneWithUpdatedTimeline(const std::shared_ptr<Timeline>& timeline) {
  return std::make_shared<MaskingTimeline>(
      timeline, replacedInternalWindowUid, replacedInternalPeriodUid);
}

std::shared_ptr<Timeline::Window>
MaskingTimeline::getWindow(int windowIndex, const std::shared_ptr<Window>& window,
    int64_t defaultPositionProjectionUs) {
  timeline->getWindow(windowIndex, window, defaultPositionProjectionUs);
  // A masking timeline that was built from the initial timeline of the source does not replace any
  // identifier, so the optional is empty and nothing has to be remapped.
  if (replacedInternalWindowUid.has_value() && window->uid == replacedInternalWindowUid.value()) {
    window->uid = Window::getSingleWindowUid();
  }
  return window;
}

std::shared_ptr<Timeline::Period>
MaskingTimeline::getPeriod(int periodIndex, const std::shared_ptr<Period>& period, bool setIds) {
  timeline->getPeriod(periodIndex, period, setIds);
  if (setIds && replacedInternalPeriodUid.has_value() &&
      period->uid == replacedInternalPeriodUid.value()) {
    period->uid = getMaskingExternalPeriodUid();
  }
  return period;
}

int MaskingTimeline::getIndexOfPeriod(const std::optional<PeriodUid>& uid) {
  return timeline->getIndexOfPeriod(
      getMaskingExternalPeriodUid() == uid && replacedInternalPeriodUid != std::nullopt
      ? replacedInternalPeriodUid.value()
      : uid);
}

std::optional<PeriodUid> MaskingTimeline::getUidOfPeriod(int periodIndex) {
  auto uid = timeline->getUidOfPeriod(periodIndex);
  return (uid == replacedInternalPeriodUid) ? getMaskingExternalPeriodUid() : uid;
}

} // namespace coral
