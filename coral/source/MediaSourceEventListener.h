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
#ifndef CORAL_PLAYBACK_SOURCE_MEDIA_SOURCE_EVENT_LISTENER_H
#define CORAL_PLAYBACK_SOURCE_MEDIA_SOURCE_EVENT_LISTENER_H

#include <coral/source/LoadEventInfo.h>
#include <coral/source/MediaLoadData.h>
#include <coral/source/MediaPeriodId.h>

#include <codec/Error.h>

#include <core/event/EventHandler.h>

#include <memory>
#include <optional>

namespace coral {

/** Interface for callbacks to be notified of {@link MediaSource} events. */
class MediaSourceEventListener {
public:
  virtual ~MediaSourceEventListener() = default;

  /**
   * Called when a media period is created.
   *
   * @param windowIndex The window index in the timeline of the media source this period belongs to.
   * @param mediaPeriodId The {@link MediaPeriodId} of the period.
   */
  virtual void onMediaPeriodCreated(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId) {
    static_cast<void>(windowIndex);
    static_cast<void>(mediaPeriodId);
  }

  /**
   * Called when a media period is released.
   *
   * @param windowIndex The window index in the timeline of the media source this period belonged to.
   * @param mediaPeriodId The {@link MediaPeriodId} of the period.
   */
  virtual void onMediaPeriodReleased(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId) {
    static_cast<void>(windowIndex);
    static_cast<void>(mediaPeriodId);
  }

  /**
   * Called when a load begins.
   *
   * @param windowIndex The window index in the timeline of the media source this load belongs to.
   * @param mediaPeriodId The {@link MediaPeriodId} this load belongs to. Null if the load does not
   *     belong to a specific media period.
   * @param loadEventInfo The {@link LoadEventInfo} corresponding to the event. The value of {@link
   *     LoadEventInfo#uri} won't reflect potential redirection yet and {@link
   *     LoadEventInfo#responseHeaders} will be empty.
   * @param mediaLoadData The {@link MediaLoadData} defining the data being loaded.
   */
  virtual void onLoadStarted(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData) {}

  /**
   * Called when a load ends.
   *
   * @param windowIndex The window index in the timeline of the media source this load belongs to.
   * @param mediaPeriodId The {@link MediaPeriodId} this load belongs to. Null if the load does not
   *     belong to a specific media period.
   * @param loadEventInfo The {@link LoadEventInfo} corresponding to the event. The values of {@link
   *     LoadEventInfo#elapsedRealtimeMs} and {@link LoadEventInfo#bytesLoaded} are relative to the
   *     corresponding {@link #onLoadStarted(int, MediaPeriodId, LoadEventInfo, MediaLoadData)}
   *     event.
   * @param mediaLoadData The {@link MediaLoadData} defining the data being loaded.
   */
  virtual void onLoadCompleted(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData) {}

  /**
   * Called when a load is canceled.
   *
   * @param windowIndex The window index in the timeline of the media source this load belongs to.
   * @param mediaPeriodId The {@link MediaPeriodId} this load belongs to. Null if the load does not
   *     belong to a specific media period.
   * @param loadEventInfo The {@link LoadEventInfo} corresponding to the event. The values of {@link
   *     LoadEventInfo#elapsedRealtimeMs} and {@link LoadEventInfo#bytesLoaded} are relative to the
   *     corresponding {@link #onLoadStarted(int, MediaPeriodId, LoadEventInfo, MediaLoadData)}
   *     event.
   * @param mediaLoadData The {@link MediaLoadData} defining the data being loaded.
   */
  virtual void onLoadCanceled(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData) {}

  /**
   * Called when a load error occurs.
   *
   * <p>The error may or may not have resulted in the load being canceled, as indicated by the
   * {@code wasCanceled} parameter. If the load was canceled, {@link #onLoadCanceled} will
   * <em>not</em> be called in addition to this method.
   *
   * <p>This method being called does not indicate that playback has failed, or that it will fail.
   * The player may be able to recover from the error and continue. Hence applications should
   * <em>not</em> implement this method to display a user visible error or initiate an application
   * level retry ({@link Player.EventListener#onPlayerError} is the appropriate place to implement
   * such behavior). This method is called to provide the application with an opportunity to log the
   * error if it wishes to do so.
   *
   * @param windowIndex The window index in the timeline of the media source this load belongs to.
   * @param mediaPeriodId The {@link MediaPeriodId} this load belongs to. Null if the load does not
   *     belong to a specific media period.
   * @param loadEventInfo The {@link LoadEventInfo} corresponding to the event. The values of {@link
   *     LoadEventInfo#elapsedRealtimeMs} and {@link LoadEventInfo#bytesLoaded} are relative to the
   *     corresponding {@link #onLoadStarted(int, MediaPeriodId, LoadEventInfo, MediaLoadData)}
   *     event.
   * @param mediaLoadData The {@link MediaLoadData} defining the data being loaded.
   * @param error The load error.
   * @param wasCanceled Whether the load was canceled as a result of the error.
   */
  virtual void onLoadError(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData,
      const Error* error,
      bool wasCanceled) {}

  /**
   * Called when data is removed from the back of a media buffer, typically so that it can be
   * re-buffered in a different format.
   *
   * @param windowIndex The window index in the timeline of the media source this load belongs to.
   * @param mediaPeriodId The {@link MediaPeriodId} the media belongs to.
   * @param mediaLoadData The {@link MediaLoadData} defining the media being discarded.
   */
  virtual void onUpstreamDiscarded(int windowIndex,
      const MediaPeriodId& mediaPeriodId,
      const MediaLoadData& mediaLoadData) {}

  /**
   * Called when a downstream format change occurs (i.e. when the format of the media being read
   * from one or more {@link SampleStream}s provided by the source changes).
   *
   * @param windowIndex The window index in the timeline of the media source this load belongs to.
   * @param mediaPeriodId The {@link MediaPeriodId} the media belongs to.
   * @param mediaLoadData The {@link MediaLoadData} defining the newly selected downstream data.
   */
  virtual void onDownstreamFormatChanged(int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const MediaLoadData& mediaLoadData) {}
};

/**
 * Pairs a listener with the handler that its events are delivered on.
 *
 * <p>Mirrors ExoPlayer's {@code EventListenerAndHandler}. A media source keeps one of these per
 * registered listener; a source whose periods are played inside a playlist is registered on its
 * parent with the handler the listener asked for, so that the events keep the thread affinity the
 * application chose.
 */
struct EventListenerAndHandler {
  std::shared_ptr<EventHandler> eventHandler;
  std::shared_ptr<MediaSourceEventListener> eventListener;

  EventListenerAndHandler(std::shared_ptr<EventHandler> eventHandler,
      std::shared_ptr<MediaSourceEventListener> eventListener) noexcept
      : eventHandler(std::move(eventHandler)),
        eventListener(std::move(eventListener)) {
  }
};

/**
 * Delivers an event to a listener on the thread of its handler.
 *
 * <p>The handler is applied where the event is created, so the listener is called on the thread the
 * application asked for even when the event comes from the loading thread. A null handler delivers
 * the event inline, which is what a source used without a looper needs.
 *
 * @param eventHandler The handler of the listener, may be null.
 * @param event The closure that calls the listener.
 */
template<typename Event>
void dispatchMediaSourceEvent(const std::shared_ptr<EventHandler>& eventHandler, Event&& event) {
  if (eventHandler != nullptr) {
    eventHandler->post(std::forward<Event>(event));
    return;
  }
  event();
}

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MEDIA_SOURCE_EVENT_LISTENER_H
