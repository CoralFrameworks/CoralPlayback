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

#include <coral/source/ProgressiveMediaPeriod.h>

#include <minilog/log.h>
#include <utils/Assert.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace coral {

namespace {

/** Milliseconds from the clock the load events report, the same one the loader uses. */
int64_t currentTimeMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

/** Converts microseconds to the milliseconds a {@link MediaLoadData} reports. */
int64_t toMilliseconds(const int64_t timeUs) {
  return timeUs == Definitions::kTimeUnset ? Definitions::kTimeUnset
                                           : timeUs / Definitions::kMicrosPerMillisecond;
}

} // namespace anonymous

// ==================================================================================================
// Construction and lifetime
// ==================================================================================================

ProgressiveMediaPeriod::ProgressiveMediaPeriod(std::string url,
    std::shared_ptr<ProgressiveMediaExtractor> extractor,
    Listener* listener,
    std::shared_ptr<Allocator> allocator,
    DataSourceFactory dataSourceFactory,
    BaseMediaSource::EventListenerDispatcher eventDispatcher,
    std::shared_ptr<LoadErrorHandlingPolicy> loadErrorHandlingPolicy,
    const int64_t imageDurationUs) noexcept
    : listener(listener),
      allocator(std::move(allocator)),
      continueLoadingCheckIntervalBytes(DEFAULT_LOADING_CHECK_INTERVAL_BYTES),
      loader(std::make_unique<Loader>(nullptr, "Loader:ProgressiveMediaPeriod")),
      inputUrl(std::move(url)),
      dataSourceFactory(std::move(dataSourceFactory)),
      extractor(std::move(extractor)),
      eventDispatcher(std::move(eventDispatcher)),
      loadErrorHandlingPolicy(loadErrorHandlingPolicy != nullptr
                              ? std::move(loadErrorHandlingPolicy)
                              : std::make_shared<DefaultLoadErrorHandlingPolicy>()),
      mediaCallback(nullptr),
      sampleQueuesBuilt(false),
      prepared(false),
      haveAudioVideoTracks(false),
      mSeekMap(nullptr),
      durationUs(Definitions::kTimeUnset),
      imageDurationUs(imageDurationUs),
      isLive(false),
      seenFirstTrackSelection(false),
      notifyDiscontinuity(false),
      enabledTrackCount(0),
      length(Definitions::kLengthUnset),
      startPositionUs(0),
      lastSeekPositionUs(0),
      pendingResetPositionUs(Definitions::kTimeUnset),
      pendingDeferredRetry(false),
      extractedSamplesCountAtStartOfLoad(0),
      loadingFinished(false),
      released(false) {
  // The loader takes the looper of the thread that starts the load as its callback looper, which is
  // the thread that owns this period, so results never arrive on the loader thread.
}

ProgressiveMediaPeriod::~ProgressiveMediaPeriod() {
  release();
  listener = nullptr;
  loader.reset();
}

int64_t ProgressiveMediaPeriod::getDurationUs() const {
  return durationUs;
}

bool ProgressiveMediaPeriod::isSeekable() const {
  return mSeekMap != nullptr && mSeekMap->isSeekable();
}

void ProgressiveMediaPeriod::release() {
  if (released) {
    return;
  }
  if (prepared) {
    // Discard as much as we can synchronously. We only do this if we're prepared, since otherwise
    // the sample queues may still be written to by the loading thread.
    for (const auto& sampleQueue : sampleQueues) {
      sampleQueue->preRelease();
    }
  }
  // Mark the period as released before stopping the load, so that the loading thread does not report
  // any more continue-loading requests while it is being stopped, and so that a completion callback
  // that is already queued on the callback looper does nothing when it arrives.
  released = true;
  // `Loader::release` cancels the load and joins the loading thread, so once it returns nothing can
  // touch the sample queues, the extractor or the data source any more. Releasing them here rather
  // than from a callback is what keeps a callback that is posted but never dispatched from holding
  // the media of the period allocated: a completion callback that is still queued keeps this period
  // alive through the loadable the loader owns, and it returns early because the period is released.
  loader->release(nullptr);
  releaseResources();
  mediaCallback = nullptr;
}

void ProgressiveMediaPeriod::releaseResources() {
  if (resourcesReleased) {
    return;
  }
  resourcesReleased = true;
  for (const auto& sampleQueue : sampleQueues) {
    sampleQueue->release();
    // The queue holds a raw pointer to the output that listens to its format changes and to its
    // discarded media, and that output is destroyed with this period.
    sampleQueue->setUpstreamFormatChangeListener(nullptr);
    sampleQueue->setUpstreamDiscardedListener(nullptr);
  }
  if (extractor != nullptr) {
    extractor->release();
    extractor.reset();
  }
  if (dataSource != nullptr) {
    dataSource->close();
  }
  dataSource.reset();
  input.reset();
  loadable.reset();
  // The selections belong to the player, which owns them through the track selector result, so they
  // are only dropped here.
  trackSelections.clear();
}

// ==================================================================================================
// MediaPeriod
// ==================================================================================================

void ProgressiveMediaPeriod::prepare(MediaPeriod::Callback* callback, const int64_t positionUs) {
  mediaCallback = callback;
  if (!prepared && positionUs > 0) {
    // The playback starts in the middle of the media, so the first load has to seek there.
    startPositionUs = positionUs;
  }
  loadCondition.open();
  startLoading();
}

const std::shared_ptr<TrackGroupArray>& ProgressiveMediaPeriod::getTrackGroups() const {
  return trackState.tracks;
}

int64_t ProgressiveMediaPeriod::selectTracks(
    const std::vector<std::shared_ptr<TrackSelection>>& selections,
    const std::vector<bool>& mayRetainStreamFlags,
    std::vector<std::shared_ptr<SampleStream>>& streams,
    std::vector<bool>& streamResetFlags, int64_t positionUs) {
  const auto tracks = trackState.tracks;
  auto& trackEnabledStates = trackState.trackEnabledStates;
  const int oldEnabledTrackCount = enabledTrackCount;

  // Deselect old tracks.
  for (size_t index = 0; index < selections.size(); ++index) {
    if (streams[index] != nullptr &&
        (selections[index] == nullptr || !mayRetainStreamFlags[index])) {
      const auto track = static_cast<ProgressiveSampleStream*>(streams[index].get())->getTrack();
      CORAL_ASSERT(trackEnabledStates[track]);
      enabledTrackCount--;
      trackEnabledStates[track] = false;
      if (trackSelections.size() > static_cast<size_t>(track)) {
        trackSelections[static_cast<size_t>(track)] = nullptr;
      }
      streams[index] = nullptr;
    }
  }

  // A seek is needed for the first selection at a non-zero position, and for a selection that
  // follows one that disabled every track.
  auto seekRequired = seenFirstTrackSelection ? oldEnabledTrackCount == 0 : positionUs != 0;
  // Select new tracks.
  for (size_t index = 0; index < selections.size(); ++index) {
    if (streams[index] == nullptr && selections[index] != nullptr) {
      const auto& selection = selections[index];
      CORAL_ASSERT(selection->length() == 1);
      CORAL_ASSERT(selection->getIndexInTrackGroup(0) == 0);
      const auto trackIndex = tracks->indexOf(selection->getTrackGroup());
      CORAL_ASSERT(!trackEnabledStates[trackIndex]);
      enabledTrackCount++;
      trackEnabledStates[trackIndex] = true;
      if (trackSelections.size() < sampleQueues.size()) {
        trackSelections.resize(sampleQueues.size(), nullptr);
      }
      trackSelections[static_cast<size_t>(trackIndex)] = selection;
      const auto& sampleQueue = sampleQueues[trackIndex];
      auto mediaType = sampleQueue->getMediaType();
      if (mediaType == MediaType::kMediaTypeDefault ||
          mediaType == MediaType::kMediaTypeInvalid) {
        // The track was declared without a type, so the format of its first sample decides.
        const auto& format = sampleQueue->getUpstreamFormat();
        if (format != nullptr) {
          mediaType = format->isAudio() ? MediaType::kMediaTypeAudio
                                       : format->isVideo() ? MediaType::kMediaTypeVideo
                                                           : MediaType::kMediaTypeDefault;
        }
      }
      streams[index] = std::make_shared<ProgressiveSampleStream>(this, trackIndex, mediaType);
      streamResetFlags[index] = true;
      // If there's still a chance of avoiding a seek, try and seek within the sample queue.
      if (!seekRequired) {
        // A seek can be avoided if we're able to seek to the current playback position in the
        // sample queue, or if we haven't read anything from the queue since the previous seek (this
        // case is common for sparse tracks such as metadata tracks). In all other cases a seek is
        // required.
        seekRequired = !sampleQueue->seekTo(positionUs, /* allowTimeBeyondBuffer= */ true) &&
                       sampleQueue->getReadIndex() != 0;
      }
    }
  }

  if (enabledTrackCount == 0) {
    pendingDeferredRetry = false;
    notifyDiscontinuity = false;
    if (loader->isLoading()) {
      // Discard as much as we can synchronously.
      for (const auto& sampleQueue : sampleQueues) {
        sampleQueue->discardToEnd();
      }
      loader->cancelLoading();
    } else {
      for (const auto& sampleQueue : sampleQueues) {
        sampleQueue->reset();
      }
    }
  } else if (seekRequired) {
    positionUs = seekToUs(positionUs);
    // The renderers consuming the streams have to be reset because of the seek.
    for (size_t i = 0; i < streams.size(); ++i) {
      if (streams[i] != nullptr) {
        streamResetFlags[i] = true;
      }
    }
  }
  seenFirstTrackSelection = true;
  return positionUs;
}

void ProgressiveMediaPeriod::discardBuffer(const int64_t positionUs, const bool toKeyframe) {
  CORAL_ASSERT(prepared);
  if (isPendingReset()) {
    return;
  }
  const auto& trackEnabledStates = trackState.trackEnabledStates;
  for (size_t index = 0; index < sampleQueues.size(); ++index) {
    sampleQueues[index]->discardTo(positionUs, toKeyframe, trackEnabledStates[index]);
  }
}

int64_t ProgressiveMediaPeriod::readDiscontinuity() {
  if (notifyDiscontinuity &&
      (loadingFinished || getExtractedSamplesCount() > extractedSamplesCountAtStartOfLoad)) {
    notifyDiscontinuity = false;
    return lastSeekPositionUs;
  }
  return Definitions::kTimeUnset;
}

int64_t ProgressiveMediaPeriod::seekToUs(const int64_t positionUs) {
  const auto& trackIsAudioVideoFlags = trackState.trackIsAudioVideoFlags;
  notifyDiscontinuity = false;
  lastSeekPositionUs = positionUs;
  if (isPendingReset()) {
    // A reset is already pending, only its position has to be updated.
    pendingResetPositionUs = positionUs;
    markPendingDiscontinuity(positionUs);
    return positionUs;
  }

  // Try and seek within the buffer first, which avoids restarting the load.
  if (seekInsideBufferUs(trackIsAudioVideoFlags, positionUs)) {
    // The queues seeked successfully, but the samples they now read start at the keyframe before
    // the target, not at the target: every sample before the target has to be decoded (the sample
    // at the target is decoded against it) and only then discarded, so that the renderers do not
    // present the output that was already presented before the seek.
    markPendingDiscontinuity(positionUs);
    return positionUs;
  }

  // The buffer does not hold the target, so the media has to be read again from there.
  pendingDeferredRetry = false;
  pendingResetPositionUs = positionUs;
  loadingFinished = false;
  if (loader->isLoading()) {
    loader->cancelLoading();
  } else {
    for (const auto& sampleQueue : sampleQueues) {
      sampleQueue->reset();
    }
  }
  return positionUs;
}

int64_t ProgressiveMediaPeriod::getAdjustedSeekPositionUs(
    const int64_t positionUs, const SeekParameters& seekParameters) {
  if (mSeekMap == nullptr || !mSeekMap->isSeekable()) {
    // Every seek of a container that cannot be seeked is a seek to its start.
    return 0;
  }
  AVSeekMap::SeekPoint first;
  AVSeekMap::SeekPoint second;
  if (!mSeekMap->getSeekPoints(positionUs, first, second)) {
    return positionUs;
  }
  return seekParameters.resolveSeekPositionUs(positionUs, first.timeUs, second.timeUs);
}

// ==================================================================================================
// SequenceableLoader
// ==================================================================================================

int64_t ProgressiveMediaPeriod::getBufferedPositionUs() {
  CORAL_ASSERT(prepared);
  const auto& trackIsAudioVideoFlags = trackState.trackIsAudioVideoFlags;
  if (loadingFinished) {
    return Definitions::kTimeEndOfSource;
  }
  if (isPendingReset()) {
    return pendingResetPositionUs;
  }
  auto largestQueuedTimestampUs = std::numeric_limits<int64_t>::max();
  if (haveAudioVideoTracks) {
    // Ignore non-AV tracks, which may be sparse or poorly interleaved.
    for (size_t index = 0; index < sampleQueues.size(); ++index) {
      if (trackIsAudioVideoFlags[index] && !sampleQueues[index]->isLastSampleQueued()) {
        largestQueuedTimestampUs =
            std::min(largestQueuedTimestampUs, sampleQueues[index]->getLargestQueuedTimestampUs());
      }
    }
  }
  if (largestQueuedTimestampUs == std::numeric_limits<int64_t>::max()) {
    largestQueuedTimestampUs = getLargestQueuedTimestampUs();
  }
  return largestQueuedTimestampUs == std::numeric_limits<int64_t>::min() ? lastSeekPositionUs
                                                                        : largestQueuedTimestampUs;
}

int64_t ProgressiveMediaPeriod::getNextLoadPositionUs() {
  return enabledTrackCount == 0 ? Definitions::kTimeEndOfSource : getBufferedPositionUs();
}

bool ProgressiveMediaPeriod::continueLoading(const int64_t positionUs) {
  static_cast<void>(positionUs);
  if (loadingFinished || pendingDeferredRetry || (prepared && enabledTrackCount == 0)) {
    return false;
  }
  auto continuedLoading = loadCondition.open();
  if (!loader->isLoading()) {
    startLoading();
    continuedLoading = true;
  }
  return continuedLoading;
}

bool ProgressiveMediaPeriod::isLoading() {
  // The load condition is what the period reports: the period is loading while the loader is
  // allowed to read. A finished load is not loading whatever the condition says.
  return !loadingFinished && loadCondition.isOpen();
}

void ProgressiveMediaPeriod::reevaluateBuffer(const int64_t positionUs) {
  if (released || !prepared || enabledTrackCount == 0) {
    return;
  }
  if (pendingResetPositionUs != Definitions::kTimeUnset) {
    // The queues are about to be reset by the restarted load, so trimming them now would discard
    // media that is still needed.
    return;
  }
  const auto availableDurationUs = durationUs == Definitions::kTimeUnset
                                     ? Definitions::kTimeEndOfSource
                                     : std::max(int64_t(0), durationUs - positionUs);
  const auto bufferedPositionUs = getBufferedPositionUs();
  // Everything the source knows about is buffered when the buffered position is the end of the
  // source, which is what the duration that is available tells the selection.
  const auto bufferedDurationUs = bufferedPositionUs == Definitions::kTimeEndOfSource
                                    ? availableDurationUs
                                    : std::max(int64_t(0), bufferedPositionUs - positionUs);
  const auto& trackEnabledStates = trackState.trackEnabledStates;
  const auto trackCount = static_cast<int>(sampleQueues.size());
  for (auto track = 0; track < trackCount; ++track) {
    if (!trackEnabledStates[static_cast<size_t>(track)]) {
      continue;
    }
    const auto& selection = trackSelections[static_cast<size_t>(track)];
    if (selection == nullptr) {
      continue;
    }
    // This is where ExoPlayer's chunk source drives its selection: the period owns the selections, so
    // it is the one that re-evaluates them while it loads, and that trims the media the selection
    // says it will not need.
    selection->updateSelectedTrack(positionUs, bufferedDurationUs, availableDurationUs,
        /* queueSize= */ 0);
    if (!selection->evaluateQueueSize(positionUs, bufferedDurationUs)) {
      continue;
    }
    const auto minDurationToRetainAfterDiscardUs =
        selection->getMinDurationToRetainAfterDiscardUs();
    if (minDurationToRetainAfterDiscardUs == Definitions::kTimeUnset) {
      continue;
    }
    const auto& sampleQueue = sampleQueues[static_cast<size_t>(track)];
    const auto discardFromTimeUs = positionUs + minDurationToRetainAfterDiscardUs;
    // A discard only ever removes media that was read ahead of what the renderers already read, which
    // is what `discardUpstreamFrom` requires.
    if (discardFromTimeUs > sampleQueue->getLargestReadTimestampUs()) {
      sampleQueue->discardUpstreamFrom(discardFromTimeUs);
    }
  }
}

// ==================================================================================================
// AVExtractorOutput
// ==================================================================================================

std::shared_ptr<AVTrackOutput> ProgressiveMediaPeriod::track(
    const int id, const MediaType type, AVTrackOutput::SampleConsumer consumer) {
  // The consumer the extractor offers is not used: the samples of a track are buffered in its
  // sample queue, which is installed by `prepareTrackOutput`.
  static_cast<void>(consumer);
  return prepareTrackOutput(id, type);
}

std::shared_ptr<AVTrackOutput> ProgressiveMediaPeriod::track(
    const int id, AVTrackOutput::SampleConsumer consumer) {
  const auto type = MediaType::kMediaTypeDefault;
  static_cast<void>(consumer);
  return prepareTrackOutput(id, type);
}

void ProgressiveMediaPeriod::seekMap(std::shared_ptr<AVSeekMap> seekMap) {
  mSeekMap = std::move(seekMap);
  if (mSeekMap != nullptr) {
    durationUs = mSeekMap->getDurationUs();
    if (imageDurationUs != Definitions::kTimeUnset &&
        (durationUs == Definitions::kTimeUnset || durationUs == 0)) {
      durationUs = imageDurationUs;
    }
    isLive = length == Definitions::kLengthUnset && durationUs == Definitions::kTimeUnset;
    if (listener != nullptr) {
      listener->onSourceInfoRefreshed(durationUs, mSeekMap->isSeekable(), isLive);
    }
  }
  if (!prepared) {
    maybeFinishPrepare();
  }
}

void ProgressiveMediaPeriod::endTracks() {
  sampleQueuesBuilt = true;
  if (!prepared) {
    maybeFinishPrepare();
  }
  if (listener != nullptr) {
    listener->onSourceInfoRefreshed(durationUs, isSeekable(), isLive);
  }
}

void ProgressiveMediaPeriod::onUpstreamFormatChanged(const std::shared_ptr<MediaFormat>& format) {
  static_cast<void>(format);
  maybeFinishPrepare();
}

// ==================================================================================================
// Preparation
// ==================================================================================================

std::shared_ptr<AVTrackOutput> ProgressiveMediaPeriod::prepareTrackOutput(const int trackId,
    const MediaType mediaType) {
  if (prepared) {
    return nullptr;
  }
  const auto trackCount = static_cast<int>(sampleQueues.size());
  for (int index = 0; index < trackCount; ++index) {
    if (trackId == sampleQueueTrackIds[static_cast<size_t>(index)]) {
      return trackOutputs[static_cast<size_t>(index)];
    }
  }
  auto sampleQueue = std::make_shared<SampleQueue>(allocator, mediaType);
  sampleQueue->setUpstreamFormatChangeListener(this);
  auto sampleQueueOutput = std::make_shared<SampleQueueOutput>(sampleQueue, this);
  sampleQueue->setUpstreamDiscardedListener(sampleQueueOutput.get());
  auto trackOutput = std::make_shared<AVTrackOutput>(trackId, sampleQueueOutput);
  sampleQueueTrackIds.push_back(trackId);
  sampleQueues.push_back(sampleQueue);
  trackOutputs.push_back(trackOutput);
  return trackOutput;
}

void ProgressiveMediaPeriod::maybeFinishPrepare() {
  if (released || prepared || !sampleQueuesBuilt || mSeekMap == nullptr) {
    return;
  }
  for (const auto& sampleQueue : sampleQueues) {
    if (sampleQueue->getUpstreamFormat() == nullptr) {
      return;
    }
  }
  loadCondition.close();

  const auto trackCount = static_cast<int>(sampleQueues.size());
  std::vector<std::shared_ptr<TrackGroup>> trackArray(static_cast<size_t>(trackCount));
  std::vector<bool> trackIsAudioVideoFlags(static_cast<size_t>(trackCount));
  for (int index = 0; index < trackCount; ++index) {
    const auto& trackFormat = sampleQueues[static_cast<size_t>(index)]->getUpstreamFormat();
    const auto isAudioVideo = trackFormat->isAudio() || trackFormat->isVideo();
    trackIsAudioVideoFlags[static_cast<size_t>(index)] = isAudioVideo;
    haveAudioVideoTracks |= isAudioVideo;

    auto mediaType = MediaType::kMediaTypeInvalid;
    if (trackFormat->isAudio()) {
      mediaType = MediaType::kMediaTypeAudio;
    } else if (trackFormat->isVideo()) {
      mediaType = MediaType::kMediaTypeVideo;
    } else if (trackFormat->isImage()) {
      mediaType = MediaType::kMediaTypeImage;
    } else if (trackFormat->isSubtitle()) {
      mediaType = MediaType::kMediaTypeSubtitle;
    } else if (MimeTypes::getMediaType(trackFormat->getMimeType()) ==
               MediaType::kMediaTypeMetadata) {
      mediaType = MediaType::kMediaTypeMetadata;
    }
    trackArray[static_cast<size_t>(index)] = std::make_shared<TrackGroup>(mediaType, trackFormat);
  }
  trackState.tracks = std::make_shared<TrackGroupArray>(trackArray);
  trackState.trackIsAudioVideoFlags = trackIsAudioVideoFlags;
  trackState.trackEnabledStates.assign(static_cast<size_t>(trackCount), false);
  trackState.trackNotifiedDownstreamFormats.assign(static_cast<size_t>(trackCount), false);

  if (listener != nullptr) {
    listener->onSourceInfoRefreshed(durationUs, isSeekable(), isLive);
  }
  prepared = true;
  if (mediaCallback != nullptr) {
    mediaCallback->onPrepared(shared_from_this());
  }
}

void ProgressiveMediaPeriod::reportFatalError(const std::string& message) {
  if (released) {
    return;
  }
  // The error is recorded rather than reported: `maybeThrowPrepareError` is where the player reads it,
  // like ExoPlayer stores the fatal error of a media period and throws it from
  // `maybeThrowPrepareError`.
  MINILOG_ERROR("The progressive media period cannot be prepared: {}", message);
  fatalErrorMessage = message;
}

void ProgressiveMediaPeriod::maybeThrowPrepareError() {
  if (!fatalErrorMessage.empty()) {
    throw MediaPeriodException(fatalErrorMessage);
  }
}

std::string ProgressiveMediaPeriod::getPrepareError() const {
  return fatalErrorMessage;
}

// ==================================================================================================
// The loading loop
// ==================================================================================================

void ProgressiveMediaPeriod::notifyLoadStarted() {
  eventDispatcher.loadStarted(
      LoadEventInfo{nextLoadTaskId, inputUrl, {}, currentTimeMs(), 0, 0},
      createMediaLoadData(lastSeekPositionUs));
}

MediaLoadData ProgressiveMediaPeriod::createMediaLoadData(const int64_t mediaStartTimeUs) const {
  MediaLoadData mediaLoadData{};
  mediaLoadData.mediaType = MediaType::kMediaTypeDefault;
  mediaLoadData.trackFormat = nullptr;
  mediaLoadData.mediaStartTimeMs = toMilliseconds(mediaStartTimeUs);
  mediaLoadData.mediaEndTimeMs = Definitions::kTimeUnset;
  return mediaLoadData;
}

void ProgressiveMediaPeriod::startLoading() {
  if (extractor == nullptr) {
    // The source could not create an extractor for the media, so no period can ever be prepared.
    reportFatalError("No progressive extractor is available for " + inputUrl);
    return;
  }
  if (inputUrl.empty()) {
    reportFatalError("The media has no url to read from");
    return;
  }
  fatalLoadError = false;
  // The loadable is owned here because the loader only keeps a raw pointer to it. It keeps the
  // period alive through its own reference for as long as the load can reach it.
  loadable = std::make_shared<ExtractingLoadable>(inputUrl, shared_from_this(), loadCondition);
  if (prepared) {
    if (durationUs != Definitions::kTimeUnset && pendingResetPositionUs > durationUs) {
      // The seek target is beyond the end of the media, so there is nothing left to load.
      loadingFinished = true;
      pendingResetPositionUs = Definitions::kTimeUnset;
      loadable.reset();
      return;
    }
    loadable->setLoadPosition(pendingResetPositionUs);
    for (const auto& sampleQueue : sampleQueues) {
      sampleQueue->setStartTimeUs(pendingResetPositionUs);
      // The load restarts at the keyframe before the target, so the samples that follow are the
      // ones before the target first: they have to be decoded but not rendered. The marker is
      // cleared by the queue itself once it reads the first sample at the target.
      sampleQueue->setPendingDiscontinuityPositionUs(pendingResetPositionUs);
    }
    pendingResetPositionUs = Definitions::kTimeUnset;
  } else if (startPositionUs > 0) {
    // The playback starts in the middle of the media. The byte position is resolved by the
    // loadable, because the seek map is only known once the extractor is initialized.
    loadable->setLoadPosition(startPositionUs);
  }
  extractedSamplesCountAtStartOfLoad = getExtractedSamplesCount();
  try {
    const auto elapsedRealtimeMs = loader->startLoading(loadable, &loaderCallback);
    MINILOG_DEBUG("Start loading time(ms): {}", elapsedRealtimeMs);
  } catch (const LoaderException& e) {
    // Another load is still winding down, or the loader has been released. The player asks again on
    // its next iteration, so this is not fatal.
    MINILOG_WARN("The loader refused to start: {}", e.what());
    loadable.reset();
  }
}

void ProgressiveMediaPeriod::onLoadCompleted(ExtractingLoadable* completedLoadable,
    int64_t startTimeMs,
    int64_t loadDurationMs) {
  static_cast<void>(startTimeMs);
  if (released) {
    // The period was released while this callback was queued, and its sample queues are gone.
    return;
  }
  if (completedLoadable != nullptr) {
    eventDispatcher.loadCompleted(
        LoadEventInfo{completedLoadable->getLoadTaskId(),
            inputUrl,
            {},
            currentTimeMs(),
            loadDurationMs,
            completedLoadable->getBytesLoaded()},
        createMediaLoadData(lastSeekPositionUs));
  }
  if (durationUs == Definitions::kTimeUnset && imageDurationUs != Definitions::kTimeUnset) {
    durationUs = imageDurationUs;
    if (listener != nullptr) {
      listener->onSourceInfoRefreshed(durationUs, isSeekable(), isLive);
    }
  } else if (durationUs == Definitions::kTimeUnset && mSeekMap != nullptr) {
    const auto seekable = mSeekMap->isSeekable();
    const auto largestQueuedTimestampUs = getLargestQueuedTimestampUs();
    durationUs = largestQueuedTimestampUs == std::numeric_limits<int64_t>::min()
                     ? 0
                     : largestQueuedTimestampUs + DEFAULT_LAST_SAMPLE_DURATION_US;
    if (listener != nullptr) {
      listener->onSourceInfoRefreshed(durationUs, seekable, isLive);
    }
  }
  loadingFinished = true;
  if (loadable.get() == completedLoadable) {
    // The load completed, so nothing can reach the loadable any more. This also breaks the
    // reference cycle between the period and its loadable.
    loadable.reset();
  }
  if (mediaCallback != nullptr) {
    mediaCallback->onContinueLoadingRequested(shared_from_this());
  }
}

void ProgressiveMediaPeriod::onLoadCanceled(ExtractingLoadable* canceledLoadable,
    int64_t startTimeMs,
    int64_t loadDurationMs,
    const bool releasedByLoader) {
  static_cast<void>(startTimeMs);
  if (released) {
    // The period was released while this callback was queued, and its sample queues are gone.
    return;
  }
  if (canceledLoadable != nullptr) {
    eventDispatcher.loadCanceled(
        LoadEventInfo{canceledLoadable->getLoadTaskId(),
            inputUrl,
            {},
            currentTimeMs(),
            loadDurationMs,
            canceledLoadable->getBytesLoaded()},
        createMediaLoadData(lastSeekPositionUs));
  }
  if (!releasedByLoader) {
    for (const auto& sampleQueue : sampleQueues) {
      sampleQueue->reset();
    }
    if (enabledTrackCount > 0 && mediaCallback != nullptr) {
      mediaCallback->onContinueLoadingRequested(shared_from_this());
    }
  }
  if (loadable.get() == canceledLoadable) {
    loadable.reset();
  }
}

LoadErrorAction ProgressiveMediaPeriod::onLoadError(ExtractingLoadable* failedLoadable,
    const std::string& errorMessage,
    const int errorCount,
    int64_t startTimeMs,
    int64_t loadDurationMs) {
  static_cast<void>(startTimeMs);
  if (released) {
    // The period was released while this callback was queued, and the retry it could ask for would
    // never be read: the load is over.
    return LoadErrorAction::Cancel();
  }
  MINILOG_ERROR("Load error on attempt {}: {}", errorCount, errorMessage);
  // The loadable is not reset here: the loader keeps a raw pointer to it for the retry.
  LoadErrorHandlingPolicy::LoadErrorInfo loadErrorInfo;
  loadErrorInfo.url = inputUrl;
  loadErrorInfo.errorCount = errorCount;
  loadErrorInfo.error = Error(errorMessage);
  loadErrorInfo.loadDurationMs = loadDurationMs;

  // A container that no extractor recognizes cannot be recognized by a retry either, so such a
  // failure cancels the load whatever the policy says.
  const auto minimumRetryCount =
      loadErrorHandlingPolicy->getMinimumLoadableRetryCount(DataType::kMedia);
  const auto retryDelayMs = loadErrorHandlingPolicy->getRetryDelayMsFor(loadErrorInfo);
  const bool fatal = fatalLoadError || errorCount > minimumRetryCount ||
                     retryDelayMs == Definitions::kTimeUnset;

  if (failedLoadable != nullptr) {
    // ExoPlayer reports the error to the listeners before it decides whether to retry: an
    // application that only logs the error does not need to know which of the two happened.
    eventDispatcher.loadError(
        LoadEventInfo{failedLoadable->getLoadTaskId(),
            inputUrl,
            {},
            currentTimeMs(),
            loadDurationMs,
            failedLoadable->getBytesLoaded()},
        createMediaLoadData(lastSeekPositionUs),
        loadErrorInfo.error,
        fatal);
  }
  if (fatal) {
    reportFatalError(errorMessage);
    return LoadErrorAction::Cancel();
  }
  return LoadErrorAction::Retry(retryDelayMs);
}

void ProgressiveMediaPeriod::onLoaderReleased() {
  // The resources are released by `release` itself, which is what makes the release independent of
  // this callback being dispatched. It is kept for the `ReleaseCallback` of the loader.
  releaseResources();
}

//--------------------------------------------------------------------------------------------------
// LoaderCallbackImpl: forwards the loader results to the period.

void ProgressiveMediaPeriod::LoaderCallbackImpl::onLoadCompleted(ExtractingLoadable* load,
    int64_t startTimeMs,
    int64_t loadDurationMs) {
  period.onLoadCompleted(load, startTimeMs, loadDurationMs);
}

void ProgressiveMediaPeriod::LoaderCallbackImpl::onLoadCanceled(ExtractingLoadable* load,
    int64_t startTimeMs,
    int64_t loadDurationMs,
    bool released) {
  period.onLoadCanceled(load, startTimeMs, loadDurationMs, released);
}

LoadErrorAction ProgressiveMediaPeriod::LoaderCallbackImpl::onLoadError(ExtractingLoadable* load,
    const std::string& errorMessage,
    int errorCount,
    int64_t startTimeMs,
    int64_t loadDurationMs) {
  return period.onLoadError(load, errorMessage, errorCount, startTimeMs, loadDurationMs);
}

void ProgressiveMediaPeriod::LoaderCallbackImpl::onLoaderReleased() {
  period.onLoaderReleased();
}

// ==================================================================================================
// Seeking and reading
// ==================================================================================================

bool ProgressiveMediaPeriod::seekInsideBufferUs(
    const std::vector<bool>& trackIsAudioVideoFlags, const int64_t positionUs) {
  const auto trackCount = sampleQueues.size();
  for (size_t index = 0; index < trackCount; ++index) {
    const auto& sampleQueue = sampleQueues[index];
    const auto seekInsideQueue = sampleQueue->seekTo(positionUs, false);
    if (!seekInsideQueue && (trackIsAudioVideoFlags[index] || !haveAudioVideoTracks)) {
      return false;
    }
  }
  return true;
}

void ProgressiveMediaPeriod::markPendingDiscontinuity(const int64_t positionUs) {
  for (const auto& sampleQueue : sampleQueues) {
    sampleQueue->setPendingDiscontinuityPositionUs(positionUs);
  }
}

int ProgressiveMediaPeriod::getExtractedSamplesCount() {
  int extractedSamplesCount = 0;
  for (const auto& sampleQueue : sampleQueues) {
    extractedSamplesCount += sampleQueue->getWriteIndex();
  }
  return extractedSamplesCount;
}

int64_t ProgressiveMediaPeriod::getLargestQueuedTimestampUs() {
  auto largestQueuedTimestampUs = std::numeric_limits<int64_t>::min();
  for (const auto& sampleQueue : sampleQueues) {
    largestQueuedTimestampUs =
        std::max(largestQueuedTimestampUs, sampleQueue->getLargestQueuedTimestampUs());
  }
  return largestQueuedTimestampUs;
}

bool ProgressiveMediaPeriod::suppressRead() const {
  return notifyDiscontinuity || isPendingReset();
}

bool ProgressiveMediaPeriod::isPendingReset() const {
  return pendingResetPositionUs != Definitions::kTimeUnset;
}

bool ProgressiveMediaPeriod::isReady(const int track) const {
  return !suppressRead() && sampleQueues[static_cast<size_t>(track)]->isReady(loadingFinished);
}

SampleStream::ReadDataResult ProgressiveMediaPeriod::readData(const int track,
    FormatHolder& formatHolder,
    AVPacketBuffer* packetBuffer,
    const bool formatRequired) {
  if (suppressRead()) {
    return SampleStream::ReadDataResult::kResultNothingRead;
  }
  maybeNotifyDownstreamFormat(track);
  const auto result =
      static_cast<SampleStream::ReadDataResult>(sampleQueues[static_cast<size_t>(track)]->readSample(
          formatHolder, packetBuffer, formatRequired, loadingFinished));
  if (result == SampleStream::ReadDataResult::kResultNothingRead) {
    maybeStartDeferredRetry(track);
  }
  return result;
}

int ProgressiveMediaPeriod::skipData(const int track, const int64_t positionUs) {
  if (suppressRead()) {
    return 0;
  }
  maybeNotifyDownstreamFormat(track);
  const auto& sampleQueue = sampleQueues[static_cast<size_t>(track)];
  const int skipCount = sampleQueue->getSkipCount(positionUs, loadingFinished);
  sampleQueue->skip(skipCount);
  if (skipCount == 0) {
    maybeStartDeferredRetry(track);
  }
  return skipCount;
}

void ProgressiveMediaPeriod::maybeNotifyDownstreamFormat(const int track) {
  auto& trackNotifiedDownstreamFormats = trackState.trackNotifiedDownstreamFormats;
  if (trackNotifiedDownstreamFormats[static_cast<size_t>(track)]) {
    return;
  }
  // The format of the track is handed to a renderer for the first time. ExoPlayer reports it so
  // that an application can observe the switch between the tracks of different qualities.
  const auto& trackGroup = trackState.tracks->get(track);
  auto const& format = trackGroup->getFormat(0);
  MediaLoadData mediaLoadData{};
  mediaLoadData.mediaType = format != nullptr && format->isAudio() ? MediaType::kMediaTypeAudio
                             : format != nullptr && format->isVideo() ? MediaType::kMediaTypeVideo
                                                                     : MediaType::kMediaTypeDefault;
  mediaLoadData.trackFormat = format;
  mediaLoadData.mediaStartTimeMs = toMilliseconds(lastSeekPositionUs);
  mediaLoadData.mediaEndTimeMs = Definitions::kTimeUnset;
  eventDispatcher.downstreamFormatChanged(mediaLoadData);
  trackNotifiedDownstreamFormats[static_cast<size_t>(track)] = true;
}

void ProgressiveMediaPeriod::notifyUpstreamDiscarded(const MediaLoadData& mediaLoadData) {
  eventDispatcher.upstreamDiscarded(mediaLoadData);
}

void ProgressiveMediaPeriod::maybeStartDeferredRetry(const int track) {
  const auto& trackIsAudioVideoFlags = trackState.trackIsAudioVideoFlags;
  if (!pendingDeferredRetry || !trackIsAudioVideoFlags[static_cast<size_t>(track)] ||
      sampleQueues[static_cast<size_t>(track)]->isReady(false)) {
    return;
  }
  pendingResetPositionUs = 0;
  pendingDeferredRetry = false;
  notifyDiscontinuity = true;
  lastSeekPositionUs = 0;
  extractedSamplesCountAtStartOfLoad = 0;
  for (const auto& sampleQueue : sampleQueues) {
    sampleQueue->reset();
  }
  if (mediaCallback != nullptr) {
    mediaCallback->onContinueLoadingRequested(shared_from_this());
  }
}

void ProgressiveMediaPeriod::maybeCallbackContinueLoadingRequested() {
  if (!released && mediaCallback != nullptr) {
    mediaCallback->onContinueLoadingRequested(shared_from_this());
  }
}

// ==================================================================================================
// SampleQueueOutput
// ==================================================================================================

void ProgressiveMediaPeriod::SampleQueueOutput::setFormat(const std::shared_ptr<MediaFormat>& format) {
  queue->setFormat(format);
}

void ProgressiveMediaPeriod::SampleQueueOutput::consume(const AVSample& sample) {
  // The queue ignores the sample that signals the end of the stream: the end of the stream is derived
  // from the state of the load, see SampleQueue::readSampleMetadata, so that the renderers see it
  // exactly once.
  queue->consume(sample);
}

void ProgressiveMediaPeriod::SampleQueueOutput::onUpstreamDiscarded(const int64_t mediaStartTimeUs,
    const int64_t mediaEndTimeUs) {
  if (period == nullptr) {
    return;
  }
  // The media that was read ahead is given up, which ExoPlayer reports so that an application can
  // observe that a part of the stream has to be read again in another format.
  auto mediaLoadData = MediaLoadData{};
  mediaLoadData.mediaType = queue->getMediaType();
  mediaLoadData.trackFormat = queue->getUpstreamFormat();
  mediaLoadData.mediaStartTimeMs =
      mediaStartTimeUs == Definitions::kTimeUnset ? Definitions::kTimeUnset
                                                  : toMilliseconds(mediaStartTimeUs);
  mediaLoadData.mediaEndTimeMs =
      mediaEndTimeUs == Definitions::kTimeUnset ? Definitions::kTimeUnset
                                                : toMilliseconds(mediaEndTimeUs);
  period->notifyUpstreamDiscarded(mediaLoadData);
}

AVBufferFlags ProgressiveMediaPeriod::SampleQueueOutput::toBufferFlags(const uint32_t flags) {
  int32_t result = AVBufferFlags::kNone;
  if ((flags & AVSample::kFlagKeyFrame) != 0) {
    result |= AVBufferFlags::kKeyFrame;
  }
  if ((flags & AVSample::kFlagCodecConfig) != 0) {
    result |= AVBufferFlags::kCodecConfig;
  }
  if ((flags & AVSample::kFlagEndOfStream) != 0) {
    result |= AVBufferFlags::kEndOfStream;
  }
  if ((flags & AVSample::kFlagFirstSample) != 0) {
    result |= AVBufferFlags::kFirstSample;
  }
  if ((flags & AVSample::kFlagHasSupplementalData) != 0) {
    result |= AVBufferFlags::kHasSupplementalData;
  }
  if ((flags & AVSample::kFlagLastSample) != 0) {
    result |= AVBufferFlags::kLastSample;
  }
  if ((flags & AVSample::kFlagDiscard) != 0) {
    result |= AVBufferFlags::kDiscard;
  }
  return static_cast<AVBufferFlags>(result);
}

// ==================================================================================================
// ProgressiveSampleStream
// ==================================================================================================

bool ProgressiveMediaPeriod::ProgressiveSampleStream::isReady() const {
  return source->isReady(track);
}

SampleStream::ReadDataResult ProgressiveMediaPeriod::ProgressiveSampleStream::readData(
    FormatHolder& formatHolder, AVPacketBuffer* packetBuffer, const bool formatRequired) {
  return source->readData(track, formatHolder, packetBuffer, formatRequired);
}

int ProgressiveMediaPeriod::ProgressiveSampleStream::skipData(const int64_t positionUs) {
  return source->skipData(track, positionUs);
}

// ==================================================================================================
// ExtractingLoadable
// ==================================================================================================

ProgressiveMediaPeriod::ExtractingLoadable::ExtractingLoadable(std::string url,
    const std::shared_ptr<ProgressiveMediaPeriod>& source,
    LoadCondition& loadCondition) noexcept
    : mInputUrl(std::move(url)),
      source(source),
      loadCondition(loadCondition) {
}

ProgressiveMediaPeriod::ExtractingLoadable::~ExtractingLoadable() = default;

int64_t ProgressiveMediaPeriod::ExtractingLoadable::getBytesLoaded() const {
  if (source->input == nullptr) {
    return 0;
  }
  const auto position = source->input->getPosition();
  return position > loadStartPosition ? position - loadStartPosition : 0;
}

void ProgressiveMediaPeriod::ExtractingLoadable::cancelLoad() {
  loadingCanceled.store(true, std::memory_order_release);
  loadCondition.close();
}

bool ProgressiveMediaPeriod::ExtractingLoadable::ensureInput() {
  if (source->input != nullptr) {
    return true;
  }
  auto created = source->dataSourceFactory != nullptr ? source->dataSourceFactory(mInputUrl)
                                                      : std::make_shared<AVFileDataSource>();
  if (created == nullptr) {
    return false;
  }
  if (!created->isOpen() && !created->open(mInputUrl)) {
    return false;
  }
  source->dataSource = created;
  source->length = created->getLength();
  source->input = std::make_shared<AVDefaultExtractorInput>(created, 0);
  return true;
}

void ProgressiveMediaPeriod::ExtractingLoadable::load() {
  loadErrorMessage.clear();

  // Every attempt of a load is reported with its own identifier, so that an application can pair
  // the events of a retry with the attempt they belong to.
  loadTaskId = ++source->nextLoadTaskId;
  loadStartTimeMs = currentTimeMs();
  loadStartPosition = source->input != nullptr ? source->input->getPosition() : 0;
  source->notifyLoadStarted();

  if (!ensureInput()) {
    loadErrorMessage = "Unable to open the media: " + mInputUrl;
    return;
  }
  auto& extractorInput = *source->input;

  // The extractor has to be initialized before it can recognize the container, and initializing it
  // is what lets the selected extractor declare its tracks and its seek map. It is done once per
  // period: the tracks of the container are declared when it is initialized, and the period refuses
  // to create them again once it is prepared. A later load reuses the extractor and only tells it
  // where the input was repositioned.
  if (!source->extractorInitialized) {
    source->extractor->init(*source);
    const int confidence = source->extractor->sniff(extractorInput);
    // A confident extractor that has nothing to read is as broken as one that recognized nothing, and
    // an extractor that demuxes the media with the reader of its platform recognizes the container
    // without having an extractor of the module to report, see `isContainerRecognized`.
    if (confidence <= AVExtractorsFactory::kSniffResultNone ||
        !source->extractor->isContainerRecognized()) {
      loadErrorMessage = "None of the extractors recognized the media: " + mInputUrl;
      // A retry cannot make an unrecognized container recognizable, so the failure is fatal.
      source->fatalLoadError = true;
      return;
    }
    source->extractorInitialized = true;
  }

  if (pendingExtractorSeek) {
    // The byte position of the target is resolved from the seek map, which the extractor declared
    // while it was initialized, i.e. before the first read. A container that cannot be seeked is
    // read again from its beginning.
    long position = 0;
    const auto seekMap = source->extractor->getSeekMap();
    AVSeekMap::SeekPoint seekPoint;
    if (seekMap != nullptr && seekMap->isSeekable() &&
        seekMap->getSeekPoint(seekTimeUs, seekPoint)) {
      position = seekPoint.position;
    }
    if (!extractorInput.resetPosition(position, true)) {
      loadErrorMessage = "Unable to seek to the byte position " + std::to_string(position) + " of " +
                         mInputUrl;
      return;
    }
    source->extractor->seek(position, seekTimeUs);
    pendingExtractorSeek = false;
  }

  // The number of bytes that were read since the last time the load asked whether it should
  // continue, so that the condition is not closed for every single sample.
  auto checkpoint = extractorInput.getPosition();
  while (!loadingCanceled.load(std::memory_order_acquire)) {
    // Wait until the period allows the load to read again. The condition is closed every
    // continueLoadingCheckIntervalBytes bytes, which parks this thread until the player decides
    // that more media is needed.
    if (!loadCondition.block(loadingCanceled)) {
      return;
    }
    const auto positionBefore = extractorInput.getPosition();
    const auto extractedBefore = source->getExtractedSamplesCount();
    const auto result = source->extractor->read(extractorInput);
    const auto positionAfter = extractorInput.getPosition();
    const auto extractedAfter = source->getExtractedSamplesCount();

    if (source->prepared &&
            positionAfter > checkpoint + source->continueLoadingCheckIntervalBytes) {
      checkpoint = positionAfter;
      loadCondition.close();
      source->maybeCallbackContinueLoadingRequested();
    }

    if (result == AVExtractor::kResultEndOfInput) {
      // The end of the media was reached. The load completes, and the sample queues report the end
      // of the stream to the renderers.
      return;
    }
    if (positionAfter == positionBefore && extractedAfter == extractedBefore) {
      // The extractor neither read from the input nor delivered a sample, so the load stops rather
      // than spinning. Both are progress: an extractor may deliver the samples it read ahead, or,
      // like the extractor of a platform, read the media through its own reader, while the input of
      // this reader only moves when that reader fills up again. The failure is reported so that the
      // player can surface it.
      loadErrorMessage = "The extractor of " + mInputUrl + " stopped making progress";
      return;
    }
  }
}

} // namespace coral
