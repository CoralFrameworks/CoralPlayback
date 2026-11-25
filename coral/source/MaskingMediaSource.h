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
#ifndef CORAL_PLAYBACK_SOURCE_MASKING_MEDIA_SOURCE_H
#define CORAL_PLAYBACK_SOURCE_MASKING_MEDIA_SOURCE_H

#include <coral/source/BaseMediaSource.h>
#include <coral/source/ForwardingTimeline.h>
#include <coral/source/MaskingMediaPeriod.h>

#include <memory>
#include <optional>

namespace coral {

/**
 * Timeline used as placeholder for an unprepared media source. After preparation, a
 * MaskingTimeline is used to keep the originally assigned masking period ID.
 */
class MaskingTimeline final : public ForwardingTimeline {
private:
  friend class MaskingMediaSource;

  std::optional<PeriodUid> replacedInternalWindowUid;
  std::optional<PeriodUid> replacedInternalPeriodUid;

public:
  static const PeriodUid& getMaskingExternalPeriodUid();

  static std::shared_ptr<MaskingTimeline>
  createWithPlaceholderTimeline(const std::shared_ptr<MediaItem>& mediaItem);

  static std::shared_ptr<MaskingTimeline>
  createWithRealTimeline(const std::shared_ptr<Timeline>& timeline,
      const std::optional<PeriodUid>& firstWindowUid,
      const std::optional<PeriodUid>& firstPeriodUid);

  explicit MaskingTimeline(const std::shared_ptr<Timeline>& timeline,
      const std::optional<PeriodUid>& replacedInternalWindowUid,
      const std::optional<PeriodUid>& replacedInternalPeriodUid) noexcept;

  std::shared_ptr<MaskingTimeline>
  cloneWithUpdatedTimeline(const std::shared_ptr<Timeline>& timeline);

  std::shared_ptr<Window> getWindow(int windowIndex,
      const std::shared_ptr<Window>& window,
      int64_t defaultPositionProjectionUs) override;

  std::shared_ptr<Period> getPeriod(int periodIndex,
      const std::shared_ptr<Period>& period,
      bool setIds) override;

  int getIndexOfPeriod(const std::optional<PeriodUid>& uid) override;

  std::optional<PeriodUid> getUidOfPeriod(int periodIndex) override;
};

/**
 * A {@link MediaSource} that masks the {@link Timeline} with a placeholder until the actual media
 * structure is known.
 */
class MaskingMediaSource final : public BaseMediaSource {
public:
  /**
   * The source owns a caller that points back at it and hands it to the wrapped source, so a copy
   * would make one of the two objects release the other's caller.
   */
  MaskingMediaSource(const MaskingMediaSource& other) = delete;
  MaskingMediaSource& operator=(const MaskingMediaSource& other) = delete;
  MaskingMediaSource(MaskingMediaSource&& other) = delete;
  MaskingMediaSource& operator=(MaskingMediaSource&& other) = delete;

private:
  std::shared_ptr<MediaSource> mediaSource;
  std::shared_ptr<MediaSourceCaller> caller;
  bool useLazyPreparation;
  std::shared_ptr<MaskingTimeline> timeline;
  std::shared_ptr<MaskingMediaPeriod> unpreparedMaskingMediaPeriod;
  bool hasStartedPreparing;
  bool isPrepared;
  bool hasRealTimeline;

  std::shared_ptr<Timeline::Window> window;
  std::shared_ptr<Timeline::Period> period;

  /** The handlers and the wrappers that were registered on the wrapped source. */
  std::vector<std::shared_ptr<EventListenerAndHandler>> childEventListeners;

public:
  explicit MaskingMediaSource(
      const std::shared_ptr<MediaSource>& mediaSource, bool useLazyPreparation) noexcept;

  /**
   * Releases the wrapped source and unregisters the listeners that were registered on it.
   *
   * <p>A source that is destroyed without {@link #releaseSource} being called must not leave
   * anything behind that points at it: the wrapper listeners hold a raw pointer to this object, and
   * the caller the wrapped source keeps would call into it.
   */
  ~MaskingMediaSource() override;

  /**
   * Returns the {@link Timeline}.
   *
   * Returns by value on purpose: the member is a {@link MaskingTimeline}, so a reference return type
   * would bind the reference to a temporary that is converted to a {@link Timeline} in the return
   * statement and destroyed before the caller can use it.
   */
  std::shared_ptr<Timeline> getTimeline() const {
    return timeline;
  }

  void prepareSourceInternal() override;

  const std::shared_ptr<MediaItem>& getMediaItem() const override {
    return mediaSource->getMediaItem();
  }

  std::shared_ptr<MediaPeriod>
  createPeriod(MediaPeriodId id, const std::shared_ptr<Allocator>& allocator,
      int64_t startPositionUs) override;

  void releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) override;

  /**
   * Registers a listener that sees the period identifiers of the masked timeline.
   *
   * <p>The wrapped source names its periods with internal identifiers, which the masking timeline
   * replaces with the identifier the rest of the player sees. The listener is therefore wrapped, and
   * the period identifiers of the events it receives are translated back to the external ones.
   */
  void addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
      const std::shared_ptr<MediaSourceEventListener>& eventListener) override;

  void removeEventListener(const std::shared_ptr<MediaSourceEventListener>& eventListener) override;

protected:
  void releaseSourceInternal() override;

  void onChildSourceInfoRefreshed(const std::shared_ptr<MediaSource>& mediaSource,
      const std::shared_ptr<Timeline>& timeline);

  PeriodUid getInternalPeriodUid(const PeriodUid& externalPeriodUid);

  PeriodUid getExternalPeriodUid(const PeriodUid& internalPeriodUid);

  void setPreparePositionOverrideToUnpreparedMaskingPeriod(int64_t preparePositionOverrideUs);

private:
  void prepareChildSource();

  static const char* getName() { return "MaskingMediaSource"; }

  class MediaSourceCallerImpl final : public MediaSourceCaller {
  private:
    MaskingMediaSource* mediaSource;
  public:
    explicit MediaSourceCallerImpl(MaskingMediaSource* source) noexcept
        : mediaSource(source) {}

    void onSourceInfoRefreshed(const std::shared_ptr<MediaSource>& source,
        const std::shared_ptr<Timeline>& timeline) override;
  };

  /**
   * Translates the internal period identifiers of the wrapped source into the external ones.
   */
  class MaskingEventListener final : public MediaSourceEventListener {
  private:
    MaskingMediaSource* mediaSource;
    std::shared_ptr<MediaSourceEventListener> target;

  public:
    MaskingEventListener(MaskingMediaSource* mediaSource,
        std::shared_ptr<MediaSourceEventListener> target) noexcept
        : mediaSource(mediaSource),
          target(std::move(target)) {
    }

    void onMediaPeriodCreated(int windowIndex,
        const std::optional<MediaPeriodId>& mediaPeriodId) override;

    void onMediaPeriodReleased(int windowIndex,
        const std::optional<MediaPeriodId>& mediaPeriodId) override;

    void onLoadStarted(int windowIndex,
        const std::optional<MediaPeriodId>& mediaPeriodId,
        const LoadEventInfo& loadEventInfo,
        const MediaLoadData& mediaLoadData) override;

    void onLoadCompleted(int windowIndex,
        const std::optional<MediaPeriodId>& mediaPeriodId,
        const LoadEventInfo& loadEventInfo,
        const MediaLoadData& mediaLoadData) override;

    void onLoadCanceled(int windowIndex,
        const std::optional<MediaPeriodId>& mediaPeriodId,
        const LoadEventInfo& loadEventInfo,
        const MediaLoadData& mediaLoadData) override;

    void onLoadError(int windowIndex,
        const std::optional<MediaPeriodId>& mediaPeriodId,
        const LoadEventInfo& loadEventInfo,
        const MediaLoadData& mediaLoadData,
        const Error* error,
        bool wasCanceled) override;

    void onUpstreamDiscarded(int windowIndex,
        const MediaPeriodId& mediaPeriodId,
        const MediaLoadData& mediaLoadData) override;

    void onDownstreamFormatChanged(int windowIndex,
        const std::optional<MediaPeriodId>& mediaPeriodId,
        const MediaLoadData& mediaLoadData) override;

    [[nodiscard]] const std::shared_ptr<MediaSourceEventListener>& getTarget() const {
      return target;
    }

  private:
    /** Returns the identifier of the period as the masked timeline names it. */
    [[nodiscard]] std::optional<MediaPeriodId> toExternalMediaPeriodId(
        const std::optional<MediaPeriodId>& internalMediaPeriodId) const;
  };
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MASKING_MEDIA_SOURCE_H
