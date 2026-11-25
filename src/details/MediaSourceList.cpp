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
#include "MediaSourceList.h"
#include "PlaylistTimeline.h"
#include "UidGenerator.h"

#include <cassert>

namespace coral {

MediaSourceHolder::MediaSourceHolder(const std::shared_ptr<MediaSource>& childSource, bool useLazyPreparation) noexcept
  : uid(newUidString()),
    firstWindowIndexInChild(0),
    isRemoved(false) {
  mediaSource = std::make_shared<MaskingMediaSource>(childSource, useLazyPreparation);
}

MediaSourceHolder::~MediaSourceHolder() {
  mediaSource.reset();
}

void MediaSourceHolder::reset(const int firstIndexInChild) {
  this->firstWindowIndexInChild = firstIndexInChild;
  isRemoved = false;
  activeMediaPeriodIds.clear();
}

//--------------------------------------------------------------------------------------------------

MediaSourceList::MediaSourceList(MediaSourceListInfoRefreshListener* listener) noexcept
  : mediaSourceListInfoListener(listener),
    prepared(false) {
  shuffleOrder = std::make_shared<DefaultShuffleOrder>(0);
}

MediaSourceList::~MediaSourceList() {
  // The listener goes first: releasing a child source may make it refresh its timeline, which must
  // not reach this list any more.
  mediaSourceListInfoListener = nullptr;
  // The child sources hold a caller that points back at this list, so they have to be released
  // before the list is gone: a source that was left prepared would report its next timeline or load
  // event into freed memory. `release` is idempotent, so a caller that already released the list is
  // not a problem.
  release();
  mediaSourceHolders.erase(mediaSourceHolders.begin(), mediaSourceHolders.end());
  std::unordered_map<PeriodUid, std::shared_ptr<MediaSourceHolder>> emptyHolder;
  mediaSourceByUid.swap(emptyHolder);
}

std::shared_ptr<Timeline>
MediaSourceList::setMediaSources(const std::vector<std::shared_ptr<MediaSourceHolder>>& holders,
  const std::shared_ptr<ShuffleOrder>& shuffleOrder) {
  const auto length = static_cast<int>(mediaSourceHolders.size());
  removeMediaSourcesInternal(0, length);
  return addMediaSources(length, holders, shuffleOrder);
}

std::shared_ptr<Timeline>
MediaSourceList::addMediaSources(const int index,
  const std::vector<std::shared_ptr<MediaSourceHolder>>& holders,
  const std::shared_ptr<ShuffleOrder>& shuffleOrder) {
  if (!holders.empty()) {
    this->shuffleOrder = shuffleOrder;
    for (int insertionIndex = index; insertionIndex < index + holders.size(); insertionIndex++) {
      auto const& holder = holders[insertionIndex - index];
      if (insertionIndex > 0) {
        auto const& previousHolder = mediaSourceHolders[insertionIndex - 1];
        auto const& previousTimeline = previousHolder->mediaSource->getTimeline();
        holder->reset(
          /* firstWindowIndexInChild= */ previousHolder->firstWindowIndexInChild
                                         + previousTimeline->getWindowCount());
      } else {
        holder->reset(/* firstWindowIndexInChild= */ 0);
      }
      auto const& newTimeline = holder->mediaSource->getTimeline();
      correctOffsets(
        /* startIndex= */ insertionIndex,
        /* windowOffsetUpdate= */ newTimeline->getWindowCount());
      if (insertionIndex >= mediaSourceHolders.size()) {
        mediaSourceHolders.push_back(holder);
      } else {
        mediaSourceHolders.insert(mediaSourceHolders.begin() + insertionIndex, holder);
      }
      mediaSourceByUid[holder->uid] = holder;
      if (isPrepared()) {
        prepareChildSource(holder);
        if (mediaSourceByMediaPeriod.empty()) {
          enabledMediaSourceHolders.push_back(holder);
        } else {
          disableChildSource(holder);
        }
      }
    }
  }
  return createTimeline();
}

std::shared_ptr<Timeline> MediaSourceList::clear(const std::shared_ptr<ShuffleOrder>& newOrder) {
  shuffleOrder = newOrder != nullptr ? newOrder : shuffleOrder->cloneAndClear();
  removeMediaSourcesInternal(0, getSize());
  return createTimeline();
}

std::shared_ptr<Timeline> MediaSourceList::removeMediaSources(const int fromIndex,
  const int toIndex, const std::shared_ptr<ShuffleOrder>& newOrder) {
  shuffleOrder = newOrder != nullptr ? newOrder : shuffleOrder->cloneAndRemove(fromIndex, toIndex);
  removeMediaSourcesInternal(fromIndex, toIndex);
  return createTimeline();
}

std::shared_ptr<Timeline> MediaSourceList::moveMediaSources(const int fromIndex,
  const int toIndex, const int newIndex, const std::shared_ptr<ShuffleOrder>& newOrder) {
  // The shuffle order is only replaced, never reordered: the child sources keep the order they were
  // shuffled into, and only their position in the playlist changes.
  if (newOrder != nullptr) {
    shuffleOrder = newOrder;
  }
  assert(fromIndex <= toIndex && newIndex <= getSize());

  auto moved = std::vector<std::shared_ptr<MediaSourceHolder>>(
    mediaSourceHolders.begin() + fromIndex, mediaSourceHolders.begin() + toIndex);
  mediaSourceHolders.erase(mediaSourceHolders.begin() + fromIndex,
    mediaSourceHolders.begin() + toIndex);
  mediaSourceHolders.insert(mediaSourceHolders.begin() + newIndex, moved.begin(), moved.end());

  // The first window index of a holder is the number of windows the holders before it own, so moving
  // a range changes it for everything between the two positions. Recomputing from the front is both
  // simpler and less error prone than adding the two offset updates ExoPlayer sums, and it leaves the
  // list provably consistent.
  auto firstWindowIndex = 0;
  for (auto const& holder : mediaSourceHolders) {
    holder->firstWindowIndexInChild = firstWindowIndex;
    firstWindowIndex += holder->mediaSource->getTimeline()->getWindowCount();
  }
  return createTimeline();
}

std::shared_ptr<Timeline>
MediaSourceList::setShuffleOrder(const std::shared_ptr<ShuffleOrder>& order) {
  auto size = getSize();
  if (order->getLength() != size) {
    shuffleOrder = order->cloneAndClear()->cloneAndInsert(0, size);
  }
  return createTimeline();
}

void MediaSourceList::prepare() {
  assert(!prepared);
  for (auto i = 0; i < mediaSourceHolders.size(); ++i) {
    auto const& mediaSourceHolder = mediaSourceHolders[i];
    prepareChildSource(mediaSourceHolder);
    enabledMediaSourceHolders.push_back(mediaSourceHolder);
  }
  prepared = true;
}

std::shared_ptr<MediaPeriod>
MediaSourceList::createPeriod(MediaPeriodId id, const std::shared_ptr<Allocator>& allocator,
  int64_t startPositionUs) {
  auto mediaSourceHolderUid = getMediaSourceHolderUid(id.periodUid());
  auto childMediaPeriodId = id.copyWithPeriodUid(getChildPeriodUid(id.periodUid()).value());
  auto const& holder = mediaSourceByUid[mediaSourceHolderUid.value()];
  enableMediaSource(holder);
  holder->activeMediaPeriodIds.push_back(childMediaPeriodId);
  auto const& mediaPeriod =
    holder->mediaSource->createPeriod(childMediaPeriodId, allocator, startPositionUs);
  mediaSourceByMediaPeriod
    .push_back(std::make_shared<MediaPeriodAndSourceHolder>(mediaPeriod, holder));
  disableUnusedMediaSources();
  // The period exists, so its creation is reported with the identifier the playlist uses for it.
  notifyMediaPeriodCreated(id);
  return mediaPeriod;
}

void MediaSourceList::releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) {
  auto position = mediaSourceByMediaPeriod.begin();
  while (position != mediaSourceByMediaPeriod.end()) {
    if (position->get()->mediaPeriod != mediaPeriod) {
      ++position;
    } else {
      break;
    }
  }
  if (position != mediaSourceByMediaPeriod.end()) {
    // Keep the holder alive across the erase: the entry owns the only reference to the object the
    // holder is read from, so erasing it first would leave `holder` dangling.
    auto holder = position->get()->mediaSourceHolder;
    mediaSourceByMediaPeriod.erase(position);
    // The identifier of the period is resolved before the period is released, because the release
    // forgets which identifiers the holder is active for.
    auto maskingMediaPeriod = std::dynamic_pointer_cast<MaskingMediaPeriod>(mediaPeriod);
    auto playlistMediaPeriodId = maskingMediaPeriod != nullptr
                                 ? getMediaPeriodIdForChildMediaPeriodId(
                                       holder, maskingMediaPeriod->getId())
                                 : std::nullopt;
    holder->mediaSource->releasePeriod(mediaPeriod);
    if (maskingMediaPeriod != nullptr) {
      auto ptr = std::find(holder->activeMediaPeriodIds.begin(),
        holder->activeMediaPeriodIds.end(),
        maskingMediaPeriod->getId());
      if (ptr != holder->activeMediaPeriodIds.end()) {
        holder->activeMediaPeriodIds.erase(ptr);
      }
    }
    if (!mediaSourceByMediaPeriod.empty()) {
      disableUnusedMediaSources();
    }
    maybeReleaseChildSource(holder);
    if (playlistMediaPeriodId.has_value()) {
      notifyMediaPeriodReleased(playlistMediaPeriodId.value());
    }
  }
}

void MediaSourceList::release() {
  auto position = childSources.begin();
  while (position != childSources.end()) {
    unregisterChildEventListeners(*position);
    position->get()->holder->mediaSource->releaseSource(position->get()->caller);
    position = childSources.erase(position);
  }
  childSources.clear();
  std::vector<std::shared_ptr<MediaSourceHolder>> emptyHolders{};
  enabledMediaSourceHolders.swap(emptyHolders);
  prepared = false;
}

std::shared_ptr<Timeline> MediaSourceList::createTimeline() {
  if (mediaSourceHolders.empty()) {
    return std::make_shared<EmptyTimeline>();
  }
  int windowOffset = 0;
  for (const auto& mediaSourceHolder: mediaSourceHolders) {
    mediaSourceHolder->firstWindowIndexInChild = windowOffset;
    windowOffset += mediaSourceHolder->mediaSource->getTimeline()->getWindowCount();
  }
  return std::make_shared<PlaylistTimeline>(mediaSourceHolders, shuffleOrder);
}

void MediaSourceList::enableMediaSource(const std::shared_ptr<MediaSourceHolder>& holder) {
  enabledMediaSourceHolders.push_back(holder);
  auto position = childSources.begin();
  while (position != childSources.end()) {
    if (position->get()->holder == holder) {
      auto const& enabledChild = position->get();
      if (enabledChild != nullptr) {
        enabledChild->mediaSource->enable(enabledChild->caller);
      }
      break;
    } else {
      ++position;
    }
  }
}

void MediaSourceList::disableUnusedMediaSources() {
  auto position = enabledMediaSourceHolders.begin();
  while (position != enabledMediaSourceHolders.end()) {
    if (position->get()->activeMediaPeriodIds.empty()) {
      disableChildSource(*position);
      position = enabledMediaSourceHolders.erase(position);
    } else {
      ++position;
    }
  }
}

void MediaSourceList::disableChildSource(const std::shared_ptr<MediaSourceHolder>& holder) {
  auto position = childSources.begin();
  while (position != childSources.end()) {
    if (position->get()->holder == holder) {
      auto disableChild = position->get();
      if (disableChild != nullptr) {
        disableChild->mediaSource->disable(disableChild->caller);
      }
      break;
    } else {
      ++position;
    }
  }
}

void MediaSourceList::removeMediaSourcesInternal(int fromIndex, int toIndex) {
  for (int index = toIndex - 1; index >= fromIndex; --index) {
    auto const& holder = mediaSourceHolders[index];
    auto position = mediaSourceByUid.find(holder->uid);
    if (position != mediaSourceByUid.end()) {
      mediaSourceByUid.erase(position);
    }
    auto const& oldTimeline = holder->mediaSource->getTimeline();
    correctOffsets(
      /* startIndex= */ index, /* windowOffsetUpdate= */ -oldTimeline->getWindowCount());
    holder->isRemoved = true;
    if (isPrepared()) {
      maybeReleaseChildSource(holder);
    }
  }
  mediaSourceHolders
    .erase(mediaSourceHolders.begin() + fromIndex, mediaSourceHolders.begin() + toIndex);
}

void MediaSourceList::correctOffsets(int startIndex, int windowOffsetUpdate) {
  for (auto i = startIndex; i < mediaSourceHolders.size(); i++) {
    auto const& mediaSourceHolder = mediaSourceHolders[i];
    mediaSourceHolder->firstWindowIndexInChild += windowOffsetUpdate;
  }
}

std::optional<MediaPeriodId>
MediaSourceList::getMediaPeriodIdForChildMediaPeriodId(
  const std::shared_ptr<MediaSourceHolder>& mediaSourceHolder, MediaPeriodId mediaPeriodId) {
  for (auto i = 0; i < mediaSourceHolder->activeMediaPeriodIds.size(); ++i) {
    // Ensure the reported media period id has the same window sequence number as the one created
    // by this media source. Otherwise it does not belong to this child source.
    if (mediaSourceHolder->activeMediaPeriodIds[i].windowSequenceNumber()
        == mediaPeriodId.windowSequenceNumber()) {
      auto const& periodUid = getPeriodUid(mediaSourceHolder, mediaPeriodId.periodUid());
      return mediaPeriodId.copyWithPeriodUid(periodUid.value());
    }
  }
  return std::nullopt;
}

int MediaSourceList::getWindowIndexForChildWindowIndex(
  const std::shared_ptr<MediaSourceHolder>& mediaSourceHolder, int windowIndex) {
  // A source reports an unset window index when it does not know where its windows end up in the
  // playlist, which is the case for every source of this list. The index of the first window the
  // source owns is the useful answer for the application, so unset is mapped to it rather than
  // reported as unset.
  if (windowIndex == Definitions::kIndexUnset) {
    return mediaSourceHolder->firstWindowIndexInChild;
  }
  return windowIndex + mediaSourceHolder->firstWindowIndexInChild;
}

void MediaSourceList::prepareChildSource(const std::shared_ptr<MediaSourceHolder>& holder) {
  auto const& mediaSource = holder->mediaSource;
  auto caller = std::make_shared<MediaSourceCallerImpl>(this);
  auto source = std::make_shared<MediaSourceHolderAndListener>(holder, mediaSource, caller);
  childSources.push_back(source);
  // The listener has to be registered before the source is prepared: the events of the preparation
  // itself, e.g. the load of the header, are already reported.
  for (const auto& listenerAndHandler: eventListeners) {
    registerChildEventListener(source, listenerAndHandler->eventHandler,
      listenerAndHandler->eventListener);
  }
  mediaSource->prepareSource(caller);
}

int MediaSourceList::getWindowIndexForPeriodId(const MediaPeriodId& mediaPeriodId) const {
  const auto holderUid = getMediaSourceHolderUid(mediaPeriodId.periodUid());
  if (!holderUid.has_value()) {
    return Definitions::kIndexUnset;
  }
  const auto position = mediaSourceByUid.find(holderUid.value());
  if (position == mediaSourceByUid.end()) {
    return Definitions::kIndexUnset;
  }
  const auto& holder = position->second;
  const auto& childTimeline = holder->mediaSource->getTimeline();
  const auto childPeriodUid = getChildPeriodUid(mediaPeriodId.periodUid());
  if (childTimeline == nullptr || !childPeriodUid.has_value()) {
    // A child source that knows no timeline yet still owns the windows it was given a place for.
    return holder->firstWindowIndexInChild;
  }
  const auto childPeriodIndex = childTimeline->getIndexOfPeriod(childPeriodUid.value());
  if (childPeriodIndex == Definitions::kIndexUnset) {
    return holder->firstWindowIndexInChild;
  }
  const auto childPeriod = childTimeline->getPeriod(childPeriodIndex,
      std::make_shared<Timeline::Period>(), /* setIds= */ false);
  if (childPeriod == nullptr) {
    return holder->firstWindowIndexInChild;
  }
  // The child timeline says which of its windows the period belongs to, and the holder says where
  // those windows are in the playlist.
  return getWindowIndexForChildWindowIndex(holder, childPeriod->windowIndex);
}

void MediaSourceList::notifyMediaPeriodCreated(const MediaPeriodId& mediaPeriodId) {
  const auto windowIndex = getWindowIndexForPeriodId(mediaPeriodId);
  for (const auto& listenerAndHandler : eventListeners) {
    const auto listener = listenerAndHandler->eventListener;
    // The identifier is copied into the closure because it may be delivered later, on the handler of
    // the listener.
    dispatchMediaSourceEvent(listenerAndHandler->eventHandler,
        [listener, windowIndex, mediaPeriodId] {
          listener->onMediaPeriodCreated(windowIndex, mediaPeriodId);
        });
  }
}

void MediaSourceList::notifyMediaPeriodReleased(const MediaPeriodId& mediaPeriodId) {
  const auto windowIndex = getWindowIndexForPeriodId(mediaPeriodId);
  for (const auto& listenerAndHandler : eventListeners) {
    const auto listener = listenerAndHandler->eventListener;
    dispatchMediaSourceEvent(listenerAndHandler->eventHandler,
        [listener, windowIndex, mediaPeriodId] {
          listener->onMediaPeriodReleased(windowIndex, mediaPeriodId);
        });
  }
}

void MediaSourceList::registerChildEventListener(
  const std::shared_ptr<MediaSourceHolderAndListener>& childSource,
  const std::shared_ptr<EventHandler>& eventHandler,
  const std::shared_ptr<MediaSourceEventListener>& listener) {
  auto forwarder = std::make_shared<ForwardingEventListener>(childSource->holder, listener);
  childSource->eventListeners.push_back(
    std::make_shared<ChildEventListener>(eventHandler, listener, forwarder));
  childSource->mediaSource->addEventListener(eventHandler, forwarder);
}

void MediaSourceList::unregisterChildEventListeners(
  const std::shared_ptr<MediaSourceHolderAndListener>& childSource) {
  for (const auto& registration: childSource->eventListeners) {
    childSource->mediaSource->removeEventListener(registration->forwarder);
  }
  childSource->eventListeners.clear();
}

void MediaSourceList::addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
  const std::shared_ptr<MediaSourceEventListener>& eventListener) {
  // The registration is remembered so that the child sources that are prepared later are covered
  // too, and applied to the child sources that are already prepared.
  eventListeners.push_back(
    std::make_shared<EventListenerAndHandler>(eventHandler, eventListener));
  for (const auto& childSource: childSources) {
    registerChildEventListener(childSource, eventHandler, eventListener);
  }
}

void MediaSourceList::removeEventListener(
  const std::shared_ptr<MediaSourceEventListener>& eventListener) {
  auto position = eventListeners.begin();
  while (position != eventListeners.end()) {
    if (position->get()->eventListener == eventListener) {
      position = eventListeners.erase(position);
    } else {
      ++position;
    }
  }
  for (const auto& childSource: childSources) {
    auto& registrations = childSource->eventListeners;
    auto registration = registrations.begin();
    while (registration != registrations.end()) {
      if (registration->get()->target == eventListener) {
        childSource->mediaSource->removeEventListener(registration->get()->forwarder);
        registration = registrations.erase(registration);
      } else {
        ++registration;
      }
    }
  }
}

void MediaSourceList::maybeReleaseChildSource(const std::shared_ptr<MediaSourceHolder>& holder) {
  // Release if the source has been removed from the playlist and no periods are still active.
  if (holder->isRemoved && holder->activeMediaPeriodIds.empty()) {
    auto position = childSources.begin();
    while (position != childSources.end()) {
      if (position->get()->holder == holder) {
        break;
      } else {
        ++position;
      }
    }
    if (position != childSources.end()) {
      auto const& removedChild = *position;
      unregisterChildEventListeners(removedChild);
      removedChild->mediaSource->releaseSource(removedChild->caller);
    }
    auto index =
      std::find(enabledMediaSourceHolders.begin(), enabledMediaSourceHolders.end(), holder);
    if (index != enabledMediaSourceHolders.end()) {
      enabledMediaSourceHolders.erase(index);
    }
  }
}

std::optional<PeriodUid> MediaSourceList::getMediaSourceHolderUid(const PeriodUid& periodUid) {
  return PlaylistTimeline::getChildTimelineUidFromConcatenatedUid(periodUid);
}

std::optional<PeriodUid> MediaSourceList::getChildPeriodUid(const PeriodUid& periodUid) {
  return PlaylistTimeline::getChildPeriodUidFromConcatenatedUid(periodUid);
}

std::optional<PeriodUid>
MediaSourceList::getPeriodUid(const std::shared_ptr<MediaSourceHolder>& holder,
  const PeriodUid& childPeriodUid) {
  return PlaylistTimeline::getConcatenatedUid(holder->uid, childPeriodUid);
}

//--------------------------------------------------------------------------------------------------

std::optional<MediaPeriodId> MediaSourceList::ForwardingEventListener::toPlaylistMediaPeriodId(
  const MediaPeriodId& childMediaPeriodId) const {
  auto const& lockedHolder = holder.lock();
  if (lockedHolder == nullptr) {
    return std::nullopt;
  }
  return getMediaPeriodIdForChildMediaPeriodId(lockedHolder, childMediaPeriodId);
}

void MediaSourceList::ForwardingEventListener::onMediaPeriodCreated(
    const int windowIndex, const std::optional<MediaPeriodId>& mediaPeriodId) {
  static_cast<void>(windowIndex);
  static_cast<void>(mediaPeriodId);
  // The creation and the release of the periods of a child source are reported by the playlist
  // itself, which is the one that creates and releases them, so they are not forwarded.
}

void MediaSourceList::ForwardingEventListener::onMediaPeriodReleased(
    const int windowIndex, const std::optional<MediaPeriodId>& mediaPeriodId) {
  static_cast<void>(windowIndex);
  static_cast<void>(mediaPeriodId);
}

void MediaSourceList::ForwardingEventListener::onLoadStarted(const int windowIndex,
  const std::optional<MediaPeriodId>& mediaPeriodId,
  const LoadEventInfo& loadEventInfo,
  const MediaLoadData& mediaLoadData) {
  auto const& lockedHolder = holder.lock();
  if (lockedHolder == nullptr) {
    return;
  }
  auto playlistMediaPeriodId = mediaPeriodId.has_value() ? toPlaylistMediaPeriodId(mediaPeriodId.value())
                                                         : std::nullopt;
  target->onLoadStarted(getWindowIndexForChildWindowIndex(lockedHolder, windowIndex),
    playlistMediaPeriodId, loadEventInfo, mediaLoadData);
}

void MediaSourceList::ForwardingEventListener::onLoadCompleted(const int windowIndex,
  const std::optional<MediaPeriodId>& mediaPeriodId,
  const LoadEventInfo& loadEventInfo,
  const MediaLoadData& mediaLoadData) {
  auto const& lockedHolder = holder.lock();
  if (lockedHolder == nullptr) {
    return;
  }
  auto playlistMediaPeriodId = mediaPeriodId.has_value()
                               ? toPlaylistMediaPeriodId(mediaPeriodId.value())
                               : std::nullopt;
  target->onLoadCompleted(getWindowIndexForChildWindowIndex(lockedHolder, windowIndex),
    playlistMediaPeriodId, loadEventInfo, mediaLoadData);
}

void MediaSourceList::ForwardingEventListener::onLoadCanceled(const int windowIndex,
  const std::optional<MediaPeriodId>& mediaPeriodId,
  const LoadEventInfo& loadEventInfo,
  const MediaLoadData& mediaLoadData) {
  auto const& lockedHolder = holder.lock();
  if (lockedHolder == nullptr) {
    return;
  }
  auto playlistMediaPeriodId = mediaPeriodId.has_value()
                               ? toPlaylistMediaPeriodId(mediaPeriodId.value())
                               : std::nullopt;
  target->onLoadCanceled(getWindowIndexForChildWindowIndex(lockedHolder, windowIndex),
    playlistMediaPeriodId, loadEventInfo, mediaLoadData);
}

void MediaSourceList::ForwardingEventListener::onLoadError(const int windowIndex,
  const std::optional<MediaPeriodId>& mediaPeriodId,
  const LoadEventInfo& loadEventInfo,
  const MediaLoadData& mediaLoadData,
  const Error* error,
  const bool wasCanceled) {
  auto const& lockedHolder = holder.lock();
  if (lockedHolder == nullptr) {
    return;
  }
  auto playlistMediaPeriodId = mediaPeriodId.has_value()
                               ? toPlaylistMediaPeriodId(mediaPeriodId.value())
                               : std::nullopt;
  target->onLoadError(getWindowIndexForChildWindowIndex(lockedHolder, windowIndex),
    playlistMediaPeriodId, loadEventInfo, mediaLoadData, error, wasCanceled);
}

void MediaSourceList::ForwardingEventListener::onUpstreamDiscarded(const int windowIndex,
  const MediaPeriodId& mediaPeriodId, const MediaLoadData& mediaLoadData) {
  auto const& lockedHolder = holder.lock();
  if (lockedHolder == nullptr) {
    return;
  }
  auto playlistMediaPeriodId = toPlaylistMediaPeriodId(mediaPeriodId);
  if (!playlistMediaPeriodId.has_value()) {
    return;
  }
  target->onUpstreamDiscarded(getWindowIndexForChildWindowIndex(lockedHolder, windowIndex),
    playlistMediaPeriodId.value(), mediaLoadData);
}

void MediaSourceList::ForwardingEventListener::onDownstreamFormatChanged(const int windowIndex,
  const std::optional<MediaPeriodId>& mediaPeriodId, const MediaLoadData& mediaLoadData) {
  auto const& lockedHolder = holder.lock();
  if (lockedHolder == nullptr) {
    return;
  }
  auto playlistMediaPeriodId = mediaPeriodId.has_value()
                               ? toPlaylistMediaPeriodId(mediaPeriodId.value())
                               : std::nullopt;
  target->onDownstreamFormatChanged(getWindowIndexForChildWindowIndex(lockedHolder, windowIndex),
    playlistMediaPeriodId, mediaLoadData);
}

//--------------------------------------------------------------------------------------------------

void MediaSourceList::MediaSourceCallerImpl::onSourceInfoRefreshed(
  const std::shared_ptr<MediaSource>& source,
  const std::shared_ptr<Timeline>& timeline) {
  if (list->mediaSourceListInfoListener) {
    list->mediaSourceListInfoListener->onPlaylistUpdateRequested();
  }
}

} // namespace coral
