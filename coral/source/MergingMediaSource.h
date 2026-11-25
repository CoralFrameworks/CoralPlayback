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
#ifndef CORAL_PLAYBACK_SOURCE_MERGING_MEDIA_SOURCE_H
#define CORAL_PLAYBACK_SOURCE_MERGING_MEDIA_SOURCE_H

#include <coral/source/MediaSource.h>

#include <memory>
#include <vector>

namespace coral {

/**
 * A media source that exposes the tracks of every child source as one period.
 *
 * <p>The minimal single-period form: all children must be single-window sources (video plus one
 * side-loaded subtitle file). The first child is the timeline owner; the others contribute their
 * track groups and sample streams to the merged period.
 */
/** Optional metadata written into the formats of a side-loaded subtitle source. */
struct SubtitleMetadata final {
  bool present = false;
  std::string mimeType;
  std::string language;
  std::string label;
  std::string id;
};

class MergingMediaSource final : public MediaSource,
                                 public std::enable_shared_from_this<MergingMediaSource> {
public:
  explicit MergingMediaSource(std::vector<std::shared_ptr<MediaSource>> sources,
      std::vector<SubtitleMetadata> metadata = {});

  ~MergingMediaSource() override = default;

  [[nodiscard]] const std::shared_ptr<Timeline>& getInitialTimeline() const override;
  [[nodiscard]] bool isSingleWindow() override;
  [[nodiscard]] const std::shared_ptr<MediaItem>& getMediaItem() const override;

  void prepareSource(const std::shared_ptr<MediaSourceCaller>& caller) override;
  void enable(const std::shared_ptr<MediaSourceCaller>& caller) override;
  std::shared_ptr<MediaPeriod> createPeriod(MediaPeriodId id,
      const std::shared_ptr<Allocator>& allocator, int64_t startPositionUs) override;
  void releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) override;
  void disable(const std::shared_ptr<MediaSourceCaller>& caller) override;
  void releaseSource(const std::shared_ptr<MediaSourceCaller>& caller) override;

  void addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
      const std::shared_ptr<MediaSourceEventListener>& eventListener) override;
  void removeEventListener(
      const std::shared_ptr<MediaSourceEventListener>& eventListener) override;

private:
  class CallerBridge;

  std::vector<std::shared_ptr<MediaSource>> sources;
  std::vector<SubtitleMetadata> metadata;
  std::vector<std::shared_ptr<MediaSourceCaller>> callers;
  std::shared_ptr<MediaSourceCaller> bridge;
  std::shared_ptr<MediaSourceCaller> caller;
  std::shared_ptr<Timeline> firstTimeline;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MERGING_MEDIA_SOURCE_H
