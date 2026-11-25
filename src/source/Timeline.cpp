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

#include <coral/source/Timeline.h>

#include "UidGenerator.h"

#include <cassert>

namespace coral {

const PeriodUid& Timeline::Window::getSingleWindowUid() {
  static const auto sUid = PeriodUid(newUidString());
  return sUid;
}

Timeline::Window::Window()
    : uid(std::nullopt),
      presentationStartTimeMs(Definitions::kTimeUnset),
      windowStartTimeMs(Definitions::kTimeUnset),
      elapsedRealtimeEpochOffsetMs(Definitions::kTimeUnset),
      isSeekable(false),
      isDynamic(false),
      isLive(false),
      isPlaceholder(false),
      firstPeriodIndex(0),
      lastPeriodIndex(0),
      defaultPositionUs(Definitions::kTimeUnset),
      durationUs(Definitions::kTimeUnset),
      positionInFirstPeriodUs(0) {}

void Timeline::Window::set(const std::optional<PeriodUid>& newUid,
    const std::shared_ptr<MediaItem>& newMediaItem,
    int64_t newPresentationStartTimeMs,
    int64_t newWindowStartTimeMs,
    int64_t newElapsedRealtimeEpochOffsetMs,
    bool newSeekable,
    bool newDynamic,
    bool newLive,
    int64_t newDefaultPositionUs,
    int64_t newDurationUs,
    int newFirstPeriodIndex,
    int newLastPeriodIndex,
    int64_t newPositionInFirstPeriodUs) {
  this->uid = newUid;
  this->mediaItem = newMediaItem;
  this->presentationStartTimeMs = newPresentationStartTimeMs;
  this->windowStartTimeMs = newWindowStartTimeMs;
  this->elapsedRealtimeEpochOffsetMs = newElapsedRealtimeEpochOffsetMs;
  this->isSeekable = newSeekable;
  this->isDynamic = newDynamic;
  this->isLive = newLive;
  this->defaultPositionUs = newDefaultPositionUs;
  this->durationUs = newDurationUs;
  this->firstPeriodIndex = newFirstPeriodIndex;
  this->lastPeriodIndex = newLastPeriodIndex;
  this->positionInFirstPeriodUs = newPositionInFirstPeriodUs;
  this->isPlaceholder = false;
}

//--------------------------------------------------------------------------------------------------

Timeline::Period::Period()
    : id(std::nullopt),
      uid(std::nullopt),
      windowIndex(Definitions::kIndexUnset),
      durationUs(Definitions::kTimeUnset),
      positionInWindowUs(Definitions::kTimeUnset) {}

//--------------------------------------------------------------------------------------------------

int Timeline::getNextWindowIndex(
    int windowIndex, RepeatMode repeatMode, bool shuffleModeEnabled) {
  switch (repeatMode) {
    case RepeatMode::REPEAT_MODE_OFF: {
      return windowIndex == getLastWindowIndex(shuffleModeEnabled)
             ? Definitions::kIndexUnset
             : windowIndex + 1;
    }
    case RepeatMode::REPEAT_MODE_ONE: {
      return windowIndex;
    }
    case RepeatMode::REPEAT_MODE_ALL: {
      return windowIndex == getLastWindowIndex(shuffleModeEnabled)
             ? getFirstWindowIndex(shuffleModeEnabled)
             : windowIndex + 1;
    }
  }
}

int Timeline::getPreviousWindowIndex(
    int windowIndex, RepeatMode repeatMode, bool shuffleModeEnabled) {
  switch (repeatMode) {
    case RepeatMode::REPEAT_MODE_OFF: {
      return windowIndex == getFirstWindowIndex(shuffleModeEnabled)
             ? Definitions::kIndexUnset
             : windowIndex - 1;
    }
    case RepeatMode::REPEAT_MODE_ONE: {
      return windowIndex;
    }
    case RepeatMode::REPEAT_MODE_ALL: {
      return windowIndex == getFirstWindowIndex(shuffleModeEnabled)
             ? getLastWindowIndex(shuffleModeEnabled)
             : windowIndex - 1;
    }
  }
}

int Timeline::getLastWindowIndex(bool shuffleModeEnabled) {
  return isEmpty() ? Definitions::kIndexUnset : getWindowCount() - 1;
}

int Timeline::getFirstWindowIndex(bool shuffleModeEnabled) {
  return isEmpty() ? Definitions::kIndexUnset : 0;
}

int Timeline::getNextPeriodIndex(int periodIndex,
    const std::shared_ptr<Period>& period,
    const std::shared_ptr<Window>& window,
    RepeatMode repeatMode,
    bool shuffleModeEnabled) {
  int windowIndex = getPeriod(periodIndex, period)->windowIndex;
  if (getWindow(windowIndex, window)->lastPeriodIndex == periodIndex) {
    int nextWindowIndex = getNextWindowIndex(windowIndex, repeatMode, shuffleModeEnabled);
    if (nextWindowIndex == Definitions::kIndexUnset) {
      return Definitions::kIndexUnset;
    }
    return getWindow(nextWindowIndex, window)->firstPeriodIndex;
  }
  return periodIndex + 1;
}

bool Timeline::isLastPeriod(int periodIndex,
    const std::shared_ptr<Period>& period,
    const std::shared_ptr<Window>& window,
    RepeatMode repeatMode,
    bool shuffleModeEnabled) {
  return getNextPeriodIndex(periodIndex, period, window, repeatMode, shuffleModeEnabled)
         == Definitions::kIndexUnset;
}

std::pair<PeriodUid, int64_t>
Timeline::getPeriodPosition(const std::shared_ptr<Window>& window,
    const std::shared_ptr<Period>& period,
    int windowIndex,
    int64_t windowPositionUs,
    int64_t defaultPositionProjectionUs) {
  assert(windowIndex >= 0 && windowIndex <= getWindowCount());
  getWindow(windowIndex, window, defaultPositionProjectionUs);
  if (windowPositionUs == Definitions::kTimeUnset) {
    windowPositionUs = window->getDefaultPositionUs();
    if (windowPositionUs == Definitions::kTimeUnset) {
      return {};
    }
  }
  auto periodIndex = window->firstPeriodIndex;
  auto periodPositionUs = window->getPositionInFirstPeriodUs() + windowPositionUs;
  auto periodDurationUs = getPeriod(periodIndex, period, /* setIds= */ true)->getDurationUs();
  while (periodDurationUs != Definitions::kTimeUnset && periodPositionUs >= periodDurationUs
         && periodIndex < window->lastPeriodIndex) {
    periodPositionUs -= periodDurationUs;
    periodDurationUs = getPeriod(++periodIndex, period, /* setIds= */ true)->getDurationUs();
  }
  return std::make_pair(period->uid.value(), periodPositionUs);
}

} // namespace coral
