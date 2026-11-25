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

#include <coral/source/MergingMediaSource.h>

#include <coral/source/MergingMediaPeriod.h>

#include <utility>

namespace coral {

class MergingMediaSource::CallerBridge final : public MediaSourceCaller {
public:
  explicit CallerBridge(std::shared_ptr<MergingMediaSource> owner) : owner(std::move(owner)) {}

  void onSourceInfoRefreshed(const std::shared_ptr<MediaSource>& refreshed,
      const std::shared_ptr<Timeline>& timeline) override {
    if (auto ownerSource = owner.lock()) {
      if (!ownerSource->sources.empty() && refreshed == ownerSource->sources.front()) {
        ownerSource->firstTimeline = timeline;
      }
      ++refreshedCount;
      if (refreshedCount >= static_cast<int>(ownerSource->sources.size()) &&
          ownerSource->caller != nullptr) {
        ownerSource->caller->onSourceInfoRefreshed(ownerSource->shared_from_this(),
          ownerSource->firstTimeline);
      }
    }
  }

private:
  std::weak_ptr<MergingMediaSource> owner;
  int refreshedCount = 0;
};

MergingMediaSource::MergingMediaSource(std::vector<std::shared_ptr<MediaSource>> sources,
    std::vector<SubtitleMetadata> metadata)
    : sources(std::move(sources)), metadata(std::move(metadata)) {
}

const std::shared_ptr<Timeline>& MergingMediaSource::getInitialTimeline() const {
  static const std::shared_ptr<Timeline> kEmpty = nullptr;
  return sources.empty() ? kEmpty : sources.front()->getInitialTimeline();
}

bool MergingMediaSource::isSingleWindow() {
  for (const auto& source: sources) {
    if (!source->isSingleWindow()) return false;
  }
  return true;
}

const std::shared_ptr<MediaItem>& MergingMediaSource::getMediaItem() const {
  static const std::shared_ptr<MediaItem> kEmpty = nullptr;
  return sources.empty() ? kEmpty : sources.front()->getMediaItem();
}

void MergingMediaSource::prepareSource(const std::shared_ptr<MediaSourceCaller>& caller) {
  this->caller = caller;
  bridge = std::make_shared<CallerBridge>(shared_from_this());
  for (const auto& source: sources) source->prepareSource(bridge);
}

void MergingMediaSource::enable(const std::shared_ptr<MediaSourceCaller>& caller) {
  for (const auto& source: sources) source->enable(caller);
}

std::shared_ptr<MediaPeriod> MergingMediaSource::createPeriod(MediaPeriodId id,
    const std::shared_ptr<Allocator>& allocator, int64_t startPositionUs) {
  std::vector<std::shared_ptr<MediaPeriod>> periods;
  for (const auto& source: sources) {
    periods.push_back(source->createPeriod(id, allocator, startPositionUs));
  }
  return std::make_shared<MergingMediaPeriod>(std::move(periods), nullptr, metadata);
}

void MergingMediaSource::releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) {
  auto merged = std::dynamic_pointer_cast<MergingMediaPeriod>(mediaPeriod);
  if (merged == nullptr) return;
  for (size_t i = 0; i < sources.size(); ++i) {
    sources[i]->releasePeriod(merged->periods[i]);
  }
}

void MergingMediaSource::disable(const std::shared_ptr<MediaSourceCaller>& caller) {
  for (const auto& source: sources) source->disable(caller);
}

void MergingMediaSource::releaseSource(const std::shared_ptr<MediaSourceCaller>& caller) {
  for (const auto& source: sources) source->releaseSource(bridge != nullptr ? bridge : caller);
  callers.clear();
  this->caller = nullptr;
  bridge.reset();
}

void MergingMediaSource::addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
    const std::shared_ptr<MediaSourceEventListener>& eventListener) {
  for (const auto& source: sources) source->addEventListener(eventHandler, eventListener);
}

void MergingMediaSource::removeEventListener(
    const std::shared_ptr<MediaSourceEventListener>& eventListener) {
  for (const auto& source: sources) source->removeEventListener(eventListener);
}

} // namespace coral