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
#ifndef CORAL_PLAYBACK_DETAILS_MEDIA_SOURCE_LIST_H
#define CORAL_PLAYBACK_DETAILS_MEDIA_SOURCE_LIST_H

#include <coral/source/MaskingMediaSource.h>
#include <coral/source/MediaPeriodId.h>
#include <coral/source/MediaSourceEventListener.h>
#include <coral/source/ShuffleOrder.h>
#include <coral/source/Timeline.h>
#include <coral/upstream/Allocator.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace coral {

/**
 * Data class to hold playlist media sources together with meta data needed to process them.
 */
class MediaSourceHolder final {
  friend class MediaSourceList;

public:
  std::shared_ptr<MaskingMediaSource> mediaSource;
  PeriodUid uid;
  std::vector<MediaPeriodId> activeMediaPeriodIds;

  int firstWindowIndexInChild;
  bool isRemoved;

  explicit MediaSourceHolder(const std::shared_ptr<MediaSource>& mediaSource, bool useLazyPreparation) noexcept;

  ~MediaSourceHolder();

  void reset(int firstWindowIndexInChild);

  [[nodiscard]] const PeriodUid& getUid() const {
    return uid;
  }

  /**
   * Returns the timeline of the source. Returned by value because the source converts its own
   * timeline type, see {@link MaskingMediaSource::getTimeline()}.
   */
  [[nodiscard]] std::shared_ptr<Timeline> getTimeline() const {
    return mediaSource->getTimeline();
  }
};

/** Listener for source events. */
class MediaSourceListInfoRefreshListener {
public:
  virtual ~MediaSourceListInfoRefreshListener() = default;
  /**
   * Called when the timeline of a media item has changed and a new timeline that reflects the
   * current playlist state needs to be created by calling {@link createTimeline()}.
   */
  virtual void onPlaylistUpdateRequested() = 0;
};

/**
 * Concatenates multiple {@link MediaSource}s. The list of {@link MediaSource}s can be modified
 * during playback. It is valid for the same {@link MediaSource} instance to be present more than
 * once in the playlist.
 */
class MediaSourceList final {
public:
  /**
   * The list owns its child sources through callers that point back at it, so a copy would leave
   * both copies releasing the same sources and would make one of the back pointers ambiguous.
   */
  MediaSourceList(const MediaSourceList& other) = delete;
  MediaSourceList& operator=(const MediaSourceList& other) = delete;
  MediaSourceList(MediaSourceList&& other) = delete;
  MediaSourceList& operator=(MediaSourceList&& other) = delete;

private:
  struct MediaPeriodAndSourceHolder {
    std::shared_ptr<MediaPeriod> mediaPeriod;
    std::shared_ptr<MediaSourceHolder> mediaSourceHolder;

    explicit MediaPeriodAndSourceHolder(
      const std::shared_ptr<MediaPeriod>& mediaPeriod,
      const std::shared_ptr<MediaSourceHolder>& mediaSourceHolder) noexcept
      : mediaPeriod(mediaPeriod),
        mediaSourceHolder(mediaSourceHolder) {
    }
  };

  /**
   * One listener of the playlist, registered on a child source through a forwarding listener.
   */
  struct ChildEventListener {
    const std::shared_ptr<EventHandler> eventHandler;
    /** The listener of the playlist, which receives the events of the child source. */
    const std::shared_ptr<MediaSourceEventListener> target;
    /** The listener that was registered on the child source. */
    const std::shared_ptr<MediaSourceEventListener> forwarder;

    explicit ChildEventListener(std::shared_ptr<EventHandler> eventHandler,
      std::shared_ptr<MediaSourceEventListener> target,
      std::shared_ptr<MediaSourceEventListener> forwarder) noexcept
      : eventHandler(std::move(eventHandler)),
        target(std::move(target)),
        forwarder(std::move(forwarder)) {
    }
  };

  struct MediaSourceHolderAndListener {
    const std::shared_ptr<MediaSourceHolder> holder;
    const std::shared_ptr<MediaSource> mediaSource;
    const std::shared_ptr<MediaSourceCaller> caller;

    /**
     * The forwarding listeners registered on the child source, one per listener of the playlist.
     */
    std::vector<std::shared_ptr<ChildEventListener>> eventListeners;

    explicit MediaSourceHolderAndListener(
      const std::shared_ptr<MediaSourceHolder>& holder,
      const std::shared_ptr<MediaSource>& mediaSource,
      const std::shared_ptr<MediaSourceCaller>& caller) noexcept
      : holder(holder),
        mediaSource(mediaSource),
        caller(caller) {
    }
  };

  std::vector<std::shared_ptr<MediaSourceHolder>> mediaSourceHolders;
  std::vector<std::shared_ptr<MediaPeriodAndSourceHolder>> mediaSourceByMediaPeriod;
  std::unordered_map<PeriodUid, std::shared_ptr<MediaSourceHolder>> mediaSourceByUid;
  MediaSourceListInfoRefreshListener* mediaSourceListInfoListener;
  std::vector<std::shared_ptr<MediaSourceHolderAndListener>> childSources;
  std::vector<std::shared_ptr<MediaSourceHolder>> enabledMediaSourceHolders;
  std::vector<std::shared_ptr<EventListenerAndHandler>> eventListeners;
  std::shared_ptr<ShuffleOrder> shuffleOrder;
  bool prepared;

public:
  explicit MediaSourceList(MediaSourceListInfoRefreshListener* listener) noexcept;

  ~MediaSourceList();

  /**
   * Registers a listener of the events of every media source of the playlist.
   *
   * <p>The events of a child source carry the identifiers of the playlist: its window index and its
   * period identifier are remapped by a forwarding listener before the listener sees them.
   *
   * @param eventHandler The handler the events are delivered on, or null.
   * @param eventListener The listener to register.
   */
  void addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
    const std::shared_ptr<MediaSourceEventListener>& eventListener);

  /**
   * Unregisters a listener that was registered with {@link #addEventListener}.
   *
   * @param eventListener The listener to unregister.
   */
  void removeEventListener(const std::shared_ptr<MediaSourceEventListener>& eventListener);

  /**
   * Sets the media sources replacing any sources previously contained in the playlist.
   *
   * @param holders The list of {@link MediaSourceHolder}s to set.
   * @param shuffleOrder The new shuffle order.
   * @return The new {@link Timeline}.
   */
  std::shared_ptr<Timeline>
  setMediaSources(const std::vector<std::shared_ptr<MediaSourceHolder>>& holders,
    const std::shared_ptr<ShuffleOrder>& shuffleOrder);

  /**
   * Adds multiple {@link MediaSourceHolder}s to the playlist.
   *
   * @param index The index at which the new {@link MediaSourceHolder}s will be inserted. This index
   *     must be in the range of 0 &lt;= index &lt;= {@link getSize()}.
   * @param holders A list of {@link MediaSourceHolder}s to be added.
   * @param shuffleOrder The new shuffle order.
   * @return The new {@link Timeline}.
   */
  std::shared_ptr<Timeline>
  addMediaSources(int index, const std::vector<std::shared_ptr<MediaSourceHolder>>& holders,
    const std::shared_ptr<ShuffleOrder>& shuffleOrder);

  /** Clears the playlist. */
  std::shared_ptr<Timeline>
  clear(const std::shared_ptr<ShuffleOrder>& shuffleOrder);

  /**
   * Removes a range of {@link MediaSourceHolder}s from the playlist.
   *
   * <p>Mirrors ExoPlayer's {@code MediaSourceList.removeMediaSources}. A holder whose periods are
   * still active is kept alive until they are released, even though it is no longer part of the
   * playlist.
   *
   * @param fromIndex The index of the first holder to remove.
   * @param toIndex The index after the last holder to remove.
   * @param shuffleOrder The new shuffle order.
   * @return The new {@link Timeline}.
   */
  std::shared_ptr<Timeline>
  removeMediaSources(int fromIndex, int toIndex, const std::shared_ptr<ShuffleOrder>& shuffleOrder);

  /**
   * Moves a range of {@link MediaSourceHolder}s to a new index.
   *
   * <p>Mirrors ExoPlayer's {@code MediaSourceList.moveMediaSources}. The shuffle order of the child
   * sources is unchanged: only their position in the playlist moves.
   *
   * @param fromIndex The index of the first holder to move.
   * @param toIndex The index after the last holder to move.
   * @param newIndex The index the range is inserted at, after it was removed.
   * @param shuffleOrder The new shuffle order.
   * @return The new {@link Timeline}.
   */
  std::shared_ptr<Timeline> moveMediaSources(int fromIndex, int toIndex, int newIndex,
    const std::shared_ptr<ShuffleOrder>& shuffleOrder);

  /** Whether the playlist is prepared. */
  [[nodiscard]] bool isPrepared() const {
    return prepared;
  }

  /** Returns the number of media sources in the playlist. */
  [[nodiscard]] int getSize() const {
    return mediaSourceHolders.size();
  }

  /**
   * Sets a new shuffle order to use when shuffling the child media sources.
   *
   * @param shuffleOrder A {@link ShuffleOrder}.
   */
  std::shared_ptr<Timeline>
  setShuffleOrder(const std::shared_ptr<ShuffleOrder>& shuffleOrder);

  /** Prepares the playlist. */
  void prepare();

  /**
   * Returns a new {@link MediaPeriod} identified by {@code periodId}.
   *
   * @param id The identifier of the period.
   * @param allocator An {@link Allocator} from which to obtain media buffer allocations.
   * @param startPositionUs The expected start position, in microseconds.
   * @return A new {@link MediaPeriod}.
   */
  std::shared_ptr<MediaPeriod>
  createPeriod(MediaPeriodId id, const std::shared_ptr<Allocator>& allocator,
    int64_t startPositionUs);

  /**
   * Releases the period.
   *
   * @param mediaPeriod The period to release.
   */
  void releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod);

  /** Releases the playlist. */
  void release();

  /** Creates a timeline reflecting the current state of the playlist. */
  std::shared_ptr<Timeline> createTimeline();

  /**
   * Reports the creation of a period of the playlist to its listeners.
   *
   * @param mediaPeriodId The identifier of the period, as the playlist reports it.
   */
  void notifyMediaPeriodCreated(const MediaPeriodId& mediaPeriodId);

  /**
   * Reports the release of a period of the playlist to its listeners.
   *
   * @param mediaPeriodId The identifier of the period, as the playlist reports it.
   */
  void notifyMediaPeriodReleased(const MediaPeriodId& mediaPeriodId);

  /**
   * Returns the index of the window the period belongs to, or {@link Definitions::kIndexUnset}.
   *
   * @param mediaPeriodId The identifier of the period, as the playlist reports it.
   */
  [[nodiscard]] int getWindowIndexForPeriodId(const MediaPeriodId& mediaPeriodId) const;


private:
  void enableMediaSource(const std::shared_ptr<MediaSourceHolder>& mediaSourceHolder);

  void disableUnusedMediaSources();

  void disableChildSource(const std::shared_ptr<MediaSourceHolder>& holder);

  void removeMediaSourcesInternal(int fromIndex, int toIndex);

  void correctOffsets(int startIndex, int windowOffsetUpdate);

  static std::optional<MediaPeriodId>
  getMediaPeriodIdForChildMediaPeriodId(const std::shared_ptr<MediaSourceHolder>& mediaSourceHolder,
    MediaPeriodId mediaPeriodId);

  static int getWindowIndexForChildWindowIndex(
    const std::shared_ptr<MediaSourceHolder>& mediaSourceHolder, int windowIndex);

  void prepareChildSource(const std::shared_ptr<MediaSourceHolder>& holder);

  /**
   * Registers a forwarding listener for {@code listener} on one child source.
   */
  void registerChildEventListener(const std::shared_ptr<MediaSourceHolderAndListener>& childSource,
    const std::shared_ptr<EventHandler>& eventHandler,
    const std::shared_ptr<MediaSourceEventListener>& listener);

  /** Unregisters every forwarding listener of one child source. */
  void unregisterChildEventListeners(
    const std::shared_ptr<MediaSourceHolderAndListener>& childSource);

  void maybeReleaseChildSource(const std::shared_ptr<MediaSourceHolder>& mediaSourceHolder);

  static std::optional<PeriodUid>
  getMediaSourceHolderUid(const PeriodUid& periodUid);

  static std::optional<PeriodUid>
  getChildPeriodUid(const PeriodUid& periodUid);

  static std::optional<PeriodUid>
  getPeriodUid(const std::shared_ptr<MediaSourceHolder>& holder,
    const PeriodUid& childPeriodUid);

  class MediaSourceCallerImpl final : public MediaSourceCaller {
  private:
    MediaSourceList* list;

  public:
    explicit MediaSourceCallerImpl(MediaSourceList* list) noexcept
      : list(list) {
    }

    ~MediaSourceCallerImpl() override = default;

    void onSourceInfoRefreshed(const std::shared_ptr<MediaSource>& source,
      const std::shared_ptr<Timeline>& timeline) override;
  };

  class ForwardingEventListener final : public MediaSourceEventListener {
  private:
    std::weak_ptr<MediaSourceHolder> holder;
    /** The listener of the playlist that receives the remapped events. */
    std::shared_ptr<MediaSourceEventListener> target;

  public:
    explicit ForwardingEventListener(const std::shared_ptr<MediaSourceHolder>& holder,
      std::shared_ptr<MediaSourceEventListener> target) noexcept
      : holder(holder),
        target(std::move(target)) {
    }

    ~ForwardingEventListener() override = default;

    void onLoadStarted(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData) override;

    void onMediaPeriodCreated(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId) override;

    void onMediaPeriodReleased(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId) override;

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

  private:
    /** Remaps the period identifier of a child source to the identifier of the playlist. */
    [[nodiscard]] std::optional<MediaPeriodId> toPlaylistMediaPeriodId(
      const MediaPeriodId& childMediaPeriodId) const;
  };
};

} // namespace coral

#endif //CORAL_PLAYBACK_DETAILS_MEDIA_SOURCE_LIST_H
