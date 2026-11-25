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
#ifndef CORAL_PLAYBACK_SOURCE_ABSTRACT_CONCATENATED_TIMELINE_H
#define CORAL_PLAYBACK_SOURCE_ABSTRACT_CONCATENATED_TIMELINE_H

#include <coral/source/ShuffleOrder.h>
#include <coral/source/Timeline.h>

#include <memory>
#include <optional>

namespace coral {

/** Abstract base class for the concatenation of one or more {@link Timeline}s. */
class AbstractConcatenatedTimeline : public Timeline {
private:
  int childCount;
  std::shared_ptr<ShuffleOrder> shuffleOrder;
  bool isAtomic;

public:
  /**
   * Returns UID of child timeline from a concatenated period UID.
   *
   * @param concatenatedUid UID of a period in a concatenated timeline.
   * @return UID of the child timeline this period belongs to.
   */
  static std::optional<PeriodUid>
  getChildTimelineUidFromConcatenatedUid(const std::optional<PeriodUid>& concatenatedUid);

  /**
   * Returns UID of the period in the child timeline from a concatenated period UID.
   *
   * @param concatenatedUid UID of a period in a concatenated timeline.
   * @return UID of the period in the child timeline.
   */
  static std::optional<PeriodUid>
  getChildPeriodUidFromConcatenatedUid(const std::optional<PeriodUid>& concatenatedUid);

  /**
   * Returns a concatenated UID for a period or window in a child timeline.
   *
   * @param childTimelineUid UID of the child timeline this period or window belongs to.
   * @param childPeriodOrWindowUid UID of the period or window in the child timeline.
   * @return UID of the period or window in the concatenated timeline.
   */
  static std::optional<PeriodUid>
  getConcatenatedUid(const std::optional<PeriodUid>& childTimelineUid,
      const std::optional<PeriodUid>& childPeriodOrWindowUid);

  explicit AbstractConcatenatedTimeline(
      bool isAtomic, const std::shared_ptr<ShuffleOrder>& shuffleOrder) noexcept;

  int getNextWindowIndex(
      int windowIndex, RepeatMode repeatMode, bool shuffleModeEnabled) override;

  int getPreviousWindowIndex(
      int windowIndex, RepeatMode repeatMode, bool shuffleModeEnabled) override;

  int getLastWindowIndex(bool shuffleModeEnabled) override;

  int getFirstWindowIndex(bool shuffleModeEnabled) override;

  std::shared_ptr<Window>
  getWindow(int windowIndex,
      const std::shared_ptr<Window>& window,
      int64_t defaultPositionProjectionUs) override;

  std::shared_ptr<Period>
  getPeriodByUid(const std::optional<PeriodUid>& periodUid,
      const std::shared_ptr<Period>& period) override;

  std::shared_ptr<Period>
  getPeriod(int periodIndex, const std::shared_ptr<Period>& period, bool setIds) override;

  int getIndexOfPeriod(const std::optional<PeriodUid>& uid) override;

  std::optional<PeriodUid> getUidOfPeriod(int periodIndex) override;

protected:
  /**
   * Returns the index of the child timeline containing the given period index.
   *
   * @param periodIndex A valid period index within the bounds of the timeline.
   */
  virtual int getChildIndexByPeriodIndex(int periodIndex) = 0;

  /**
   * Returns the index of the child timeline containing the given window index.
   *
   * @param windowIndex A valid window index within the bounds of the timeline.
   */
  virtual int getChildIndexByWindowIndex(int windowIndex) = 0;

  /**
   * Returns the index of the child timeline with the given UID or {@link Definitions::kIndexUnset}
   * if not found.
   *
   * @param childUid A child UID.
   * @return Index of child timeline or {@link Definitions::kIndexUnset} if UID was not found.
   */
  virtual int getChildIndexByChildUid(const std::optional<PeriodUid>& childUid) = 0;

  /**
   * Returns the child timeline for the child with the given index.
   *
   * @param childIndex A valid child index within the bounds of the timeline.
   */
  virtual const std::shared_ptr<Timeline>& getTimelineByChildIndex(size_t childIndex) const = 0;

  /**
   * Returns the first period index belonging to the child timeline with the given index.
   *
   * @param childIndex A valid child index within the bounds of the timeline.
   */
  virtual int getFirstPeriodIndexByChildIndex(size_t childIndex) = 0;

  /**
   * Returns the first window index belonging to the child timeline with the given index.
   *
   * @param childIndex A valid child index within the bounds of the timeline.
   */
  virtual int getFirstWindowIndexByChildIndex(size_t childIndex) = 0;

  /**
   * Returns the UID of the child timeline with the given index.
   *
   * @param childIndex A valid child index within the bounds of the timeline.
   */
  virtual std::optional<PeriodUid> getChildUidByChildIndex(size_t childIndex) const = 0;

private:
  int getNextChildIndex(size_t childIndex, bool shuffleModeEnabled);

  int getPreviousChildIndex(size_t childIndex, bool shuffleModeEnabled);
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_ABSTRACT_CONCATENATED_TIMELINE_H
