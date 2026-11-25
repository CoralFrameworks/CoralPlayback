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

#include "DefaultLoadControl.h"

#include <minilog/log.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace coral {

static int64_t msToUs(const int64_t timeMs) {
  return (timeMs == Definitions::kTimeUnset || timeMs == Definitions::kTimeEndOfSource)
           ? timeMs
           : timeMs * Definitions::kMicrosPerMillisecond;
}

DefaultLoadControl::DefaultLoadControl()
  : DefaultLoadControl(std::make_shared<DefaultAllocator>(true, DEFAULT_BUFFER_SEGMENT_SIZE),
    DEFAULT_MIN_BUFFER_MS,
    DEFAULT_MIN_BUFFER_FOR_LOCAL_PLAYBACK_MS,
    DEFAULT_MAX_BUFFER_MS,
    DEFAULT_MAX_BUFFER_FOR_LOCAL_PLAYBACK_MS,
    DEFAULT_BUFFER_FOR_PLAYBACK_MS,
    DEFAULT_BUFFER_FOR_PLAYBACK_FOR_LOCAL_PLAYBACK_MS,
    DEFAULT_BUFFER_FOR_PLAYBACK_AFTER_REBUFFER_MS,
    DEFAULT_BUFFER_FOR_PLAYBACK_AFTER_REBUFFER_FOR_LOCAL_PLAYBACK_MS,
    DEFAULT_TARGET_BUFFER_BYTES,
    DEFAULT_PRIORITIZE_TIME_OVER_SIZE_THRESHOLDS,
    DEFAULT_PRIORITIZE_TIME_OVER_SIZE_THRESHOLDS_FOR_LOCAL_PLAYBACK,
    DEFAULT_BACK_BUFFER_DURATION_MS,
    DEFAULT_RETAIN_BACK_BUFFER_FROM_KEYFRAME) {
}

DefaultLoadControl::DefaultLoadControl(const std::shared_ptr<Allocator>& allocator) noexcept
  : DefaultLoadControl(allocator,
    DEFAULT_MIN_BUFFER_MS,
    DEFAULT_MIN_BUFFER_FOR_LOCAL_PLAYBACK_MS,
    DEFAULT_MAX_BUFFER_MS,
    DEFAULT_MAX_BUFFER_FOR_LOCAL_PLAYBACK_MS,
    DEFAULT_BUFFER_FOR_PLAYBACK_MS,
    DEFAULT_BUFFER_FOR_PLAYBACK_FOR_LOCAL_PLAYBACK_MS,
    DEFAULT_BUFFER_FOR_PLAYBACK_AFTER_REBUFFER_MS,
    DEFAULT_BUFFER_FOR_PLAYBACK_AFTER_REBUFFER_FOR_LOCAL_PLAYBACK_MS,
    DEFAULT_TARGET_BUFFER_BYTES,
    DEFAULT_PRIORITIZE_TIME_OVER_SIZE_THRESHOLDS,
    DEFAULT_PRIORITIZE_TIME_OVER_SIZE_THRESHOLDS_FOR_LOCAL_PLAYBACK,
    DEFAULT_BACK_BUFFER_DURATION_MS,
    DEFAULT_RETAIN_BACK_BUFFER_FROM_KEYFRAME) {
}

DefaultLoadControl::DefaultLoadControl(std::shared_ptr<Allocator> allocator,
  const int minBufferMs,
  const int minBufferForLocalPlaybackMs,
  const int maxBufferMs,
  const int maxBufferForLocalPlaybackMs,
  const int bufferForPlaybackMs,
  const int bufferForPlaybackForLocalPlaybackMs,
  const int bufferForPlaybackAfterRebufferMs,
  const int bufferForPlaybackAfterRebufferForLocalPlaybackMs,
  const int targetBufferBytes,
  const bool prioritizeTimeOverSizeThresholds,
  const bool prioritizeTimeOverSizeThresholdsForLocalPlayback,
  const int backBufferDurationMs,
  const bool retainBackBufferFromKeyframe,
  std::map<std::string, int> playerTargetBufferBytes) noexcept
  : allocator(std::move(allocator)),
    minBufferUs(msToUs(minBufferMs)),
    minBufferForLocalPlaybackUs(msToUs(minBufferForLocalPlaybackMs)),
    maxBufferUs(msToUs(maxBufferMs)),
    maxBufferForLocalPlaybackUs(msToUs(maxBufferForLocalPlaybackMs)),
    bufferForPlaybackUs(msToUs(bufferForPlaybackMs)),
    bufferForPlaybackForLocalPlaybackUs(msToUs(bufferForPlaybackForLocalPlaybackMs)),
    bufferForPlaybackAfterRebufferUs(msToUs(bufferForPlaybackAfterRebufferMs)),
    bufferForPlaybackAfterRebufferForLocalPlaybackUs(msToUs(bufferForPlaybackAfterRebufferForLocalPlaybackMs)),
    targetBufferBytesOverwrite(targetBufferBytes),
    prioritizeTimeOverSizeThresholds(prioritizeTimeOverSizeThresholds),
    prioritizeTimeOverSizeThresholdsForLocalPlayback(prioritizeTimeOverSizeThresholdsForLocalPlayback),
    backBufferDurationUs(msToUs(backBufferDurationMs)),
    mRetainBackBufferFromKeyframe(retainBackBufferFromKeyframe),
    playerTargetBufferBytesOverwrites(std::move(playerTargetBufferBytes)),
    targetBufferBytes(targetBufferBytes != -1 ? targetBufferBytes : DEFAULT_MIN_BUFFER_SIZE),
    isLoading(false) {
}

void DefaultLoadControl::onPrepared() {
  // Only the loading state is reset here: the allocator is applied once the tracks are known, so
  // that the target buffer size matches the selected tracks rather than the pre-selection default.
  resetPlayerLoadingState();
}

void DefaultLoadControl::onTracksSelected(const Parameters parameters,
  const std::shared_ptr<TrackGroupArray>& trackGroups,
  const std::shared_ptr<TrackSelectionArray>& selections) {
  targetBufferBytes = (targetBufferBytesOverwrite == DEFAULT_TARGET_BUFFER_BYTES)
                        ? calculateTargetBufferBytes(parameters, selections)
                        : targetBufferBytesOverwrite;
  updateAllocator();
}

void DefaultLoadControl::onStopped() {
  resetPlayerLoadingState();
  allocator->reset();
}

void DefaultLoadControl::onReleased() {
  resetPlayerLoadingState();
  allocator->reset();
}

int64_t DefaultLoadControl::getBackBufferDurationUs() {
  return backBufferDurationUs;
}

bool DefaultLoadControl::retainBackBufferFromKeyframe() {
  return mRetainBackBufferFromKeyframe;
}

bool DefaultLoadControl::shouldContinueLoading(Parameters parameters) {
  const auto targetBufferSizeReached = allocator->getTotalBytesAllocated() >= targetBufferBytes;
  auto localPlayback = isLocalPlayback(parameters);
  auto minimumBufferUs = localPlayback ? minBufferForLocalPlaybackUs : minBufferUs;
  auto maximumBufferUs = localPlayback ? maxBufferForLocalPlaybackUs : maxBufferUs;
  if (parameters.playbackSpeed > 1) {
    // The playback speed is faster than real time, so scale up the minimum required media
    // duration to keep enough media buffered for a playout duration of minBufferUs.
    const int64_t mediaDurationMinBufferUs =
      getMediaDurationForPlayoutDuration(minimumBufferUs, parameters.playbackSpeed);
    minimumBufferUs = std::min(mediaDurationMinBufferUs, maximumBufferUs);
  }
  // Prevent playback from getting stuck if minBufferUs is too small.
  minimumBufferUs = std::max(minimumBufferUs, static_cast<int64_t>(500000));
  if (parameters.bufferedDurationUs < minimumBufferUs) {

    const auto timeOverSizeThresholds = localPlayback
                                    ? prioritizeTimeOverSizeThresholdsForLocalPlayback
                                    : prioritizeTimeOverSizeThresholds;
    const auto hasEnoughHeadroomForPrioritizeTimeOverSizeThreshold =
      heapHasEnoughHeadroomForPrioritizeTimeOverSizeThreshold();
    isLoading = (timeOverSizeThresholds &&
                 hasEnoughHeadroomForPrioritizeTimeOverSizeThreshold) ||
                !targetBufferSizeReached;
    if (!isLoading &&
        timeOverSizeThresholds &&
        !hasEnoughHeadroomForPrioritizeTimeOverSizeThreshold) {
      isLoading = true;
      MINILOG_INFO("Stopped loading before minBufferUs reached due to memory pressure, "
        "despite prioritizeTimeOverSizeThresholds=true.");
    }
    if (!isLoading && parameters.bufferedDurationUs < 500000) {
      MINILOG_WARN("Target buffer size reached with less than 500ms of buffered media data.");
    }
  } else if (parameters.bufferedDurationUs >= maxBufferUs || targetBufferSizeReached) {
    isLoading = false;
  } // Else don't change the loading state.
  return isLoading;
}

bool DefaultLoadControl::shouldStartPlayback(const Parameters parameters) {
  const auto localPlayback = isLocalPlayback(parameters);
  const auto bufferedDurationUs =
    getPlayoutDurationForMediaDuration(parameters.bufferedDurationUs, parameters.playbackSpeed);
  auto minBufferDurationUs =
    parameters.rebuffering
      ? (localPlayback ? bufferForPlaybackAfterRebufferForLocalPlaybackUs: bufferForPlaybackAfterRebufferUs)
      : (localPlayback ? bufferForPlaybackForLocalPlaybackUs: bufferForPlaybackUs);
  if (parameters.targetLiveOffsetUs != kTimeUnset) {
    // The playback is close enough to the live edge when half of the target offset is buffered.
    minBufferDurationUs = std::min(parameters.targetLiveOffsetUs / 2, minBufferDurationUs);
  }
  const auto timeOverSizeThresholds = localPlayback
                                        ? prioritizeTimeOverSizeThresholdsForLocalPlayback
                                        : prioritizeTimeOverSizeThresholds;
  return minBufferDurationUs <= 0 ||
         bufferedDurationUs >= minBufferDurationUs ||
         (!timeOverSizeThresholds && allocator->getTotalBytesAllocated() >= targetBufferBytes);
}

int DefaultLoadControl::calculateTargetBufferBytes(const Parameters& parameters,
  const std::shared_ptr<TrackSelectionArray>& selections) {
  int targetBufferSize = 0;
  auto localPlayback = isLocalPlayback(parameters);
  for (size_t i = 0; i < selections->length(); ++i) {
    if (selections->get(i) != nullptr) {
      targetBufferSize += getDefaultBufferSize(selections->get(i)->getTrackGroup()->mediaType(), localPlayback);
    }
  }
  return std::clamp(targetBufferSize, DEFAULT_MIN_BUFFER_SIZE, DEFAULT_MAX_BUFFER_SIZE);
}

void DefaultLoadControl::resetPlayerLoadingState() {
  targetBufferBytes = targetBufferBytesOverwrite == DEFAULT_TARGET_BUFFER_BYTES
                        ? DEFAULT_MIN_BUFFER_SIZE
                        : targetBufferBytesOverwrite;
  isLoading = false;
}

void DefaultLoadControl::updateAllocator() {
  allocator->setTargetBufferSize(targetBufferBytes);
}

bool DefaultLoadControl::isLocalPlayback(const Parameters& parameters) {
  const auto timeline = parameters.timeline.lock();
  if (timeline == nullptr || timeline->isEmpty()) {
    return false;
  }
  // A timeline populates the window and the period it is given and returns them, so both lookups are
  // made with the scratch objects of this control, which are never null. The period of the parameters
  // may not be in the timeline yet, e.g. while the player prepares it, and then there is no media item
  // to read.
  const auto timelinePeriod = timeline->getPeriodByUid(parameters.mediaPeriodId.periodUid(), period);
  if (timelinePeriod == nullptr) {
    return false;
  }
  if (timeline->getWindow(timelinePeriod->windowIndex, window) == nullptr) {
    return false;
  }
  const auto mediaItem = window->mediaItem;
  if (mediaItem == nullptr || !mediaItem->hasLocalConfiguration()) {
    return false;
  }
  return isLocalUrl(mediaItem->getUrl());
}

bool DefaultLoadControl::isLocalUrl(const std::string& url) {
  const auto separator = url.find(':');
  if (separator == std::string::npos) {
    // A url without a scheme is a path on the device.
    return true;
  }
  const auto scheme = url.substr(0, separator);
  return scheme == "file"
         || scheme == "content"
         || scheme == "data"
         || scheme == "android.resource"
         || scheme == "rawresource"
         || scheme == "asset"
         // The Apple layout names a resource of the application bundle with either scheme; the data
         // source of the platform reads both from the bundle rather than from a server.
         || scheme == "bundle";
}

int DefaultLoadControl::getDefaultBufferSize(MediaType trackType, bool isLocalPlayback) {
  switch (trackType) {
    case MediaType::kMediaTypeDefault: {
      // A muxed track carries video, audio and text, so it needs room for all of them.
      return DEFAULT_MUXED_BUFFER_SIZE;
    }
    case MediaType::kMediaTypeAudio: {
      return DEFAULT_AUDIO_BUFFER_SIZE;
    }
    case MediaType::kMediaTypeVideo: {
      return isLocalPlayback ? DEFAULT_VIDEO_BUFFER_SIZE_FOR_LOCAL_PLAYBACK : DEFAULT_VIDEO_BUFFER_SIZE;
    }
    case MediaType::kMediaTypeSubtitle: {
      return DEFAULT_TEXT_BUFFER_SIZE;
    }
    case MediaType::kMediaTypeMetadata: {
      return DEFAULT_METADATA_BUFFER_SIZE;
    }
    case MediaType::kMediaTypeImage: {
      return DEFAULT_IMAGE_BUFFER_SIZE;
    }
    case MediaType::kMediaTypeInvalid:
    default: {
      // A track of a type that is not known cannot be sized, and it is not a reason to fail the
      // playback: ExoPlayer returns its minimum buffer size for an unknown track type as well.
      return DEFAULT_MIN_BUFFER_SIZE;
    }
  }
}

int64_t DefaultLoadControl::getMediaDurationForPlayoutDuration(const int64_t playoutDuration, const float speed) {
  if (speed == 1.f) {
    return playoutDuration;
  }
  return static_cast<int64_t>(std::llround(static_cast<double>(playoutDuration) * speed));
}

int64_t DefaultLoadControl::getPlayoutDurationForMediaDuration(const int64_t mediaDuration, const float speed) {
  if (speed == 1.f) {
    return mediaDuration;
  }
  return static_cast<int64_t>(std::llround(static_cast<double>(mediaDuration) / speed));
}

bool DefaultLoadControl::heapHasEnoughHeadroomForPrioritizeTimeOverSizeThreshold() {
  // ExoPlayer asks the runtime of the JVM whether the heap still has room here, because its buffers
  // live on that heap. The buffers of this allocator are native allocations instead, so the media the
  // player holds is not what makes the JVM run out of memory, and the headroom the question is about is
  // always there: reporting that it is not would stop the loading of high bitrate media before the
  // minimum duration it asks for is buffered.
  return true;
}

} // namespace coral
