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
#include <coral/source/MaskingMediaPeriod.h>

#include <media/Definitions.h>

#include <utility>

namespace coral {

MaskingMediaPeriod::MaskingMediaPeriod(const std::shared_ptr<MediaSource>& mediaSource,
    MediaPeriodId id,
    const std::shared_ptr<Allocator>& allocator,
    int64_t preparePositionUs) noexcept
    : mediaSource(mediaSource),
      id(std::move(id)),
      allocator(allocator),
      callback(nullptr),
      preparePositionUs(preparePositionUs),
      listener(nullptr) {
  preparePositionOverrideUs = Definitions::kTimeUnset;
}

void MaskingMediaPeriod::setPrepareListener(MaskingMediaPeriod::PrepareListener* newListener) {
  this->listener = newListener;
}

void MaskingMediaPeriod::createPeriod(const MediaPeriodId& newId) {
  const auto positionUs = getPreparePositionWithOverride(this->preparePositionUs);
  mediaPeriod = mediaSource->createPeriod(newId, allocator, positionUs);
  if (callback) {
    mediaPeriod->prepare(this, positionUs);
  }
}

void MaskingMediaPeriod::releasePeriod() {
  if (mediaPeriod) {
    // Clear the member before the source releases the period: a callback raised by the release must
    // not select tracks on a period that is already being torn down.
    const auto released = std::move(mediaPeriod);
    mediaPeriod.reset();
    mediaSource->releasePeriod(released);
  }
}

void MaskingMediaPeriod::prepare(MediaPeriod::Callback* newCallback, int64_t positionUs) {
  this->callback = newCallback;
  if (mediaPeriod != nullptr) {
    mediaPeriod->prepare(this, getPreparePositionWithOverride(preparePositionUs));
  }
}

int64_t MaskingMediaPeriod::selectTracks(
    const std::vector<std::shared_ptr<TrackSelection>>& selections,
    const std::vector<bool>& mayRetainStreamFlags,
    std::vector<std::shared_ptr<SampleStream>>& streams,
    std::vector<bool>& streamResetFlags,
    int64_t positionUs) {
  if (preparePositionOverrideUs != Definitions::kTimeUnset && positionUs == preparePositionUs) {
    positionUs = preparePositionOverrideUs;
    preparePositionOverrideUs = Definitions::kTimeUnset;
  }
  if (mediaPeriod == nullptr) {
    return positionUs;
  }
  return mediaPeriod
      ->selectTracks(selections, mayRetainStreamFlags, streams, streamResetFlags, positionUs);
}

void MaskingMediaPeriod::discardBuffer(int64_t positionUs, bool toKeyframe) {
  mediaPeriod->discardBuffer(positionUs, toKeyframe);
}

int64_t MaskingMediaPeriod::readDiscontinuity() {
  return mediaPeriod->readDiscontinuity();
}

int64_t MaskingMediaPeriod::getBufferedPositionUs() {
  return mediaPeriod->getBufferedPositionUs();
}

int64_t MaskingMediaPeriod::seekToUs(int64_t positionUs) {
  return mediaPeriod->seekToUs(positionUs);
}

int64_t MaskingMediaPeriod::getAdjustedSeekPositionUs(
    int64_t positionUs, const SeekParameters& seekParameters) {
  return mediaPeriod->getAdjustedSeekPositionUs(positionUs, seekParameters);
}

int64_t MaskingMediaPeriod::getNextLoadPositionUs() {
  return mediaPeriod->getNextLoadPositionUs();
}

void MaskingMediaPeriod::reevaluateBuffer(int64_t positionUs) {
  if (mediaPeriod) {
    mediaPeriod->reevaluateBuffer(positionUs);
  }
}

bool MaskingMediaPeriod::continueLoading(int64_t positionUs) {
  return mediaPeriod != nullptr && mediaPeriod->continueLoading(positionUs);
}

void MaskingMediaPeriod::onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& period) {
  if (callback) {
    callback->onContinueLoadingRequested(shared_from_this());
  }
}

void MaskingMediaPeriod::maybeThrowPrepareError() {
  // The wrapped period owns the failure, so it is the one that reports it.
  if (mediaPeriod) {
    mediaPeriod->maybeThrowPrepareError();
  }
}

std::string MaskingMediaPeriod::getPrepareError() const {
  return mediaPeriod != nullptr ? mediaPeriod->getPrepareError() : std::string();
}

void MaskingMediaPeriod::onPrepared(const std::shared_ptr<MediaPeriod>& period) {
  if (callback) {
    callback->onPrepared(shared_from_this());
  }
  if (listener) {
    listener->onPrepareComplete(id);
  }
}

int64_t MaskingMediaPeriod::getPreparePositionWithOverride(int64_t newPreparePositionUs) {
  return preparePositionOverrideUs != Definitions::kTimeUnset
         ? preparePositionOverrideUs
         : newPreparePositionUs;
}

} // namespace coral
