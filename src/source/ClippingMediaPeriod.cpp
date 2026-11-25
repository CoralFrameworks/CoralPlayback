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

#include <coral/source/ClippingMediaPeriod.h>

#include <media/Types.h>

#include <cassert>

namespace coral {

ClippingMediaPeriod::ClippingMediaPeriod(
    const std::shared_ptr<MediaPeriod>& mediaPeriod,
    bool enableInitialDiscontinuity, int64_t startPositionUs,
    int64_t endPositionUs) noexcept
    : mediaPeriod(mediaPeriod),
      mCallback(nullptr),
      pendingInitialDiscontinuityPositionUs(
          enableInitialDiscontinuity ? startPositionUs : Definitions::kTimeUnset),
      startPositionUs(startPositionUs),
      endPositionUs(endPositionUs) {}

ClippingMediaPeriod::~ClippingMediaPeriod() { mediaPeriod.reset(); }

void ClippingMediaPeriod::releaseChildPeriod() {
  mediaPeriod.reset();
  sampleStreams.clear();
}
void ClippingMediaPeriod::updateClipping(int64_t startUs, int64_t endUs) {
  this->startPositionUs = startUs;
  this->endPositionUs = endUs;
}

void ClippingMediaPeriod::prepare(Callback* callback, int64_t positionUs) {
  this->mCallback = callback;
  mediaPeriod->prepare(this, positionUs);
}

void ClippingMediaPeriod::onPrepared(const std::shared_ptr<MediaPeriod>& source) {
  if (mCallback) {
    mCallback->onPrepared(shared_from_this());
  }
}

const std::shared_ptr<TrackGroupArray>&
ClippingMediaPeriod::getTrackGroups() const {
  return mediaPeriod->getTrackGroups();
}

int64_t ClippingMediaPeriod::selectTracks(
    const std::vector<std::shared_ptr<TrackSelection>>& selections,
    const std::vector<bool>& mayRetainStreamFlags,
    std::vector<std::shared_ptr<SampleStream>>& streams,
    std::vector<bool>& streamResetFlags, int64_t positionUs) {
  if (mediaPeriod == nullptr) {
    for (auto& stream: streams) stream = nullptr;
    sampleStreams.clear();
    return positionUs;
  }
  sampleStreams.resize(streams.size());
  std::vector<std::shared_ptr<SampleStream>> childStreams(streams.size());
  for (size_t i = 0; i < streams.size(); i++) {
    sampleStreams[i] = std::dynamic_pointer_cast<ClippingSampleStream>(streams[i]);
    childStreams[i] = sampleStreams[i] != nullptr ? sampleStreams[i]->childStream : nullptr;
  }
  auto enablePositionUs =
      mediaPeriod->selectTracks(selections, mayRetainStreamFlags, childStreams,
          streamResetFlags, positionUs);
  pendingInitialDiscontinuityPositionUs =
      isPendingInitialDiscontinuity() && positionUs == startPositionUs
      && shouldKeepInitialDiscontinuity(startPositionUs, selections)
      ? enablePositionUs
      : Definitions::kTimeUnset;
  assert(enablePositionUs == positionUs || (
      enablePositionUs >= startPositionUs
      && (endPositionUs == Definitions::kTimeEndOfSource || enablePositionUs <= endPositionUs)));
  for (size_t i = 0; i < streams.size(); i++) {
    if (childStreams[i] == nullptr) {
      sampleStreams[i] = nullptr;
    } else if (sampleStreams[i] == nullptr || sampleStreams[i]->childStream != childStreams[i]) {
      sampleStreams[i] = std::make_shared<ClippingSampleStream>(this, childStreams[i]);
    }
    streams[i] = sampleStreams[i];
  }
  return enablePositionUs;
}

void ClippingMediaPeriod::discardBuffer(int64_t positionUs, bool toKeyframe) {
  mediaPeriod->discardBuffer(positionUs, toKeyframe);
}

int64_t ClippingMediaPeriod::readDiscontinuity() {
  if (isPendingInitialDiscontinuity()) {
    auto initialDiscontinuityUs = pendingInitialDiscontinuityPositionUs;
    pendingInitialDiscontinuityPositionUs = Definitions::kTimeUnset;
    // Always read an initial discontinuity from the child, and use it if set.
    auto childDiscontinuityUs = readDiscontinuity();
    return childDiscontinuityUs != Definitions::kTimeUnset
           ? childDiscontinuityUs
           : initialDiscontinuityUs;
  }
  int64_t discontinuityUs = mediaPeriod->readDiscontinuity();
  if (discontinuityUs == Definitions::kTimeUnset) {
    return Definitions::kTimeUnset;
  }
  assert(discontinuityUs >= startPositionUs);
  assert(endPositionUs == Definitions::kTimeEndOfSource || discontinuityUs <= endPositionUs);
  return discontinuityUs;
}

int64_t ClippingMediaPeriod::seekToUs(int64_t positionUs) {
  pendingInitialDiscontinuityPositionUs = Definitions::kTimeUnset;
  for (auto& sampleStream: sampleStreams) {
    if (sampleStream != nullptr) {
      sampleStream->clearSentEos();
    }
  }
  int64_t seekUs = mediaPeriod->seekToUs(positionUs);
  assert(seekUs == positionUs || (
      seekUs >= startPositionUs
      && (endPositionUs == Definitions::kTimeEndOfSource || seekUs <= endPositionUs)));
  return seekUs;
}

int64_t ClippingMediaPeriod::getAdjustedSeekPositionUs(
    int64_t positionUs, const SeekParameters& seekParameters) {
  if (positionUs == startPositionUs) {
    return startPositionUs;
  }
  auto clippedSeekParameters = clipSeekParameters(positionUs, seekParameters);
  return mediaPeriod->getAdjustedSeekPositionUs(positionUs, clippedSeekParameters);
}

void ClippingMediaPeriod::onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& source) {
  if (mCallback) {
    mCallback->onContinueLoadingRequested(shared_from_this());
  }
}

void ClippingMediaPeriod::maybeThrowPrepareError() {
  // The clipped period owns the failure, so it is the one that reports it.
  if (mediaPeriod != nullptr) {
    mediaPeriod->maybeThrowPrepareError();
  }
}

std::string ClippingMediaPeriod::getPrepareError() const {
  return mediaPeriod != nullptr ? mediaPeriod->getPrepareError() : std::string();
}

int64_t ClippingMediaPeriod::getBufferedPositionUs() {
  auto bufferedPositionUs = mediaPeriod->getBufferedPositionUs();
  if (bufferedPositionUs == Definitions::kTimeEndOfSource
      || (endPositionUs != Definitions::kTimeEndOfSource && bufferedPositionUs >= endPositionUs)) {
    return Definitions::kTimeEndOfSource;
  }
  return bufferedPositionUs;
}

int64_t ClippingMediaPeriod::getNextLoadPositionUs() {
  auto nextLoadPositionUs = mediaPeriod->getNextLoadPositionUs();
  if (nextLoadPositionUs == Definitions::kTimeEndOfSource ||
      (
          endPositionUs != Definitions::kTimeEndOfSource &&
          nextLoadPositionUs >= endPositionUs)) {
    return Definitions::kTimeEndOfSource;
  }
  return nextLoadPositionUs;
}

bool ClippingMediaPeriod::continueLoading(int64_t positionUs) {
  return mediaPeriod->continueLoading(positionUs);
}

bool ClippingMediaPeriod::isLoading() {
  return mediaPeriod->isLoading();
}

void ClippingMediaPeriod::reevaluateBuffer(int64_t positionUs) {
  mediaPeriod->reevaluateBuffer(positionUs);
}

SeekParameters ClippingMediaPeriod::clipSeekParameters(
    int64_t positionUs, const SeekParameters& seekParameters) const {
  auto toleranceBeforeUs = std::clamp(seekParameters.toleranceBeforeUs,
      int64_t(0), positionUs - startPositionUs);
  auto toleranceAfterUs =
      std::clamp(seekParameters.toleranceAfterUs, int64_t(0),
          endPositionUs == Definitions::kTimeEndOfSource
          ? INT64_MAX
          : endPositionUs - positionUs);
  if (toleranceBeforeUs == seekParameters.toleranceBeforeUs &&
      toleranceAfterUs == seekParameters.toleranceAfterUs) {
    return seekParameters;
  } else {
    return SeekParameters(toleranceBeforeUs, toleranceAfterUs);
  }
}

bool ClippingMediaPeriod::shouldKeepInitialDiscontinuity(
    int64_t startUs,
    const std::vector<std::shared_ptr<TrackSelection>>& selections) {
  // If the clipping start position is non-zero, the clipping sample streams
  // will adjust timestamps on buffers they read from the unclipped sample
  // streams. These adjusted buffer timestamps can be negative, because sample
  // streams provide buffers starting at a key-frame, which may be before the
  // clipping start point. When the renderer reads a buffer with a negative
  // timestamp, its offset timestamp can jump backwards compared to the last
  // timestamp read in the previous period. Renderer implementations may not
  // allow this, so we signal a discontinuity which resets the renderers before
  // they read the clipping sample stream. However, for tracks where all samples
  // are sync samples, we assume they have random access seek behaviour and do
  // not need an initial discontinuity to reset the renderer.
  if (startUs != 0) {
    for (auto const& trackSelection: selections) {
      if (trackSelection != nullptr) {
        auto const& selectedFormat = trackSelection->getSelectedFormat();
        auto& availableMimeTypes = MimeTypes::audioAvailableMimeTypes();
        for (auto& mime: availableMimeTypes) {
          if (mime == selectedFormat->getMimeType()) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

//--------------------------------------------------------------------------------------------------

SampleStream::ReadDataResult
ClippingMediaPeriod::ClippingSampleStream::readData(FormatHolder& formatHolder,
  AVPacketBuffer* buffer,
  const bool formatRequired) {
  if (mediaSource->isPendingInitialDiscontinuity()) {
    return ReadDataResult::kResultNothingRead;
  }

  if (sentEos) {
    buffer->setFlags(AVBufferFlags::kEndOfStream);
    return ReadDataResult::kResultBufferRead;
  }

  auto result = childStream->readData(formatHolder, buffer, formatRequired);
  if (result == ReadDataResult::kResultFormatRead) {
    auto format = formatHolder.format;
    if (format == nullptr) {
      return ReadDataResult::kResultNothingRead;
    }
    if (format->getEncoderDelay() != 0 || format->getEncoderPadding() != 0) {
      // Clear gapless playback metadata if the start/end points don't match the
      // media.
      int encoderDelay =
          mediaSource->startPositionUs != 0 ? 0 : format->getEncoderDelay();
      int encoderPadding =
          (mediaSource->endPositionUs != Definitions::kTimeEndOfSource)
          ? 0
          : format->getEncoderPadding();
      format->setEncoderDelay(encoderDelay);
      format->setEncoderPadding(encoderPadding);
    }
    return ReadDataResult::kResultFormatRead;
  }
  if (mediaSource->endPositionUs != Definitions::kTimeEndOfSource &&
      ((
           result == ReadDataResult::kResultBufferRead &&
           buffer->timeUs() >= mediaSource->endPositionUs) ||
       (
           result == ReadDataResult::kResultNothingRead &&
           mediaSource->getBufferedPositionUs() == Definitions::kTimeEndOfSource &&
           !buffer->isWaitingForKeys()))) {
    buffer->clear();
    buffer->setFlags(AVBufferFlags::kEndOfStream);
    sentEos = true;
    return ReadDataResult::kResultBufferRead;
  }
  return result;
}

} // namespace coral
