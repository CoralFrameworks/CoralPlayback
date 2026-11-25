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
#ifndef CORAL_PLAYBACK_SOURCE_BASE_MEDIA_SOURCE_H
#define CORAL_PLAYBACK_SOURCE_BASE_MEDIA_SOURCE_H

#include <coral/source/MediaSource.h>

#include <core/task/Mutex.h>

#include <core/event/EventLooper.h>

#include <memory>
#include <optional>
#include <vector>

namespace coral {

/**
 * Base {@link MediaSource} implementation to handle parallel reuse and to keep a list of {@link
 * MediaSourceEventListener}s.
 *
 * Whenever an implementing subclass needs to provide a new timeline, it must call {@link
 * #refreshSourceInfo(Timeline)} to notify all listeners.
 *
 * Subclasses that want to report the events of their loads create one {@link EventListenerDispatcher}
 * per media period - see {@link #createEventDispatcher(MediaPeriodId)} - and hand it to the period,
 * which calls its `load*` methods. The dispatcher re-reads the listeners of the source at every
 * event, so a listener that is added after a period was created still receives its events.
 */
class BaseMediaSource : public MediaSource,
                        public std::enable_shared_from_this<BaseMediaSource> {
public:
  /**
   * Dispatches the events of one media period to every listener of the source.
   *
   * <p>Mirrors ExoPlayer's {@code BaseMediaSource.EventListenerDispatcher}. An instance keeps the
   * source weakly: a period may outlive the source that created it, and an event that arrives after
   * the source was destroyed is dropped rather than dereferencing it. The default constructed
   * dispatcher does nothing, so a source that was created without a listener still has a valid
   * dispatcher to hand to its periods.
   */
  class EventListenerDispatcher final {
  public:
    EventListenerDispatcher() = default;

    EventListenerDispatcher(const EventListenerDispatcher& other) = default;
    EventListenerDispatcher& operator=(const EventListenerDispatcher& other) = default;

    /** Reports that a load started. */
    /** Dispatches the creation of the period this dispatcher belongs to. */
    void mediaPeriodCreated() const;

    /** Dispatches the release of the period this dispatcher belongs to. */
    void mediaPeriodReleased() const;

    void loadStarted(const LoadEventInfo& loadEventInfo, const MediaLoadData& mediaLoadData) const;

    /** Reports that a load completed. */
    void loadCompleted(
        const LoadEventInfo& loadEventInfo, const MediaLoadData& mediaLoadData) const;

    /** Reports that a load was canceled. */
    void loadCanceled(const LoadEventInfo& loadEventInfo, const MediaLoadData& mediaLoadData) const;

    /** Reports that a load failed. */
    void loadError(const LoadEventInfo& loadEventInfo,
        const MediaLoadData& mediaLoadData,
        const Error& error,
        bool wasCanceled) const;

    /** Reports that data was discarded from the back of a media buffer. */
    void upstreamDiscarded(const MediaLoadData& mediaLoadData) const;

    /** Reports that the format a stream delivers to a renderer changed. */
    void downstreamFormatChanged(const MediaLoadData& mediaLoadData) const;

    /**
     * Returns the dispatcher of one period of this source.
     *
     * @param windowIndex The index of the window the period belongs to.
     * @param mediaPeriodId The identifier of the period.
     */
    [[nodiscard]] EventListenerDispatcher mediaPeriodEventDispatcher(
        int windowIndex, const MediaPeriodId& mediaPeriodId) const;

    /** Returns a dispatcher whose media period id is replaced by {@code mediaPeriodId}. */
    [[nodiscard]] EventListenerDispatcher withMediaPeriodId(
        const MediaPeriodId& mediaPeriodId) const;

  private:
    friend class BaseMediaSource;

    EventListenerDispatcher(std::weak_ptr<BaseMediaSource> source,
        int windowIndex,
        std::optional<MediaPeriodId> mediaPeriodId) noexcept
        : source(std::move(source)),
          windowIndex(windowIndex),
          mediaPeriodId(std::move(mediaPeriodId)) {
    }

    /** The source whose listeners receive the events, kept weakly. */
    std::weak_ptr<BaseMediaSource> source;

    /** The index of the window the events belong to, or {@link Definitions::kIndexUnset}. */
    int windowIndex;

    /** The identifier of the media period the events belong to, or null. */
    std::optional<MediaPeriodId> mediaPeriodId;
  };

private:
  std::vector<std::shared_ptr<MediaSourceCaller>> mediaSourceCallers;
  std::vector<std::shared_ptr<MediaSourceCaller>> enabledMediaSourceCallers;
  std::weak_ptr<EventLooper> mLooper;
  std::shared_ptr<Timeline> mTimeline;

  /**
   * Guards {@link #eventListeners}, which a period reads from the loading thread while the
   * application registers and unregisters its listeners from the playback thread.
   */
  mutable Mutex eventListenersLock;
  std::vector<std::shared_ptr<EventListenerAndHandler>> eventListeners;

  /**
   * Returns the listeners that are registered now.
   *
   * The events are produced by the loading thread while the listeners are registered by the
   * playback thread, so a dispatcher takes a snapshot instead of iterating the list itself.
   */
  [[nodiscard]] std::vector<std::shared_ptr<EventListenerAndHandler>> getEventListeners() const;

protected:
  virtual void prepareSourceInternal() = 0;

  /** Enables the source, see {@link #enable(MediaSourceCaller)}. */
  virtual void enableInternal() {}

  /** Disables the source, see {@link #disable(MediaSourceCaller)}. */
  virtual void disableInternal() {}

  /**
   * Releases the source, see {@link #releaseSource(MediaSourceCaller)}. This method is called
   * exactly once after each call to {@link #prepareSourceInternal()}.
   */
  virtual void releaseSourceInternal() = 0;

  /**
   * Updates timeline and manifest and notifies all listeners of the update.
   *
   * @param timeline The new {@link Timeline}.
   */
  void refreshSourceInfo(const std::shared_ptr<Timeline>& timeline);

  /**
   * Creates the dispatcher of the events of a media period of this source.
   *
   * The window index of the events is {@link Definitions::kIndexUnset}, because a source does not
   * know where its windows end up in the timeline of the player. A source that is played inside a
   * playlist has its events remapped, see {@code MediaSourceList::ForwardingEventListener}.
   *
   * @param mediaPeriodId The identifier of the period.
   */
  [[nodiscard]] EventListenerDispatcher createEventDispatcher(const MediaPeriodId& mediaPeriodId);

  /**
   * Creates the dispatcher of the events of this source that do not belong to a media period.
   */
  [[nodiscard]] EventListenerDispatcher createEventDispatcher();

public:
  /** Returns whether the source is enabled. */
  bool isEnabled() const { return !enabledMediaSourceCallers.empty(); }

  void prepareSource(const std::shared_ptr<MediaSourceCaller>& caller) override;

  void enable(const std::shared_ptr<MediaSourceCaller>& caller) override;

  void disable(const std::shared_ptr<MediaSourceCaller>& caller) override;

  void releaseSource(const std::shared_ptr<MediaSourceCaller>& caller) override;

  void addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
      const std::shared_ptr<MediaSourceEventListener>& eventListener) override;

  void removeEventListener(const std::shared_ptr<MediaSourceEventListener>& eventListener) override;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_BASE_MEDIA_SOURCE_H
