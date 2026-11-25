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
#ifndef CORAL_PLAYBACK_SOURCE_MEDIA_SOURCE_H
#define CORAL_PLAYBACK_SOURCE_MEDIA_SOURCE_H

#include <coral/source/MediaPeriodId.h>
#include <coral/source/MediaPeriod.h>
#include <coral/source/MediaSourceEventListener.h>
#include <coral/source/Timeline.h>
#include <coral/upstream/Allocator.h>

#include <memory>
#include <string>

namespace coral {

class MediaSource;

/** A caller of media sources, which will be notified of source events. */
class MediaSourceCaller {
public:
  virtual ~MediaSourceCaller() = default;

  /**
   * Called when the {@link Timeline} has been refreshed.
   *
   * <p>Called on the playback thread.
   *
   * @param source The {@link MediaSource} whose info has been refreshed.
   * @param timeline The source's timeline.
   */
  virtual void onSourceInfoRefreshed(const std::shared_ptr<MediaSource>& source,
      const std::shared_ptr<Timeline>& timeline) = 0;
};

/**
 * Defines and provides media to be played by a Player. A MediaSource has two main responsibilities:
 *
 * <ul>
 *   <li>To provide the player with a {@link Timeline} defining the structure of its media, and to
 *       provide a new timeline whenever the structure of the media changes. The MediaSource
 *       provides these timelines by calling {@link MediaSourceCaller#onSourceInfoRefreshed} on the
 *       {@link MediaSourceCaller}s passed to {@link #prepareSource(MediaSourceCaller)}.
 *   <li>To provide {@link MediaPeriod} instances for the periods in its timeline. MediaPeriods are
 *       obtained by calling {@link #createPeriod(MediaPeriodId, Allocator, long)}, and provide a
 *       way for the player to load and read the media.
 * </ul>
 *
 * All methods are called on the player's internal playback thread. They should not be called
 * directly from application code. Instances can be re-used, but only for one Player instance
 * simultaneously.
 */
class MediaSource {
public:
  virtual ~MediaSource() = default;

  /**
   * Returns the initial placeholder timeline that is returned immediately when the real timeline is
   * not yet known, or null to let the player create an initial timeline.
   *
   * The initial timeline must use the same uids for windows and periods that the real timeline
   * will use. It also must provide windows which are marked as dynamic to indicate that the window
   * is expected to change when the real timeline arrives.
   *
   * Any media source which has multiple windows should typically provide such an initial
   * timeline to make sure the player reports the correct number of windows immediately.
   */
  [[nodiscard]] virtual const std::shared_ptr<Timeline>& getInitialTimeline() const {
    static const std::shared_ptr<Timeline> emptyTimeline = nullptr;
    return emptyTimeline;
  }

  /**
   * Returns true if the media source is guaranteed to never have zero or more than one window.
   *
   * The default implementation returns {@code true}.
   *
   * @return true if the source has exactly one window.
   */
  virtual bool isSingleWindow() {
    return true;
  }

  /** Returns the {@link MediaItem} whose media is provided by the source. */
  virtual const std::shared_ptr<MediaItem>& getMediaItem() const = 0;

  /**
   * Registers a {@link MediaSourceCaller}. Starts source preparation if needed and enables the
   * source for the creation of {@link MediaPeriod}s.
   *
   * {@link MediaSourceCaller#onSourceInfoRefreshed(MediaSource, Timeline)} will be called once
   * the source has a {@link Timeline}.
   *
   * For each call to this method, a call to {@link #releaseSource(MediaSourceCaller)} is needed
   * to remove the caller and to release the source if no longer required.
   *
   * @param caller The {@link MediaSourceCaller} to be registered.
   * @param mediaTransferListener The transfer listener which should be informed of any media data
   *     transfers. May be null if no listener is available. Note that this listener should be only
   *     informed of transfers related to the media loads and not of auxiliary loads for manifests
   *     and other data.
   */
  virtual void prepareSource(const std::shared_ptr<MediaSourceCaller>& caller) = 0;

  /**
   * Enables the source for the creation of {@link MediaPeriod}s.
   *
   * Must only be called after {@link #prepareSource(MediaSourceCaller)}.
   *
   * @param caller The {@link MediaSourceCaller} enabling the source.
   */
  virtual void enable(const std::shared_ptr<MediaSourceCaller>& caller) = 0;

  /**
   * Returns a new {@link MediaPeriod} identified by {@code periodId}.
   *
   * This method must be called on the playback thread and only if the source is enabled.
   *
   * @param id The identifier of the period.
   * @param allocator An {@link Allocator} from which to obtain media buffer allocations.
   * @param startPositionUs The expected start position, in microseconds.
   * @return A new {@link MediaPeriod}.
   */
  virtual std::shared_ptr<MediaPeriod>
  createPeriod(MediaPeriodId id, const std::shared_ptr<Allocator>& allocator, int64_t startPositionUs) = 0;

  /**
   * Releases the period.
   *
   * Should not be called directly from application code.
   *
   * This method must be called on the playback thread.
   *
   * @param mediaPeriod The period to release.
   */
  virtual void releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) = 0;

  /**
   * Disables the source for the creation of {@link MediaPeriod}s. The implementation should not
   * hold onto limited resources used for the creation of media periods.
   *
   * Must only be called after all {@link MediaPeriod}s previously created by {@link
   * #createPeriod(MediaPeriodId, Allocator, long)} have been released by {@link
   * #releasePeriod(MediaPeriod)}.
   *
   * @param caller The {@link MediaSourceCaller} disabling the source.
   */
  virtual void disable(const std::shared_ptr<MediaSourceCaller>& caller) = 0;

  /**
   * Unregisters a caller, and disables and releases the source if no longer required.
   *
   * Must only be called if all created {@link MediaPeriod}a have been released by {@link
   * #releasePeriod(MediaPeriod)}.
   *
   * @param caller The {@link MediaSourceCaller} to be unregistered.
   */
  virtual void releaseSource(const std::shared_ptr<MediaSourceCaller>& caller) = 0;

  /**
   * Registers a {@link MediaSourceEventListener} to receive the events of this source.
   *
   * <p>The listener is called on the thread of {@code eventHandler}, which may be null to receive
   * the events on the thread that produced them, e.g. the loading thread.
   *
   * <p>Listeners belong to the source for its whole life: they are not removed by {@link
   * #releaseSource(MediaSourceCaller)}, so the application has to call
   * {@link #removeEventListener(MediaSourceEventListener)} when it no longer needs them.
   *
   * @param eventHandler The handler the events are delivered on, or null.
   * @param eventListener The listener to register.
   */
  virtual void addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
      const std::shared_ptr<MediaSourceEventListener>& eventListener) = 0;

  /**
   * Unregisters a {@link MediaSourceEventListener}.
   *
   * @param eventListener The listener to unregister.
   */
  virtual void removeEventListener(
      const std::shared_ptr<MediaSourceEventListener>& eventListener) = 0;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MEDIA_SOURCE_H
