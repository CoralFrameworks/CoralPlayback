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
#ifndef CORAL_PLAYBACK_SOURCE_FORWARDING_TIMELINE_H
#define CORAL_PLAYBACK_SOURCE_FORWARDING_TIMELINE_H

#include <coral/source/Timeline.h>

namespace coral {

class ForwardingTimeline : public Timeline {
protected:
  std::shared_ptr<Timeline> timeline;

public:
  explicit ForwardingTimeline(const std::shared_ptr<Timeline>& timeline) noexcept
      : timeline(timeline) {}

  [[nodiscard]] const std::shared_ptr<Timeline>& getTimeline() const {
    return timeline;
  }

  [[nodiscard]] int getWindowCount() const override {
    return timeline->getWindowCount();
  }

  int getNextWindowIndex(int windowIndex,
      RepeatMode repeatMode,
      bool shuffleModeEnabled) override {
    return timeline->getNextWindowIndex(windowIndex, repeatMode, shuffleModeEnabled);
  }

  int getPreviousWindowIndex(int windowIndex,
      RepeatMode repeatMode,
      bool shuffleModeEnabled) override {
    return timeline->getPreviousWindowIndex(windowIndex, repeatMode, shuffleModeEnabled);
  }

  int getLastWindowIndex(bool shuffleModeEnabled) override {
    return timeline->getLastWindowIndex(shuffleModeEnabled);
  }

  int getFirstWindowIndex(bool shuffleModeEnabled) override {
    return timeline->getFirstWindowIndex(shuffleModeEnabled);
  }

  std::shared_ptr<Window> getWindow(int windowIndex,
      const std::shared_ptr<Window>& window,
      int64_t defaultPositionProjectionUs) override {
    return timeline->getWindow(windowIndex, window, defaultPositionProjectionUs);
  }

  [[nodiscard]] int getPeriodCount() const override {
    return timeline->getPeriodCount();
  }

  std::shared_ptr<Period>
  getPeriod(int periodIndex, const std::shared_ptr<Period>& period, bool setIds) override {
    return timeline->getPeriod(periodIndex, period, setIds);
  }

  int getIndexOfPeriod(const std::optional<PeriodUid>& uid) override {
    return timeline->getIndexOfPeriod(uid);
  }

  std::optional<PeriodUid> getUidOfPeriod(int periodIndex) override {
    return timeline->getUidOfPeriod(periodIndex);
  }
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_FORWARDING_TIMELINE_H
