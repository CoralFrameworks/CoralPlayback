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
#include "MediaPeriodQueue.h"

#include <media/Definitions.h>

#include <cassert>

namespace coral {

MediaPeriodQueue::MediaPeriodQueue(MediaPeriodHolder::Factory* factory) noexcept
  : mediaPeriodHolderFactory(factory),
    nextWindowSequenceNumber(Definitions::kIndexUnset),
    repeatMode(RepeatMode::REPEAT_MODE_OFF),
    shuffleModeEnabled(false),
    length(0),
    oldFrontPeriodUid(std::nullopt),
    oldFrontPeriodWindowSequenceNumber(Definitions::kIndexUnset) {
  period = std::make_shared<Timeline::Period>();
  window = std::make_shared<Timeline::Window>();
}

bool MediaPeriodQueue::updateRepeatMode(const std::shared_ptr<Timeline>& timeline,
  const RepeatMode newMode) {
  repeatMode = newMode;
  return updateForPlaybackModeChange(timeline);
}

bool MediaPeriodQueue::updateShuffleModeEnabled(const std::shared_ptr<Timeline>& timeline,
  const bool newModeEnabled) {
  shuffleModeEnabled = newModeEnabled;
  return updateForPlaybackModeChange(timeline);
}

bool MediaPeriodQueue::isLoading(const std::shared_ptr<MediaPeriod>& mediaPeriod) const {
  return loading != nullptr && loading->getMediaPeriod() == mediaPeriod;
}

void MediaPeriodQueue::reevaluateBuffer(const int64_t rendererPositionUs) {
  if (loading != nullptr) {
    loading->reevaluateBuffer(rendererPositionUs);
  }
}

bool MediaPeriodQueue::shouldLoadNextMediaPeriod() {
  return loading == nullptr
         || (
           !loading->getInfo().isFinal
           && loading->isFullyBuffered()
           && loading->getInfo().durationUs != Definitions::kTimeUnset
           && length < MAXIMUM_BUFFER_AHEAD_PERIODS);
}

std::optional<MediaPeriodInfo>
MediaPeriodQueue::getNextMediaPeriodInfo(int64_t rendererPositionUs,
  const PlaybackInfo& playbackInfo) {
  return loading == nullptr
           ? getFirstMediaPeriodInfo(playbackInfo)
           : getFollowingMediaPeriodInfo(playbackInfo.timeline, loading, rendererPositionUs);
}

const std::shared_ptr<MediaPeriodHolder>&
MediaPeriodQueue::enqueueNextMediaPeriodHolder(const MediaPeriodInfo& info) {
  auto rendererPositionOffsetUs =
    loading == nullptr
      ? 0
      : (loading->getRendererOffset() + loading->getInfo().durationUs - info.startPositionUs);
  auto newPeriodHolder =
    mediaPeriodHolderFactory->createMediaPeriodHolder(info, rendererPositionOffsetUs);
  if (loading != nullptr) {
    loading->setNext(newPeriodHolder);
  } else {
    playing = newPeriodHolder;
    reading = newPeriodHolder;
  }
  oldFrontPeriodUid = std::nullopt;
  loading = newPeriodHolder;
  length++;
  notifyQueueUpdate();
  return loading;
}

const std::shared_ptr<MediaPeriodHolder>& MediaPeriodQueue::advanceReadingPeriod() {
  if (reading) {
    reading = reading->getNext();
  }
  notifyQueueUpdate();
  return reading;
}

const std::shared_ptr<MediaPeriodHolder>& MediaPeriodQueue::advancePlayingPeriod() {
  if (playing == nullptr) {
    return playing;
  }
  if (playing == reading) {
    reading = playing->getNext();
  }
  playing->release();
  length--;
  if (length == 0) {
    loading = nullptr;
    oldFrontPeriodUid = playing->uid;
    oldFrontPeriodWindowSequenceNumber = playing->getInfo().id.windowSequenceNumber();
  }
  playing = playing->getNext();
  notifyQueueUpdate();
  return playing;
}

bool MediaPeriodQueue::removeAfter(const std::shared_ptr<MediaPeriodHolder>& mediaPeriodHolder) {
  assert(mediaPeriodHolder != nullptr);
  if (mediaPeriodHolder == loading) {
    return false;
  }
  auto periodHolder = mediaPeriodHolder;
  auto removedReading = false;
  loading = periodHolder;
  while (periodHolder->getNext() != nullptr) {
    periodHolder = periodHolder->getNext();
    if (periodHolder == reading) {
      reading = playing;
      removedReading = true;
    }
    periodHolder->release();
    length--;
  }
  loading->setNext(nullptr);
  notifyQueueUpdate();
  return removedReading;
}

void MediaPeriodQueue::clear() {
  if (length == 0) {
    return;
  }
  auto front = playing;
  oldFrontPeriodUid = front->uid;
  oldFrontPeriodWindowSequenceNumber = front->getInfo().id.windowSequenceNumber();
  while (front != nullptr) {
    front->release();
    front = front->getNext();
  }
  playing = nullptr;
  loading = nullptr;
  reading = nullptr;
  length = 0;
  notifyQueueUpdate();
}

bool MediaPeriodQueue::updateQueuedPeriods(const std::shared_ptr<Timeline>& timeline,
  int64_t rendererPositionUs,
  int64_t maxRendererReadPositionUs) {
  // TODO: Merge this into setTimeline so that the queue gets updated as soon as the new timeline
  // is set, once all cases handled by ExoPlayerImplInternal.handleSourceInfoRefreshed can be
  // handled here.
  std::shared_ptr<MediaPeriodHolder> previousPeriodHolder = nullptr;
  auto periodHolder = playing;
  while (periodHolder != nullptr) {
    auto oldPeriodInfo = periodHolder->getInfo();

    // Get period info based on new timeline.
    std::optional<MediaPeriodInfo> newPeriodInfo;
    if (previousPeriodHolder == nullptr) {
      // The id and start position of the first period have already been verified by
      // ExoPlayerImplInternal.handleSourceInfoRefreshed. Just update duration, isLastInTimeline
      // and isLastInPeriod flags.
      newPeriodInfo = getUpdatedMediaPeriodInfo(timeline, oldPeriodInfo);
    } else {
      newPeriodInfo =
        getFollowingMediaPeriodInfo(timeline, previousPeriodHolder, rendererPositionUs);
      if (newPeriodInfo == std::nullopt) {
        // We've loaded a next media period that is not in the new timeline.
        return !removeAfter(previousPeriodHolder);
      }
      if (!canKeepMediaPeriodHolder(oldPeriodInfo, newPeriodInfo.value())) {
        // The new media period has a different id or start position.
        return !removeAfter(previousPeriodHolder);
      }
    }

    // Use the new period info, but keep the old requested content position to avoid overriding it
    // by the default content position generated in getFollowingMediaPeriodInfo.
    periodHolder->setInfo(newPeriodInfo->copyWithRequestedContentPositionUs(
      oldPeriodInfo.requestedContentPositionUs));

    if (!areDurationsCompatible(oldPeriodInfo.durationUs, newPeriodInfo->durationUs)) {
      // The period duration changed. Remove all subsequent periods and check whether we read
      // beyond the new duration.
      auto newDurationInRendererTime =
        newPeriodInfo->durationUs == Definitions::kTimeUnset
          ? INT64_MAX
          : periodHolder->toRendererTime(newPeriodInfo->durationUs);
      auto isReadingAndReadBeyondNewDuration =
        periodHolder == reading
        && (
          maxRendererReadPositionUs == Definitions::kTimeEndOfSource
          || maxRendererReadPositionUs >= newDurationInRendererTime);
      auto readingPeriodRemoved = removeAfter(periodHolder);
      return !readingPeriodRemoved && !isReadingAndReadBeyondNewDuration;
    }

    previousPeriodHolder = periodHolder;
    periodHolder = periodHolder->getNext();
  }
  return true;
}

std::optional<MediaPeriodInfo>
MediaPeriodQueue::getUpdatedMediaPeriodInfo(const std::shared_ptr<Timeline>& timeline,
  const MediaPeriodInfo& info) {
  auto id = info.id;
  auto lastInPeriod = true;
  auto lastInWindow = isLastInWindow(timeline, id);
  auto lastInTimeline = isLastInTimeline(timeline, id, lastInPeriod);
  timeline->getPeriodByUid(info.id.periodUid(), period);
  auto durationUs = info.endPositionUs == Definitions::kTimeUnset
                    || info.endPositionUs == Definitions::kTimeEndOfSource
                      ? period->getDurationUs()
                      : info.endPositionUs;
  return MediaPeriodInfo(id,
    info.startPositionUs,
    info.requestedContentPositionUs,
    info.endPositionUs,
    durationUs,
    lastInPeriod,
    lastInWindow,
    lastInTimeline);
}

MediaPeriodId MediaPeriodQueue::resolveMediaPeriodId(
  const std::shared_ptr<Timeline>& timeline,
  const PeriodUid& periodUid,
  int64_t positionUs) {
  auto windowSequenceNumber = resolvePeriodIndexToWindowSequenceNumber(timeline, periodUid);
  return MediaPeriodId(periodUid, windowSequenceNumber);
}

void MediaPeriodQueue::notifyQueueUpdate() {

}

int64_t MediaPeriodQueue::resolvePeriodIndexToWindowSequenceNumber(
  const std::shared_ptr<Timeline>& timeline, const PeriodUid& periodUid) {
  auto windowIndex = timeline->getPeriodByUid(periodUid, period)->windowIndex;
  if (oldFrontPeriodUid != std::nullopt) {
    auto oldFrontPeriodIndex = timeline->getIndexOfPeriod(oldFrontPeriodUid);
    if (oldFrontPeriodIndex != Definitions::kIndexUnset) {
      auto oldFrontWindowIndex = timeline->getPeriod(oldFrontPeriodIndex, period)->windowIndex;
      if (oldFrontWindowIndex == windowIndex) {
        // Try to match old front uid after the queue has been cleared.
        return oldFrontPeriodWindowSequenceNumber;
      }
    }
  }
  auto mediaPeriodHolder = playing;
  while (mediaPeriodHolder != nullptr) {
    if (mediaPeriodHolder->uid == periodUid) {
      // Reuse window sequence number of first exact period match.
      return mediaPeriodHolder->info.id.windowSequenceNumber();
    }
    mediaPeriodHolder = mediaPeriodHolder->getNext();
  }
  mediaPeriodHolder = playing;
  while (mediaPeriodHolder != nullptr) {
    auto indexOfHolderInTimeline = timeline->getIndexOfPeriod(mediaPeriodHolder->uid);
    if (indexOfHolderInTimeline != Definitions::kIndexUnset) {
      auto holderWindowIndex = timeline->getPeriod(indexOfHolderInTimeline, period)->windowIndex;
      if (holderWindowIndex == windowIndex) {
        // As an alternative, try to match other periods of the same window.
        return mediaPeriodHolder->info.id.windowSequenceNumber();
      }
    }
    mediaPeriodHolder = mediaPeriodHolder->getNext();
  }
  // If no match is found, create new sequence number.
  long windowSequenceNumber = nextWindowSequenceNumber++;
  if (playing == nullptr) {
    // If the queue is empty, save it as old front uid to allow later reuse.
    oldFrontPeriodUid = periodUid;
    oldFrontPeriodWindowSequenceNumber = windowSequenceNumber;
  }
  return windowSequenceNumber;
}

bool MediaPeriodQueue::canKeepMediaPeriodHolder(const MediaPeriodInfo& oldInfo,
  const MediaPeriodInfo& newInfo) {
  return oldInfo.startPositionUs == newInfo.startPositionUs && oldInfo.id == newInfo.id;
}

bool MediaPeriodQueue::areDurationsCompatible(int64_t previousDurationUs, int64_t newDurationUs) {
  return previousDurationUs == Definitions::kTimeUnset || previousDurationUs == newDurationUs;
}

bool MediaPeriodQueue::updateForPlaybackModeChange(const std::shared_ptr<Timeline>& timeline) {
  // Find the last existing period holder that matches the new period order.
  auto lastValidPeriodHolder = playing;
  if (lastValidPeriodHolder == nullptr) {
    return true;
  }
  auto currentPeriodIndex = timeline->getIndexOfPeriod(lastValidPeriodHolder->uid);
  while (true) {
    auto nextPeriodIndex =
      timeline->getNextPeriodIndex(
        currentPeriodIndex, period, window, repeatMode, shuffleModeEnabled);
    while (lastValidPeriodHolder->getNext() != nullptr
           && !lastValidPeriodHolder->getInfo().isLastInTimelinePeriod) {
      lastValidPeriodHolder = lastValidPeriodHolder->getNext();
    }
    auto nextMediaPeriodHolder = lastValidPeriodHolder->getNext();
    if (nextPeriodIndex == Definitions::kIndexUnset || nextMediaPeriodHolder == nullptr) {
      break;
    }
    auto nextPeriodHolderPeriodIndex = timeline->getIndexOfPeriod(nextMediaPeriodHolder->uid);
    if (nextPeriodHolderPeriodIndex != nextPeriodIndex) {
      break;
    }
    lastValidPeriodHolder = nextMediaPeriodHolder;
    currentPeriodIndex = nextPeriodIndex;
  }

  // Release any period holders that don't match the new period order.
  auto readingPeriodRemoved = removeAfter(lastValidPeriodHolder);

  // Update the period info for the last holder, as it may now be the last period in the timeline.
  auto info = getUpdatedMediaPeriodInfo(timeline, lastValidPeriodHolder->getInfo());
  lastValidPeriodHolder->setInfo(info.value());

  // If renderers may have read from a period that's been removed, it is necessary to restart.
  return !readingPeriodRemoved;
}

std::optional<MediaPeriodInfo>
MediaPeriodQueue::getFirstMediaPeriodInfo(const PlaybackInfo& playbackInfo) {
  return getMediaPeriodInfo(playbackInfo.timeline,
    playbackInfo.periodId,
    playbackInfo.requestedContentPositionUs,
    playbackInfo.positionUs);
}

std::optional<MediaPeriodInfo>
MediaPeriodQueue::getFollowingMediaPeriodInfo(
  const std::shared_ptr<Timeline>& timeline,
  const std::shared_ptr<MediaPeriodHolder>& mediaPeriodHolder,
  int64_t rendererPositionUs) {
  // TODO: This method is called repeatedly from ExoPlayerImplInternal.maybeUpdateLoadingPeriod
  // but if the timeline is not ready to provide the next period it can't return a non-null value
  // until the timeline is updated. Store whether the next timeline period is ready when the
  // timeline is updated, to avoid repeatedly checking the same timeline.
  auto mediaPeriodInfo = mediaPeriodHolder->getInfo();
  // The expected delay until playback transitions to the new period is equal the duration of
  // media that's currently buffered (assuming no interruptions). This is used to project forward
  // the start position for transitions to new windows.
  auto bufferedDurationUs =
    mediaPeriodHolder->getRendererOffset() + mediaPeriodInfo.durationUs - rendererPositionUs;
  if (mediaPeriodInfo.isLastInTimelinePeriod) {
    auto currentPeriodIndex = timeline->getIndexOfPeriod(mediaPeriodInfo.id.periodUid());
    auto nextPeriodIndex =
      timeline->getNextPeriodIndex(
        currentPeriodIndex, period, window, repeatMode, shuffleModeEnabled);
    if (nextPeriodIndex == Definitions::kIndexUnset) {
      // We can't create a next period yet.
      return std::nullopt;
    }

    int64_t startPositionUs;
    int64_t contentPositionUs;
    auto nextWindowIndex =
      timeline->getPeriod(nextPeriodIndex, period, /* setIds= */ true)->windowIndex;
    auto nextPeriodUid = period->uid;
    auto windowSequenceNumber = mediaPeriodInfo.id.windowSequenceNumber();
    if (timeline->getWindow(nextWindowIndex, window)->firstPeriodIndex == nextPeriodIndex) {
      // We're starting to buffer a new window. When playback transitions to this window we'll
      // want it to be from its default start position, so project the default start position
      // forward by the duration of the buffer, and start buffering from this point.
      contentPositionUs = Definitions::kTimeUnset;
      auto defaultPosition =
        timeline->getPeriodPosition(
          window,
          period,
          nextWindowIndex,
          /* windowPositionUs= */ Definitions::kTimeUnset,
          /* defaultPositionProjectionUs= */ std::max(int64_t(0), bufferedDurationUs));
      if (defaultPosition == std::pair<PeriodUid, int64_t>()) {
        return std::nullopt;
      }
      nextPeriodUid = defaultPosition.first;
      startPositionUs = defaultPosition.second;
      auto nextMediaPeriodHolder = mediaPeriodHolder->getNext();
      if (nextMediaPeriodHolder != nullptr && nextMediaPeriodHolder->uid == nextPeriodUid) {
        windowSequenceNumber = nextMediaPeriodHolder->getInfo().id.windowSequenceNumber();
      } else {
        windowSequenceNumber = nextWindowSequenceNumber++;
      }
    } else {
      // We're starting to buffer a new period within the same window.
      startPositionUs = 0;
      contentPositionUs = 0;
    }
    auto periodId = MediaPeriodId(nextPeriodUid.value(), windowSequenceNumber);
    return getMediaPeriodInfo(timeline, periodId, contentPositionUs, startPositionUs);
  }

  auto currentPeriodId = mediaPeriodInfo.id;
  timeline->getPeriodByUid(currentPeriodId.periodUid(), period);

  // The next ad group can't be played. Play content from the previous end position instead.
  return getMediaPeriodInfoForContent(
    timeline,
    currentPeriodId.periodUid(),
    /* startPositionUs= */ mediaPeriodInfo.durationUs,
    /* requestedContentPositionUs= */ mediaPeriodInfo.durationUs,
    currentPeriodId.windowSequenceNumber());
}

std::optional<MediaPeriodInfo>
MediaPeriodQueue::getMediaPeriodInfo(const std::shared_ptr<Timeline>& timeline,
  const MediaPeriodId& id,
  int64_t requestedContentPositionUs,
  int64_t startPositionUs) {
  return getMediaPeriodInfoForContent(
    timeline,
    id.periodUid(),
    startPositionUs,
    requestedContentPositionUs,
    id.windowSequenceNumber());
}

std::optional<MediaPeriodInfo>
MediaPeriodQueue::getMediaPeriodInfoForContent(const std::shared_ptr<Timeline>& timeline,
  const PeriodUid& periodUid,
  int64_t startPositionUs,
  int64_t requestedContentPositionUs,
  int64_t windowSequenceNumber) {
  timeline->getPeriodByUid(periodUid, period);
  auto id = MediaPeriodId(periodUid, windowSequenceNumber);
  auto lastInPeriod = true;
  auto lastInWindow = isLastInWindow(timeline, id);
  auto lastInTimeline = isLastInTimeline(timeline, id, lastInPeriod);
  auto endPositionUs = Definitions::kTimeUnset;
  auto durationUs = period->durationUs;
  if (durationUs != Definitions::kTimeUnset && startPositionUs >= durationUs) {
    // Ensure start position doesn't exceed duration.
    startPositionUs = std::max(int64_t(0), durationUs - 1);
  }
  return MediaPeriodInfo(id,
    startPositionUs,
    requestedContentPositionUs,
    endPositionUs,
    durationUs,
    lastInPeriod,
    lastInWindow,
    lastInTimeline);
}

bool MediaPeriodQueue::isLastInWindow(const std::shared_ptr<Timeline>& timeline,
  const MediaPeriodId& id) {
  auto windowIndex = timeline->getPeriodByUid(id.periodUid(), period)->windowIndex;
  auto periodIndex = timeline->getIndexOfPeriod(id.periodUid());
  return timeline->getWindow(windowIndex, window)->lastPeriodIndex == periodIndex;
}

bool MediaPeriodQueue::isLastInTimeline(const std::shared_ptr<Timeline>& timeline,
  const MediaPeriodId& id,
  bool isLastMediaPeriodInPeriod) {
  auto periodIndex = timeline->getIndexOfPeriod(id.periodUid());
  auto windowIndex = timeline->getPeriod(periodIndex, period)->windowIndex;
  return !timeline->getWindow(windowIndex, window)->isDynamic
         && timeline->isLastPeriod(periodIndex, period, window, repeatMode, shuffleModeEnabled)
         && isLastMediaPeriodInPeriod;
}

} // namespace coral
