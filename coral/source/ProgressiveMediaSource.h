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

#ifndef CORAL_PLAYBACK_SOURCE_PROGRESSIVE_MEDIA_SOURCE_H
#define CORAL_PLAYBACK_SOURCE_PROGRESSIVE_MEDIA_SOURCE_H

#include <coral/source/BaseMediaSource.h>
#include <coral/source/ProgressiveMediaExtractor.h>
#include <coral/source/ProgressiveMediaPeriod.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace coral {

/**
 * Provides the media of a progressive container, i.e. of a single file whose samples are stored in
 * the order in which they are played.
 *
 * <p>This is the counterpart of ExoPlayer's {@code ProgressiveMediaSource}. It is a
 * {@link BaseMediaSource} with a single window and a single period: it reports a placeholder
 * timeline whose duration is not known yet while the media is prepared, and refreshes it when the
 * period reports the duration and the seekability that its extractor declared.
 *
 * <p>The heavy lifting happens in the {@link ProgressiveMediaPeriod} the source creates: this class
 * only owns the media item, the factories of the data source and of the extractor, and the timeline
 * that reflects the state of the period.
 */
class ProgressiveMediaSource final : public BaseMediaSource,
                                     public ProgressiveMediaPeriod::Listener {
private:
  /**
   * Creates the extractor of a period.
   *
   * <p>This is the counterpart of ExoPlayer's {@code ProgressiveMediaExtractor.Factory}: the
   * extractor keeps state, and it is initialized with the output of one period, therefore a new one
   * is created for every period.
   */
  using ExtractorFactory = std::function<std::shared_ptr<ProgressiveMediaExtractor>()>;

  std::shared_ptr<MediaItem> mediaItem;
  std::string inputUrl;
  ProgressiveMediaPeriod::DataSourceFactory dataSourceFactory;
  ExtractorFactory extractorFactory;
  std::shared_ptr<LoadErrorHandlingPolicy> loadErrorHandlingPolicy;

  /**
   * The placeholder timeline, built when the source is created because
   * {@link #getInitialTimeline()} can be called before it is prepared.
   */
  std::shared_ptr<Timeline> initialTimeline;

  bool timelineIsPlaceholder;
  int64_t timelineDurationUs;
  bool timelineIsSeekable;
  bool timelineIsLive;

  /**
   * The periods this source created, so that its destruction can release a period the caller did not.
   * A period that is still loading keeps its loadable alive, and the loadable calls back into this
   * source: releasing it before the source is destroyed is what keeps that callback off a dangling
   * listener.
   */
  std::vector<std::weak_ptr<MediaPeriod>> createdPeriods;

public:
  /**
   * @param mediaItem The media item whose url is read. Must not be null.
   * @param dataSourceFactory The factory of the data source, or null to read the media of the local
   *                          file system.
   * @param extractorFactory The factory of the extractor, or null to demux the container with the
   *                         extractors of the extractor module. Mainly useful to substitute a
   *                         platform extractor.
   */
  explicit ProgressiveMediaSource(const std::shared_ptr<MediaItem>& mediaItem,
      ProgressiveMediaPeriod::DataSourceFactory dataSourceFactory = nullptr,
      ExtractorFactory extractorFactory = nullptr) noexcept;

  ~ProgressiveMediaSource() override = default;

  [[nodiscard]] const std::shared_ptr<MediaItem>& getMediaItem() const override {
    return mediaItem;
  }

  /**
   * Returns the placeholder timeline, whose duration is not known yet.
   *
   * <p>Mirrors ExoPlayer's {@code ProgressiveMediaSource.getInitialTimeline}. A source that knows
   * the shape of its timeline before it is prepared lets a player report the window immediately, and
   * it is what keeps the masking source from renaming the period: the identifiers in the timeline are
   * the ones the media periods report.
   */
  [[nodiscard]] const std::shared_ptr<Timeline>& getInitialTimeline() const override {
    return initialTimeline;
  }

  /**
   * Sets the policy that decides how a failed load is retried.
   *
   * <p>Mirrors ExoPlayer's {@code ProgressiveMediaSource.Factory#setLoadErrorHandlingPolicy}. The
   * policy is read when a period is created, so it has to be set before the source is prepared.
   *
   * @param policy The policy to use, or null to keep the default one.
   */
  void setLoadErrorHandlingPolicy(const std::shared_ptr<LoadErrorHandlingPolicy>& policy) {
    if (policy != nullptr) {
      loadErrorHandlingPolicy = policy;
    }
  }

  std::shared_ptr<MediaPeriod> createPeriod(MediaPeriodId id,
      const std::shared_ptr<Allocator>& allocator,
      int64_t startPositionUs) override;

  void releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) override;

  void onSourceInfoRefreshed(int64_t durationUs, bool isSeekable, bool isLive) override;

protected:
  void prepareSourceInternal() override;

  void releaseSourceInternal() override;

private:
  /** Creates the factory of the extractors that the module supports. */
  static ExtractorFactory createDefaultExtractorFactory(const std::string& url);

  /** Builds the timeline that reflects the state the source is in. */
  [[nodiscard]] std::shared_ptr<Timeline> createTimeline() const;

  void notifySourceInfoRefreshed();
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_PROGRESSIVE_MEDIA_SOURCE_H
