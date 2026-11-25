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

#include <coral/source/AbstractConcatenatedTimeline.h>

#include <media/Definitions.h>

namespace coral {

std::optional<PeriodUid>
AbstractConcatenatedTimeline::getChildTimelineUidFromConcatenatedUid(
    const std::optional<PeriodUid>& concatenatedUid) {
  if (concatenatedUid == std::nullopt) {
    return std::nullopt;
  }
  return std::optional<PeriodUid>(concatenatedUid->id());
}

std::optional<PeriodUid>
AbstractConcatenatedTimeline::getChildPeriodUidFromConcatenatedUid(
    const std::optional<PeriodUid>& concatenatedUid) {
  if (concatenatedUid == std::nullopt) {
    return std::nullopt;
  }
  return std::optional<PeriodUid>(concatenatedUid->subId());
}

std::optional<PeriodUid>
AbstractConcatenatedTimeline::getConcatenatedUid(
    const std::optional<PeriodUid>& childTimelineUid,
    const std::optional<PeriodUid>& childPeriodOrWindowUid) {
  if (childTimelineUid == std::nullopt || childPeriodOrWindowUid == std::nullopt) {
    return std::nullopt;
  }
  auto firstUid = childTimelineUid->id();
  auto secondUid = childPeriodOrWindowUid->id();
  return PeriodUid(firstUid, secondUid);
}

AbstractConcatenatedTimeline::AbstractConcatenatedTimeline(
    bool isAtomic,
    const std::shared_ptr<ShuffleOrder>& shuffleOrder) noexcept
    : childCount(shuffleOrder->getLength()),
      shuffleOrder(shuffleOrder),
      isAtomic(isAtomic) {

}

int AbstractConcatenatedTimeline::getNextWindowIndex(int windowIndex,
    RepeatMode repeatMode,
    bool shuffleModeEnabled) {
  if (isAtomic) {
    // Adapt repeat and shuffle mode to atomic concatenation.
    repeatMode = repeatMode == RepeatMode::REPEAT_MODE_ONE
                 ? RepeatMode::REPEAT_MODE_ALL
                 : repeatMode;
    shuffleModeEnabled = false;
  }
  // Find next window within current child.
  auto childIndex = getChildIndexByWindowIndex(windowIndex);
  auto firstWindowIndexInChild = getFirstWindowIndexByChildIndex(childIndex);
  auto const& timeline = getTimelineByChildIndex(childIndex);
  auto nextWindowIndexInChild =
      timeline->getNextWindowIndex(
          windowIndex - firstWindowIndexInChild,
          repeatMode == RepeatMode::REPEAT_MODE_ALL
          ? RepeatMode::REPEAT_MODE_OFF
          : repeatMode,
          shuffleModeEnabled);
  if (nextWindowIndexInChild != Definitions::kIndexUnset) {
    return firstWindowIndexInChild + nextWindowIndexInChild;
  }
  // If not found, find first window of next non-empty child.
  auto nextChildIndex = getNextChildIndex(childIndex, shuffleModeEnabled);
  while (nextChildIndex != Definitions::kIndexUnset
         && getTimelineByChildIndex(nextChildIndex)->isEmpty()) {
    nextChildIndex = getNextChildIndex(nextChildIndex, shuffleModeEnabled);
  }
  if (nextChildIndex != Definitions::kIndexUnset) {
    return getFirstWindowIndexByChildIndex(nextChildIndex)
           + getTimelineByChildIndex(nextChildIndex)->getFirstWindowIndex(shuffleModeEnabled);
  }
  // If not found, this is the last window.
  if (repeatMode == RepeatMode::REPEAT_MODE_ALL) {
    return getFirstWindowIndex(shuffleModeEnabled);
  }
  return Definitions::kIndexUnset;
}

int AbstractConcatenatedTimeline::getPreviousWindowIndex(int windowIndex,
    RepeatMode repeatMode,
    bool shuffleModeEnabled) {
  if (isAtomic) {
    // Adapt repeat and shuffle mode to atomic concatenation.
    repeatMode = repeatMode == RepeatMode::REPEAT_MODE_ONE
                 ? RepeatMode::REPEAT_MODE_ALL
                 : repeatMode;
    shuffleModeEnabled = false;
  }
  // Find previous window within current child.
  auto childIndex = getChildIndexByWindowIndex(windowIndex);
  auto firstWindowIndexInChild = getFirstWindowIndexByChildIndex(childIndex);
  auto previousWindowIndexInChild =
      getTimelineByChildIndex(childIndex)
          ->getPreviousWindowIndex(
              windowIndex - firstWindowIndexInChild,
              repeatMode == RepeatMode::REPEAT_MODE_ALL
              ? RepeatMode::REPEAT_MODE_OFF
              : repeatMode,
              shuffleModeEnabled);
  if (previousWindowIndexInChild != Definitions::kIndexUnset) {
    return firstWindowIndexInChild + previousWindowIndexInChild;
  }
  // If not found, find last window of previous non-empty child.
  auto previousChildIndex = getPreviousChildIndex(childIndex, shuffleModeEnabled);
  while (previousChildIndex != Definitions::kIndexUnset
         && getTimelineByChildIndex(previousChildIndex)->isEmpty()) {
    previousChildIndex = getPreviousChildIndex(previousChildIndex, shuffleModeEnabled);
  }
  if (previousChildIndex != Definitions::kIndexUnset) {
    return getFirstWindowIndexByChildIndex(previousChildIndex)
           + getTimelineByChildIndex(previousChildIndex)->getLastWindowIndex(shuffleModeEnabled);
  }
  // If not found, this is the first window.
  if (repeatMode == RepeatMode::REPEAT_MODE_ALL) {
    return getLastWindowIndex(shuffleModeEnabled);
  }
  return Definitions::kIndexUnset;
}

int AbstractConcatenatedTimeline::getLastWindowIndex(bool shuffleModeEnabled) {
  if (childCount == 0) {
    return Definitions::kIndexUnset;
  }
  if (isAtomic) {
    shuffleModeEnabled = false;
  }
  // Find last non-empty child.
  auto lastChildIndex = shuffleModeEnabled ? shuffleOrder->getLastIndex() : childCount - 1;
  while (getTimelineByChildIndex(lastChildIndex)->isEmpty()) {
    lastChildIndex = getPreviousChildIndex(lastChildIndex, shuffleModeEnabled);
    if (lastChildIndex == Definitions::kIndexUnset) {
      // All children are empty.
      return Definitions::kIndexUnset;
    }
  }
  return getFirstWindowIndexByChildIndex(lastChildIndex)
         + getTimelineByChildIndex(lastChildIndex)->getLastWindowIndex(shuffleModeEnabled);
}

int AbstractConcatenatedTimeline::getFirstWindowIndex(bool shuffleModeEnabled) {
  if (childCount == 0) {
    return Definitions::kIndexUnset;
  }
  if (isAtomic) {
    shuffleModeEnabled = false;
  }
  // Find first non-empty child.
  auto firstChildIndex = shuffleModeEnabled ? shuffleOrder->getFirstIndex() : 0;
  while (getTimelineByChildIndex(firstChildIndex)->isEmpty()) {
    firstChildIndex = getNextChildIndex(firstChildIndex, shuffleModeEnabled);
    if (firstChildIndex == Definitions::kIndexUnset) {
      // All children are empty.
      return Definitions::kIndexUnset;
    }
  }
  return getFirstWindowIndexByChildIndex(firstChildIndex)
         + getTimelineByChildIndex(firstChildIndex)->getFirstWindowIndex(shuffleModeEnabled);
}

std::shared_ptr<Timeline::Window>
AbstractConcatenatedTimeline::getWindow(int windowIndex,
    const std::shared_ptr<Window>& window,
    int64_t defaultPositionProjectionUs) {
  auto childIndex = getChildIndexByWindowIndex(windowIndex);
  auto firstWindowIndexInChild = getFirstWindowIndexByChildIndex(childIndex);
  auto firstPeriodIndexInChild = getFirstPeriodIndexByChildIndex(childIndex);
  getTimelineByChildIndex(childIndex)
      ->getWindow(windowIndex - firstWindowIndexInChild, window, defaultPositionProjectionUs);
  auto childUid = getChildUidByChildIndex(childIndex);
  // Don't create new objects if the child is using SINGLE_WINDOW_UID.
  window->uid =
      Window::getSingleWindowUid() == window->uid
      ? childUid
      : getConcatenatedUid(childUid, window->uid);
  window->firstPeriodIndex += firstPeriodIndexInChild;
  window->lastPeriodIndex += firstPeriodIndexInChild;
  return window;
}

std::shared_ptr<Timeline::Period>
AbstractConcatenatedTimeline::getPeriodByUid(const std::optional<PeriodUid>& uid,
    const std::shared_ptr<Period>& period) {
  if (uid == std::nullopt) {
    return period;
  }
  auto childUid = getChildTimelineUidFromConcatenatedUid(uid);
  auto periodUid = getChildPeriodUidFromConcatenatedUid(uid);
  auto childIndex = getChildIndexByChildUid(childUid);
  auto firstWindowIndexInChild = getFirstWindowIndexByChildIndex(childIndex);
  getTimelineByChildIndex(childIndex)->getPeriodByUid(periodUid, period);
  period->windowIndex += firstWindowIndexInChild;
  period->uid = uid;
  return period;
}

std::shared_ptr<Timeline::Period>
AbstractConcatenatedTimeline::getPeriod(
    int periodIndex, const std::shared_ptr<Period>& period, bool setIds) {
  auto childIndex = getChildIndexByPeriodIndex(periodIndex);
  auto firstWindowIndexInChild = getFirstWindowIndexByChildIndex(childIndex);
  auto firstPeriodIndexInChild = getFirstPeriodIndexByChildIndex(childIndex);
  getTimelineByChildIndex(childIndex)
      ->getPeriod(periodIndex - firstPeriodIndexInChild, period, setIds);
  period->windowIndex += firstWindowIndexInChild;
  if (setIds) {
    auto firstPeriodUid = getChildUidByChildIndex(childIndex);
    period->uid = getConcatenatedUid(firstPeriodUid, period->uid);
  }
  return period;
}

int AbstractConcatenatedTimeline::getIndexOfPeriod(const std::optional<PeriodUid>& uid) {
  if (uid == std::nullopt) {
    return Definitions::kIndexUnset;
  }
  auto childUid = getChildTimelineUidFromConcatenatedUid(uid);
  auto periodUid = getChildPeriodUidFromConcatenatedUid(uid);
  auto childIndex = getChildIndexByChildUid(childUid);
  if (childIndex == Definitions::kIndexUnset) {
    return Definitions::kIndexUnset;
  }
  auto const& timeline = getTimelineByChildIndex(childIndex);
  auto periodIndexInChild = timeline->getIndexOfPeriod(periodUid);
  return periodIndexInChild == Definitions::kIndexUnset
         ? Definitions::kIndexUnset
         : getFirstPeriodIndexByChildIndex(childIndex) + periodIndexInChild;
}

std::optional<PeriodUid> AbstractConcatenatedTimeline::getUidOfPeriod(int periodIndex) {
  auto childIndex = getChildIndexByPeriodIndex(periodIndex);
  auto firstPeriodIndexInChild = getFirstPeriodIndexByChildIndex(childIndex);
  auto firstPeriodUid = getChildUidByChildIndex(childIndex);
  auto const& timeline = getTimelineByChildIndex(childIndex);
  auto periodUidInChild =
      timeline->getUidOfPeriod(periodIndex - firstPeriodIndexInChild);
  return getConcatenatedUid(firstPeriodUid, periodUidInChild);
}

int AbstractConcatenatedTimeline::getNextChildIndex(size_t childIndex, bool shuffleModeEnabled) {
  if (shuffleModeEnabled) {
    return shuffleOrder->getNextIndex(childIndex);
  } else {
    return childIndex < childCount - 1 ? childIndex + 1 : Definitions::kIndexUnset;
  }
}

int AbstractConcatenatedTimeline::getPreviousChildIndex(size_t childIndex,
    bool shuffleModeEnabled) {
  if (shuffleModeEnabled) {
    return shuffleOrder->getPreviousIndex(childIndex);
  } else {
    return childIndex > 0 ? childIndex - 1 : Definitions::kIndexUnset;
  }
}

} // namespace coral
