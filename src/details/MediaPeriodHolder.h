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
#ifndef CORAL_PLAYBACK_DETAILS_MEDIA_PERIOD_HOLDER_H
#define CORAL_PLAYBACK_DETAILS_MEDIA_PERIOD_HOLDER_H

#include "MediaSourceList.h"

#include <coral/selection/TrackSelector.h>
#include <coral/source/MediaPeriod.h>
#include <coral/source/MediaPeriodInfo.h>
#include <coral/upstream/Allocator.h>

#include <memory>

namespace coral {

/** Holds a {@link MediaPeriod} with information required to play it. */
class MediaPeriodHolder final {
public:
  /** The unique period identifier the media period belongs to. */
  const PeriodUid uid;

  /** {@link MediaPeriodInfo} about this media period. */
  MediaPeriodInfo info;

private:
  /**
   * The default max cache duration in seconds
   */
  static constexpr int DEFAULT_MAX_CACHE_DURATION = 5;

  /**
   * The default min buffer duration in microsecond.
   */
  static constexpr int64_t DEFAULT_MIN_BUFFER_DURATION = 500000;

  /** The {@link MediaPeriod} wrapped by this class. */
  std::shared_ptr<MediaPeriod> mediaPeriod;
  /**
   * The sample streams for each renderer associated with this period. May contain null elements.
   */
  std::vector<std::shared_ptr<SampleStream>> sampleStreams;

  /** Whether {@link prepare(MediaPeriod::Callback, int64_t)} has been called. */
  bool prepareCalled;

  /** Whether the media period has finished preparing. */
  bool prepared;
  /** Set by release(); the holder must not select tracks or expose streams afterwards. */
  bool released = false;

  /** Whether any of the tracks of this media period are enabled. */
  bool enabledTracks;

  /**
   * Whether all required renderers have been enabled with the {@link #sampleStreams} for this
   * {@link mediaPeriod}. This means either {@link Renderer#enable(Configuration, Format[],
   * SampleStream, long, boolean, boolean, long)} or {@link Renderer#replaceStream(Format[],
   * SampleStream, long)} has been called.
   */
  bool allRenderersEnabled;

  std::vector<bool> mayRetainStreamFlags;
  std::vector<std::shared_ptr<SelectionCapabilities>> selectionCapabilities;
  std::shared_ptr<TrackSelector> trackSelector;
  std::shared_ptr<MediaSourceList> mediaSourceList;
  std::shared_ptr<MediaPeriodHolder> nextHolder;
  std::shared_ptr<TrackGroupArray> trackGroups;
  std::shared_ptr<TrackSelectorResult> trackSelectorResult;
  int64_t rendererPositionOffsetUs;

public:
  explicit MediaPeriodHolder(
    const std::vector<std::shared_ptr<SelectionCapabilities>>& selectionCapabilities,
    int64_t rendererPositionOffsetUs,
    const std::shared_ptr<TrackSelector>& trackSelector,
    const std::shared_ptr<Allocator>& allocator,
    const std::shared_ptr<MediaSourceList>& mediaSourceList,
    const MediaPeriodInfo& info,
    const std::shared_ptr<TrackSelectorResult>& emptyTrackSelectorResult) noexcept;

  virtual ~MediaPeriodHolder();

  MediaPeriodHolder(const MediaPeriodHolder& other) = delete;
  MediaPeriodHolder& operator=(const MediaPeriodHolder& other) = delete;

  /**
   * Converts time relative to the start of the period to the respective renderer time using {@link
   * #getRendererOffset()}, in microseconds.
   */
  [[nodiscard]] int64_t toRendererTime(int64_t periodTimeUs) const {
    return periodTimeUs + getRendererOffset();
  }

  /**
   * Converts renderer time to the respective time relative to the start of the period using {@link
   * #getRendererOffset()}, in microseconds.
   */
  [[nodiscard]] int64_t toPeriodTime(int64_t rendererTimeUs) const {
    return rendererTimeUs - getRendererOffset();
  }

  /** Returns the renderer time of the start of the period, in microseconds. */
  [[nodiscard]] int64_t getRendererOffset() const { return rendererPositionOffsetUs; }

  /**
   * Sets the renderer time of the start of the period, in microseconds.
   *
   * @param rendererPositionOffsetUs The new renderer position offset, in microseconds.
   */
  void setRendererOffset(int64_t rendererPositionOffsetUs);

  /** Returns start position of period in renderer time. */
  [[nodiscard]] int64_t getStartPositionRendererTime() const {
    return info.startPositionUs + rendererPositionOffsetUs;
  }

  /** Returns whether the period is fully buffered. */
  [[nodiscard]] bool isFullyBuffered() const;

  /**
   * Returns the buffered position in microseconds. If the period is buffered to the end, then the
   * period duration is returned.
   *
   * @return The buffered position in microseconds.
   */
  int64_t getBufferedPositionUs();

  /**
   * Returns the next load time relative to the start of the period, or {@link
   * Definitions::kTimeEndOfSource} if loading has finished.
   */
  int64_t getNextLoadPositionUs();

  /**
   * Handles period preparation.
   *
   * @param playbackSpeed The current factor by which playback is sped up.
   *
   * @param playWhenReady The current value of whether playback should proceed when ready.
   */
  void handlePrepared(
    float playbackSpeed, const std::shared_ptr<Timeline>& timeline, bool playWhenReady);

  /**
   * Reevaluates the buffer of the media period at the given renderer position. Should only be
   * called if this is the loading media period.
   *
   * @param rendererPositionUs The playing position in renderer time, in microseconds.
   */
  void reevaluateBuffer(int64_t rendererPositionUs);

  /**
   * Continues loading the media period with the given {@link LoadingInfo}. Should only be called if
   * this is the loading media period.
   *
   * @param loadingInfo The {@link LoadingInfo} about the current player state relevant to this load
   *     request.
   */
  void continueLoading(int64_t readingPositionUs);

  /**
   * Selects tracks for the period. Must only be called if {@link #prepared} is {@code true}.
   *
   * The new track selection needs to be applied with {@link
   * #applyTrackSelection(std::shared_ptr<TrackSelectorResult>, int64_t, bool)} before taking effect.
   *
   * @param playbackSpeed The current factor by which playback is sped up.
   * @param timeline The current {@link Timeline}.
   * @param playWhenReady The current value of whether playback should proceed when ready.
   * @return The {@link TrackSelectorResult}.
   */
  std::shared_ptr<TrackSelectorResult>
  selectTracks(float playbackSpeed,
    const std::shared_ptr<Timeline>& timeline,
    bool playWhenReady);

  /**
   * Applies a {@link TrackSelectorResult} to the period.
   *
   * @param newTrackSelectorResult The {@link TrackSelectorResult} to apply.
   * @param positionUs The position relative to the start of the period at which to apply the new
   *     track selections, in microseconds.
   * @param forceRecreateStreams Whether all streams are forced to be recreated.
   * @return The actual position relative to the start of the period at which the new track
   *     selections are applied.
   */
  int64_t applyTrackSelection(
    const std::shared_ptr<TrackSelectorResult>& newTrackSelectorResult,
    int64_t positionUs,
    bool forceRecreateStreams);

  /**
   * Applies a {@link TrackSelectorResult} to the period.
   *
   * @param newTrackSelectorResult The {@link TrackSelectorResult} to apply.
   * @param positionUs The position relative to the start of the period at which to apply the new
   *     track selections, in microseconds.
   * @param forceRecreateStreams Whether all streams are forced to be recreated.
   * @param streamResetFlags Will be populated to indicate which streams have been reset or were
   *     newly created.
   * @return The actual position relative to the start of the period at which the new track
   *     selections are applied.
   */
  int64_t applyTrackSelection(
    const std::shared_ptr<TrackSelectorResult>& newTrackSelectorResult,
    int64_t positionUs,
    bool forceRecreateStreams,
    std::vector<bool>& streamResetFlags);

  /** Releases the media period. No other method should be called after the release. */
  void release();

  /**
   * Sets the next media period holder in the queue.
   *
   * @param nextMediaPeriodHolder The next holder, or null if this will be the new loading media
   *     period holder at the end of the queue.
   */
  void setNext(const std::shared_ptr<MediaPeriodHolder>& nextMediaPeriodHolder);

  /**
   * Returns the next media period holder in the queue, or null if this is the last media period
   * (and thus the loading media period).
   */
  [[nodiscard]] const std::shared_ptr<MediaPeriodHolder>& getNext() const {
    return nextHolder;
  }

  /** Returns the {@link TrackGroupArray} exposed by this media period. */
  [[nodiscard]] const std::shared_ptr<TrackGroupArray>& getTrackGroups() const {
    return trackGroups;
  }

  /** Returns the {@link TrackSelectorResult} which is currently applied. */
  [[nodiscard]] const std::shared_ptr<TrackSelectorResult>&
  getTrackSelectorResult() const {
    return trackSelectorResult;
  }

  /**
   * Whether all required renderers have been enabled with the {@link #sampleStreams} for this
   * {@link #mediaPeriod}.
   */
  [[nodiscard]] bool isAllRenderersEnabled() const {
    return allRenderersEnabled;
  }

  void setAllRenderersEnabled() {
    allRenderersEnabled = true;
  }

  /** Updates the clipping to {@link MediaPeriodInfo#endPositionUs} if required. */
  void updateClipping();

  [[nodiscard]] bool canBeUsedForMediaPeriodInfo(const MediaPeriodInfo& info) const;

  void prepare(MediaPeriod::Callback* callback, int64_t startPositionUs);

  [[nodiscard]] bool hasEnabledTracks() const {
    return enabledTracks;
  }

  [[nodiscard]] int64_t getDurationUs() const {
    return info.durationUs;
  }

  /**
   * Returns the media period
   */
  [[nodiscard]] const std::shared_ptr<MediaPeriod>& getMediaPeriod() const {
    return mediaPeriod;
  }

  /**
   * Returns the {@link SampleStream}s
   */
  [[nodiscard]] bool isReleased() const noexcept { return released; }
  [[nodiscard]] const std::vector<std::shared_ptr<SampleStream>>& getSampleStreams() const {
    return sampleStreams;
  }

  [[nodiscard]] const std::shared_ptr<SampleStream>& getSampleStream(size_t index) const;

  void updateSampleStream(size_t index, const std::shared_ptr<SampleStream>& stream);

  [[nodiscard]] const MediaPeriodInfo& getInfo() const {
    return info;
  }

  void setInfo(const MediaPeriodInfo& newInfo) {
    this->info = newInfo;
  }

  [[nodiscard]] bool isPrepared() const {
    return prepared;
  }

private:
  void enableTrackSelectionsInResult();

  void disableTrackSelectionsInResult();

  void disassociateNoSampleRenderersWithEmptySampleStream(
    std::vector<std::shared_ptr<SampleStream>>& sampleStreams);

  void associateNoSampleRenderersWithEmptySampleStream(
    std::vector<std::shared_ptr<SampleStream>>& sampleStreams);

  [[nodiscard]] bool isLoadingMediaPeriod() const { return nextHolder == nullptr; }

  static std::shared_ptr<MediaPeriod>
  createMediaPeriod(const MediaPeriodId& id,
    const std::shared_ptr<MediaSourceList>& mediaSourceList,
    const std::shared_ptr<Allocator>& allocator,
    int64_t startPositionUs,
    int64_t endPositionUs);

  static void releaseMediaPeriod(std::shared_ptr<MediaSourceList>& mediaSourceList,
    std::shared_ptr<MediaPeriod>& mediaPeriod);

public:
  class Factory {
  public:
    virtual ~Factory() = default;

    virtual std::shared_ptr<MediaPeriodHolder>
    createMediaPeriodHolder(const MediaPeriodInfo& info,
      int64_t rendererPositionOffsetUs) = 0;
  };
};

} // namespace coral

#endif //CORAL_PLAYBACK_DETAILS_MEDIA_PERIOD_HOLDER_H
