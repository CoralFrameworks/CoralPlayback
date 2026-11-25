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
#include <coral/source/BaseMediaSource.h>

#include <algorithm>
#include <cassert>

namespace coral {

void BaseMediaSource::refreshSourceInfo(const std::shared_ptr<Timeline>& timeline) {
  mTimeline = timeline;
  for (auto const& caller: mediaSourceCallers) {
    caller->onSourceInfoRefreshed(shared_from_this(), timeline);
  }
}

void BaseMediaSource::prepareSource(const std::shared_ptr<MediaSourceCaller>& caller) {
  auto const& looper = EventLooper::myLooper();
  assert(mLooper.lock() == nullptr || looper == mLooper.lock());
  mediaSourceCallers.push_back(caller);
  if (mLooper.lock() == nullptr) {
    auto position =
        std::find(enabledMediaSourceCallers.begin(), enabledMediaSourceCallers.end(), caller);
    if (position == enabledMediaSourceCallers.end()) {
      enabledMediaSourceCallers.push_back(caller);
    }
    prepareSourceInternal();
  } else if (mTimeline != nullptr) {
    enable(caller);
    caller->onSourceInfoRefreshed(shared_from_this(), mTimeline);
  }
}

void BaseMediaSource::enable(const std::shared_ptr<MediaSourceCaller>& caller) {
  auto wasDisabled = enabledMediaSourceCallers.empty();
  auto position =
      std::find(enabledMediaSourceCallers.begin(), enabledMediaSourceCallers.end(), caller);
  if (position == enabledMediaSourceCallers.end()) {
    enabledMediaSourceCallers.push_back(caller);
  }
  if (wasDisabled) {
    enableInternal();
  }
}

void BaseMediaSource::disable(const std::shared_ptr<MediaSourceCaller>& caller) {
  auto wasEnabled = !enabledMediaSourceCallers.empty();
  auto position =
      std::find(enabledMediaSourceCallers.begin(), enabledMediaSourceCallers.end(), caller);
  if (position != enabledMediaSourceCallers.end()) {
    enabledMediaSourceCallers.erase(position);
  }
  if (wasEnabled && enabledMediaSourceCallers.empty()) {
    disableInternal();
  }
}

void BaseMediaSource::releaseSource(const std::shared_ptr<MediaSourceCaller>& caller) {
  auto position =
      std::find(mediaSourceCallers.begin(), mediaSourceCallers.end(), caller);
  if (position != mediaSourceCallers.end()) {
    mediaSourceCallers.erase(position);
  }
  if (mediaSourceCallers.empty()) {
    mLooper.reset();
    mTimeline.reset();
    enabledMediaSourceCallers.clear();
    // The event listeners deliberately survive the release: an application that reuses the source
    // keeps receiving the events of its next preparation without registering again, exactly like
    // ExoPlayer's BaseMediaSource.
    releaseSourceInternal();
  } else {
    disable(caller);
  }
}

void BaseMediaSource::addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
    const std::shared_ptr<MediaSourceEventListener>& eventListener) {
  assert(eventListener != nullptr);
  Lock lock(eventListenersLock);
  for (const auto& listenerAndHandler : eventListeners) {
    if (listenerAndHandler->eventListener == eventListener) {
      // A listener is registered once: registering it again only replaces the handler.
      listenerAndHandler->eventHandler = eventHandler;
      return;
    }
  }
  eventListeners.push_back(
      std::make_shared<EventListenerAndHandler>(eventHandler, eventListener));
}

void BaseMediaSource::removeEventListener(
    const std::shared_ptr<MediaSourceEventListener>& eventListener) {
  Lock lock(eventListenersLock);
  auto position = eventListeners.begin();
  while (position != eventListeners.end()) {
    if (position->get()->eventListener == eventListener) {
      eventListeners.erase(position);
      return;
    }
    ++position;
  }
}

std::vector<std::shared_ptr<EventListenerAndHandler>> BaseMediaSource::getEventListeners() const {
  Lock lock(eventListenersLock);
  return eventListeners;
}

BaseMediaSource::EventListenerDispatcher BaseMediaSource::createEventDispatcher(
    const MediaPeriodId& mediaPeriodId) {
  return EventListenerDispatcher(
      weak_from_this(), Definitions::kIndexUnset, std::optional<MediaPeriodId>(mediaPeriodId));
}

BaseMediaSource::EventListenerDispatcher BaseMediaSource::createEventDispatcher() {
  return EventListenerDispatcher(weak_from_this(), Definitions::kIndexUnset, std::nullopt);
}

// ==================================================================================================
// EventListenerDispatcher
// ==================================================================================================

BaseMediaSource::EventListenerDispatcher
BaseMediaSource::EventListenerDispatcher::mediaPeriodEventDispatcher(
    const int windowIndex, const MediaPeriodId& mediaPeriodId) const {
  return EventListenerDispatcher(source, windowIndex, std::optional<MediaPeriodId>(mediaPeriodId));
}

BaseMediaSource::EventListenerDispatcher BaseMediaSource::EventListenerDispatcher::withMediaPeriodId(
    const MediaPeriodId& newMediaPeriodId) const {
  return EventListenerDispatcher(source, windowIndex, std::optional<MediaPeriodId>(newMediaPeriodId));
}

void BaseMediaSource::EventListenerDispatcher::mediaPeriodCreated() const {
  const auto lockedSource = source.lock();
  if (lockedSource == nullptr) {
    return;
  }
  const auto eventWindowIndex = windowIndex;
  const auto eventMediaPeriodId = mediaPeriodId;
  for (const auto& listenerAndHandler : lockedSource->getEventListeners()) {
    const auto eventListener = listenerAndHandler->eventListener;
    dispatchMediaSourceEvent(listenerAndHandler->eventHandler,
        [eventListener, eventWindowIndex, eventMediaPeriodId] {
          eventListener->onMediaPeriodCreated(eventWindowIndex, eventMediaPeriodId);
        });
  }
}

void BaseMediaSource::EventListenerDispatcher::mediaPeriodReleased() const {
  const auto lockedSource = source.lock();
  if (lockedSource == nullptr) {
    return;
  }
  const auto eventWindowIndex = windowIndex;
  const auto eventMediaPeriodId = mediaPeriodId;
  for (const auto& listenerAndHandler : lockedSource->getEventListeners()) {
    const auto eventListener = listenerAndHandler->eventListener;
    dispatchMediaSourceEvent(listenerAndHandler->eventHandler,
        [eventListener, eventWindowIndex, eventMediaPeriodId] {
          eventListener->onMediaPeriodReleased(eventWindowIndex, eventMediaPeriodId);
        });
  }
}

void BaseMediaSource::EventListenerDispatcher::loadStarted(
    const LoadEventInfo& loadEventInfo, const MediaLoadData& mediaLoadData) const {
  const auto lockedSource = source.lock();
  if (lockedSource == nullptr) {
    return;
  }
  const auto eventWindowIndex = windowIndex;
  const auto eventMediaPeriodId = mediaPeriodId;
  for (const auto& listenerAndHandler : lockedSource->getEventListeners()) {
    const auto eventListener = listenerAndHandler->eventListener;
    dispatchMediaSourceEvent(listenerAndHandler->eventHandler,
        [eventListener, eventWindowIndex, eventMediaPeriodId, loadEventInfo, mediaLoadData] {
          eventListener->onLoadStarted(
              eventWindowIndex, eventMediaPeriodId, loadEventInfo, mediaLoadData);
        });
  }
}

void BaseMediaSource::EventListenerDispatcher::loadCompleted(
    const LoadEventInfo& loadEventInfo, const MediaLoadData& mediaLoadData) const {
  const auto lockedSource = source.lock();
  if (lockedSource == nullptr) {
    return;
  }
  const auto eventWindowIndex = windowIndex;
  const auto eventMediaPeriodId = mediaPeriodId;
  for (const auto& listenerAndHandler : lockedSource->getEventListeners()) {
    const auto eventListener = listenerAndHandler->eventListener;
    dispatchMediaSourceEvent(listenerAndHandler->eventHandler,
        [eventListener, eventWindowIndex, eventMediaPeriodId, loadEventInfo, mediaLoadData] {
          eventListener->onLoadCompleted(
              eventWindowIndex, eventMediaPeriodId, loadEventInfo, mediaLoadData);
        });
  }
}

void BaseMediaSource::EventListenerDispatcher::loadCanceled(
    const LoadEventInfo& loadEventInfo, const MediaLoadData& mediaLoadData) const {
  const auto lockedSource = source.lock();
  if (lockedSource == nullptr) {
    return;
  }
  const auto eventWindowIndex = windowIndex;
  const auto eventMediaPeriodId = mediaPeriodId;
  for (const auto& listenerAndHandler : lockedSource->getEventListeners()) {
    const auto eventListener = listenerAndHandler->eventListener;
    dispatchMediaSourceEvent(listenerAndHandler->eventHandler,
        [eventListener, eventWindowIndex, eventMediaPeriodId, loadEventInfo, mediaLoadData] {
          eventListener->onLoadCanceled(
              eventWindowIndex, eventMediaPeriodId, loadEventInfo, mediaLoadData);
        });
  }
}

void BaseMediaSource::EventListenerDispatcher::loadError(const LoadEventInfo& loadEventInfo,
    const MediaLoadData& mediaLoadData,
    const Error& error,
    const bool wasCanceled) const {
  const auto lockedSource = source.lock();
  if (lockedSource == nullptr) {
    return;
  }
  const auto eventWindowIndex = windowIndex;
  const auto eventMediaPeriodId = mediaPeriodId;
  for (const auto& listenerAndHandler : lockedSource->getEventListeners()) {
    const auto eventListener = listenerAndHandler->eventListener;
    // The error is captured by value: the listener is called on the thread of its handler, i.e.
    // after this call returned, so it cannot be given the address of a temporary.
    dispatchMediaSourceEvent(listenerAndHandler->eventHandler,
        [eventListener, eventWindowIndex, eventMediaPeriodId, loadEventInfo, mediaLoadData, error,
            wasCanceled] {
          eventListener->onLoadError(eventWindowIndex,
              eventMediaPeriodId,
              loadEventInfo,
              mediaLoadData,
              &error,
              wasCanceled);
        });
  }
}

void BaseMediaSource::EventListenerDispatcher::upstreamDiscarded(
    const MediaLoadData& mediaLoadData) const {
  if (!mediaPeriodId.has_value()) {
    return;
  }
  const auto lockedSource = source.lock();
  if (lockedSource == nullptr) {
    return;
  }
  const auto eventWindowIndex = windowIndex;
  const auto eventMediaPeriodId = mediaPeriodId.value();
  for (const auto& listenerAndHandler : lockedSource->getEventListeners()) {
    const auto eventListener = listenerAndHandler->eventListener;
    dispatchMediaSourceEvent(listenerAndHandler->eventHandler,
        [eventListener, eventWindowIndex, eventMediaPeriodId, mediaLoadData] {
          eventListener->onUpstreamDiscarded(eventWindowIndex, eventMediaPeriodId, mediaLoadData);
        });
  }
}

void BaseMediaSource::EventListenerDispatcher::downstreamFormatChanged(
    const MediaLoadData& mediaLoadData) const {
  const auto lockedSource = source.lock();
  if (lockedSource == nullptr) {
    return;
  }
  const auto eventWindowIndex = windowIndex;
  const auto eventMediaPeriodId = mediaPeriodId;
  for (const auto& listenerAndHandler : lockedSource->getEventListeners()) {
    const auto eventListener = listenerAndHandler->eventListener;
    dispatchMediaSourceEvent(listenerAndHandler->eventHandler,
        [eventListener, eventWindowIndex, eventMediaPeriodId, mediaLoadData] {
          eventListener->onDownstreamFormatChanged(
              eventWindowIndex, eventMediaPeriodId, mediaLoadData);
        });
  }
}

} // namespace coral
