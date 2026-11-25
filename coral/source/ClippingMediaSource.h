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
#ifndef CORAL_PLAYBACK_SOURCE_CLIPPING_MEDIA_SOURCE_H
#define CORAL_PLAYBACK_SOURCE_CLIPPING_MEDIA_SOURCE_H

#include <coral/source/BaseMediaSource.h>
#include <coral/source/ClippingMediaPeriod.h>
#include <coral/source/ForwardingTimeline.h>

namespace coral {

class ClippingTimeline final : public ForwardingTimeline {
private:
  int64_t startUs;
  int64_t endUs;
  int64_t durationUs;
  bool isDynamic;

public:
  explicit ClippingTimeline(const std::shared_ptr<Timeline>& timeline,
      int64_t startUs,
      int64_t endUs) noexcept;

  std::shared_ptr<Window>
  getWindow(int windowIndex,
      const std::shared_ptr<Window>& window,
      int64_t defaultPositionProjectionUs) override;

  std::shared_ptr<Period>
  getPeriod(int periodIndex, const std::shared_ptr<Period>& period, bool setIds) override;
};

/**
 * {@link MediaSource} that wraps a source and clips its timeline based on specified start/end
 * positions. The wrapped source must consist of a single period.
 */
class ClippingMediaSource final : public BaseMediaSource {
public:
  /**
   * The source owns a caller that points back at it and hands it to the wrapped source, so a copy
   * would make one of the two objects release the other's caller.
   */
  ClippingMediaSource(const ClippingMediaSource& other) = delete;
  ClippingMediaSource& operator=(const ClippingMediaSource& other) = delete;
  ClippingMediaSource(ClippingMediaSource&& other) = delete;
  ClippingMediaSource& operator=(ClippingMediaSource&& other) = delete;

private:
  std::shared_ptr<MediaSource> mediaSource;
  std::shared_ptr<MediaSourceCaller> caller;
  int64_t startUs;
  int64_t endUs;
  bool enableInitialDiscontinuity;
  bool allowDynamicClippingUpdates;
  bool relativeToDefaultPosition;
  std::vector<std::shared_ptr<ClippingMediaPeriod>> mediaPeriods;
  std::shared_ptr<Timeline::Window> window;
  std::shared_ptr<ClippingTimeline> clippingTimeline;
  int64_t periodStartUs;
  int64_t periodEndUs;

public:
  explicit ClippingMediaSource(const std::shared_ptr<MediaSource>& mediaSource,
      int64_t startPositionUs,
      int64_t endPositionUs) noexcept;

  explicit ClippingMediaSource(const std::shared_ptr<MediaSource>& mediaSource,
      int64_t durationUs) noexcept;

  explicit ClippingMediaSource(const std::shared_ptr<MediaSource>& mediaSource,
      int64_t startPositionUs,
      int64_t endPositionUs,
      bool enableInitialDiscontinuity,
      bool allowDynamicClippingUpdates,
      bool relativeToDefaultPosition) noexcept;

  /**
   * Releases the wrapped source.
   *
   * <p>A source that is destroyed without {@link #releaseSource} being called must not leave the
   * caller it handed to the wrapped source behind: that caller points back at this object.
   */
  ~ClippingMediaSource() override;

  const std::shared_ptr<MediaItem>& getMediaItem() const override {
    return mediaSource->getMediaItem();
  }

  std::shared_ptr<MediaPeriod>
  createPeriod(MediaPeriodId id, const std::shared_ptr<Allocator>& allocator,
      int64_t startPositionUs) override;

  void releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) override;

protected:
  void releaseSourceInternal() override;

  void onChildSourceInfoRefreshed(const std::shared_ptr<MediaSource>& mediaSource,
      const std::shared_ptr<Timeline>& timeline);

  void prepareSourceInternal() override;

  int64_t getMediaTimeForChildMediaTime(int64_t mediaTimeMs) const;

private:
  void prepareChildSource();

  void refreshClippedTimeline(const std::shared_ptr<Timeline>& timeline);

  class MediaSourceCallerImpl final : public MediaSourceCaller {
  private:
    ClippingMediaSource* mediaSource;
  public:
    explicit MediaSourceCallerImpl(ClippingMediaSource* source) noexcept
        : mediaSource(source) {}

    void onSourceInfoRefreshed(const std::shared_ptr<MediaSource>& source,
        const std::shared_ptr<Timeline>& timeline) override;
  };
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_CLIPPING_MEDIA_SOURCE_H
