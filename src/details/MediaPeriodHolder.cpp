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
#include "MediaPeriodHolder.h"

#include <coral/selection/DefaultTrackSelector.h>
#include <coral/source/ClippingMediaPeriod.h>
#include <coral/source/MergingMediaPeriod.h>
#include <coral/source/EmptySampleStream.h>

#include <cassert>
#include <memory>
#include <utility>

namespace coral {

MediaPeriodHolder::MediaPeriodHolder(
  const std::vector<std::shared_ptr<SelectionCapabilities>>& selectionCapabilities,
  const int64_t rendererPositionOffsetUs,
  const std::shared_ptr<TrackSelector>& trackSelector,
  const std::shared_ptr<Allocator>& allocator,
  const std::shared_ptr<MediaSourceList>& mediaSourceList,
  const MediaPeriodInfo& info,
  const std::shared_ptr<TrackSelectorResult>& emptyTrackSelectorResult) noexcept
  : uid(info.id.periodUid()),
    info(info),
    prepareCalled(false),
    prepared(false),
    enabledTracks(false),
    allRenderersEnabled(false),
    selectionCapabilities(selectionCapabilities),
    trackSelector(trackSelector),
    mediaSourceList(mediaSourceList),
    trackSelectorResult(emptyTrackSelectorResult),
    rendererPositionOffsetUs(rendererPositionOffsetUs) {
  sampleStreams.resize(selectionCapabilities.size(), nullptr);
  mayRetainStreamFlags.resize(selectionCapabilities.size(), false);
  mediaPeriod = createMediaPeriod(info.id,
    mediaSourceList,
    allocator,
    info.startPositionUs,
    info.endPositionUs);
}

MediaPeriodHolder::~MediaPeriodHolder() {
  release();
  mediaPeriod.reset();
  mayRetainStreamFlags.clear();
  std::vector<std::shared_ptr<SampleStream>> streams;
  sampleStreams.swap(streams);
  sampleStreams.clear();
  trackSelectorResult.reset();
  prepared = false;
  enabledTracks = false;
}

void MediaPeriodHolder::setRendererOffset(int64_t offsetUs) {
  rendererPositionOffsetUs = offsetUs;
}

int64_t MediaPeriodHolder::getBufferedPositionUs() {
  if (!prepared) {
    return info.startPositionUs;
  }
  auto bufferedPositionUs =
    enabledTracks ? mediaPeriod->getBufferedPositionUs() : Definitions::kTimeEndOfSource;
  return bufferedPositionUs == Definitions::kTimeEndOfSource ? info.durationUs : bufferedPositionUs;
}

int64_t MediaPeriodHolder::getNextLoadPositionUs() {
  return !prepared ? 0 : mediaPeriod->getNextLoadPositionUs();
}

const std::shared_ptr<SampleStream>& MediaPeriodHolder::getSampleStream(size_t index) const {
  if (index >= sampleStreams.size()) {
    static const std::shared_ptr<SampleStream> sEmptyStream = nullptr;
    return sEmptyStream;
  }
  return sampleStreams[index];
}

void MediaPeriodHolder::updateSampleStream(size_t index,
  const std::shared_ptr<SampleStream>& stream) {
  if (index >= sampleStreams.size()) {
    return;
  }
  if (sampleStreams[index] != stream) {
    sampleStreams[index] = stream;
    mayRetainStreamFlags[index] = stream != nullptr;
  }
}

bool MediaPeriodHolder::isFullyBuffered() const {
  return prepared && (
           !enabledTracks
           || mediaPeriod->getBufferedPositionUs() == Definitions::kTimeEndOfSource);
}

void MediaPeriodHolder::handlePrepared(
  float playbackSpeed, const std::shared_ptr<Timeline>& timeline, bool playWhenReady) {
  prepared = true;
  trackGroups = mediaPeriod->getTrackGroups();
  auto selectorResult = selectTracks(playbackSpeed, timeline, playWhenReady);
  auto requestedStartPositionUs = info.startPositionUs;
  if (info.durationUs != Definitions::kTimeUnset && requestedStartPositionUs > info.durationUs) {
    // Make sure start position doesn't exceed period duration.
    requestedStartPositionUs = std::max(int64_t(0), info.durationUs - 1);
  }
  auto newStartPositionUs =
    applyTrackSelection(selectorResult, requestedStartPositionUs, false);
  info.startPositionUs = newStartPositionUs;
}

void MediaPeriodHolder::reevaluateBuffer(int64_t rendererPositionUs) {
  assert(isLoadingMediaPeriod());
  if (prepared) {
    mediaPeriod->reevaluateBuffer(toPeriodTime(rendererPositionUs));
  }
}

void MediaPeriodHolder::continueLoading(int64_t positionUs) {
  assert(isLoadingMediaPeriod());
  mediaPeriod->continueLoading(positionUs);
}

std::shared_ptr<TrackSelectorResult>
MediaPeriodHolder::selectTracks(
  float playbackSpeed, const std::shared_ptr<Timeline>& timeline, bool playWhenReady) {
  auto selectorResult = trackSelector->selectTracks(selectionCapabilities, getTrackGroups());
  for (auto i = 0; i < selectorResult->length(); ++i) {
    if (selectorResult->isEnabled(i)) {
      assert(selectorResult->getSelection(i) != nullptr
        || selectionCapabilities[i]->getMediaType() == MediaType::kMediaTypeInvalid);
    } else {
      assert(selectorResult->getSelection(i) == nullptr);
    }
  }
  for (int i = 0; i < selectorResult->length(); ++i) {
    auto trackSelection = selectorResult->getSelection(i);
    if (trackSelection != nullptr) {
      trackSelection->onPlaybackSpeed(playbackSpeed);
      trackSelection->onPlayWhenReadyChanged(playWhenReady);
    }
  }
  return selectorResult;
}

int64_t MediaPeriodHolder::applyTrackSelection(
  const std::shared_ptr<TrackSelectorResult>& newTrackSelectorResult,
  int64_t positionUs,
  bool forceRecreateStreams) {
  auto streamResetFlags = std::vector<bool>(selectionCapabilities.size());
  return applyTrackSelection(newTrackSelectorResult, positionUs, forceRecreateStreams,
    streamResetFlags);
}

int64_t MediaPeriodHolder::applyTrackSelection(
  const std::shared_ptr<TrackSelectorResult>& newTrackSelectorResult,
  int64_t positionUs,
  bool forceRecreateStreams,
  std::vector<bool>& streamResetFlags) {
  if (released || mediaPeriod == nullptr) {
    // The holder was released; selecting tracks on the released period would dereference streams
    // whose queues no longer exist.
    return positionUs;
  }
  for (auto i = 0; i < newTrackSelectorResult->length(); ++i) {
    mayRetainStreamFlags[i] =
      !forceRecreateStreams && newTrackSelectorResult->isEquivalent(trackSelectorResult, i);
  }

  // Undo the effect of previous call to associate no-sample renderers with empty tracks
  // so the mediaPeriod receives back whatever it sent us before.
  disassociateNoSampleRenderersWithEmptySampleStream(sampleStreams);
  disableTrackSelectionsInResult();
  trackSelectorResult = newTrackSelectorResult;
  enableTrackSelectionsInResult();
  // Disable streams on the period and get new streams for updated/newly-enabled tracks.
  auto const& trackSelections = newTrackSelectorResult->getSelections();
  positionUs =
    mediaPeriod->selectTracks(
      trackSelections->getAll(),
      mayRetainStreamFlags,
      sampleStreams,
      streamResetFlags,
      positionUs);
  associateNoSampleRenderersWithEmptySampleStream(sampleStreams);

  // Update whether we have enabled tracks and check that the expected streams are non-null.
  enabledTracks = false;
  for (size_t i = 0; i < sampleStreams.size(); ++i) {
    if (sampleStreams[i] != nullptr) {
      assert(newTrackSelectorResult->isEnabled(i));
      // hasEnabledTracks should be true only when non-empty streams exists.
      if (selectionCapabilities[i]->getMediaType() != MediaType::kMediaTypeInvalid) {
        enabledTracks = true;
      }
    } else {
      assert(trackSelections->get(i) == nullptr);
    }
  }
  return positionUs;
}

void MediaPeriodHolder::release() {
  if (released) {
    return;
  }
  released = true;
  disableTrackSelectionsInResult();
  releaseMediaPeriod(mediaSourceList, mediaPeriod);
  // Drop the wrapper and its sample streams with the period: a later selection or renderer release
  // must not dereference SampleStreams whose SampleQueues have already been freed.
  mediaPeriod.reset();
  sampleStreams.clear();
  mayRetainStreamFlags.clear();
  prepared = false;
  enabledTracks = false;
}

void MediaPeriodHolder::setNext(const std::shared_ptr<MediaPeriodHolder>& nextMediaPeriodHolder) {
  if (nextMediaPeriodHolder == nextHolder) {
    return;
  }
  disableTrackSelectionsInResult();
  nextHolder = nextMediaPeriodHolder;
  enableTrackSelectionsInResult();
}

void MediaPeriodHolder::updateClipping() {
  auto clippingPeriod = std::dynamic_pointer_cast<ClippingMediaPeriod>(mediaPeriod);
  if (clippingPeriod != nullptr) {
    auto endPositionUs =
      info.endPositionUs == Definitions::kTimeUnset
        ? Definitions::kTimeEndOfSource
        : info.endPositionUs;
    clippingPeriod->updateClipping(0, endPositionUs);
  }
}

void MediaPeriodHolder::enableTrackSelectionsInResult() {
  // Enabling and disabling the selections is only done for the loading period, so that the pair
  // stays balanced: every enable() has exactly one matching disable() when this holder stops being
  // the loading one. ExoPlayer guards both methods the same way.
  if (!isLoadingMediaPeriod()) {
    return;
  }
  for (auto i = 0; i < trackSelectorResult->length(); ++i) {
    auto const& rendererEnabled = trackSelectorResult->isEnabled(i);
    auto& trackSelection = trackSelectorResult->getSelection(i);
    if (rendererEnabled && trackSelection != nullptr) {
      trackSelection->enable();
    }
  }
}

void MediaPeriodHolder::disableTrackSelectionsInResult() {
  if (!isLoadingMediaPeriod()) {
    return;
  }
  for (auto i = 0; i < trackSelectorResult->length(); ++i) {
    auto const& rendererEnabled = trackSelectorResult->isEnabled(i);
    auto& trackSelection = trackSelectorResult->getSelection(i);
    if (rendererEnabled && trackSelection != nullptr) {
      trackSelection->disable();
    }
  }
}

void MediaPeriodHolder::disassociateNoSampleRenderersWithEmptySampleStream(
  std::vector<std::shared_ptr<SampleStream>>& streams) {
  for (size_t i = 0; i < selectionCapabilities.size(); ++i) {
    if (selectionCapabilities[i]->getMediaType() == MediaType::kMediaTypeInvalid) {
      streams[i] = nullptr;
    }
  }
}

void MediaPeriodHolder::associateNoSampleRenderersWithEmptySampleStream(
  std::vector<std::shared_ptr<SampleStream>>& streams) {
  for (size_t i = 0; i < selectionCapabilities.size(); ++i) {
    if (selectionCapabilities[i]->getMediaType() == MediaType::kMediaTypeInvalid
        && trackSelectorResult->isEnabled(i)) {
      streams[i] = std::make_shared<EmptySampleStream>(MediaType::kMediaTypeInvalid);
    }
  }
}

std::shared_ptr<MediaPeriod>
MediaPeriodHolder::createMediaPeriod(const MediaPeriodId& id,
  const std::shared_ptr<MediaSourceList>& mediaSourceList,
  const std::shared_ptr<Allocator>& allocator,
  const int64_t startPositionUs,
  const int64_t endPositionUs) {
  auto mediaPeriod =
    mediaSourceList->createPeriod(id, allocator, startPositionUs);
  if (endPositionUs != Definitions::kTimeUnset) {
    mediaPeriod = std::make_shared<ClippingMediaPeriod>(mediaPeriod, true, 0, endPositionUs);
  }
  return mediaPeriod;
}

void MediaPeriodHolder::releaseMediaPeriod(std::shared_ptr<MediaSourceList>& mediaSourceList,
  std::shared_ptr<MediaPeriod>& mediaPeriod) {
  auto clippingPeriod = std::dynamic_pointer_cast<ClippingMediaPeriod>(mediaPeriod);
  if (clippingPeriod != nullptr) {
    mediaSourceList->releasePeriod(clippingPeriod->getMediaPeriod());
    clippingPeriod->releaseChildPeriod();
  } else {
    if (auto mergingPeriod = std::dynamic_pointer_cast<MergingMediaPeriod>(mediaPeriod);
        mergingPeriod != nullptr) {
      mediaSourceList->releasePeriod(mediaPeriod);
      mergingPeriod->releaseChildPeriods();
      return;
    }
    mediaSourceList->releasePeriod(mediaPeriod);
  }
}

void MediaPeriodHolder::prepare(MediaPeriod::Callback* callback, int64_t startPositionUs) {
  prepareCalled = true;
  mediaPeriod->prepare(callback, startPositionUs);
}

bool MediaPeriodHolder::canBeUsedForMediaPeriodInfo(const MediaPeriodInfo& other) const {
  return (info.durationUs == Definitions::kTimeUnset || info.durationUs == other.durationUs)
         && info.startPositionUs == other.startPositionUs;
}

} // namespace coral
