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

#include <coral/source/SampleQueue.h>

#include <cassert>
#include <mutex>
#include <sstream>

namespace coral {
namespace {

bool sameFormatContent(const std::shared_ptr<MediaFormat>& first,
    const std::shared_ptr<MediaFormat>& second) {
  if (first == second) return true;
  if (first == nullptr || second == nullptr) return false;
  // Only subtitle formats are compared by content here. Audio and video formats carry codec-specific
  // data that is not exposed, and a pointer change there is a real format change that must reach the
  // renderer.
  if (!first->isSubtitle() || !second->isSubtitle()) return false;
  return first->getMimeType() == second->getMimeType()
         && first->getLanguage() == second->getLanguage()
         && first->getLabel() == second->getLabel()
         && first->getId() == second->getId()
         && first->getSelectionFlags() == second->getSelectionFlags()
         && first->getRoleFlags() == second->getRoleFlags()
         && first->getDurationUs() == second->getDurationUs()
         && first->getTrackId() == second->getTrackId()
         && first->getFrameCount() == second->getFrameCount()
         && first->getSampleRate() == second->getSampleRate()
         && first->getChannelCount() == second->getChannelCount()
         && first->getWidth() == second->getWidth()
         && first->getHeight() == second->getHeight()
         && first->getSubsampleOffsetUs() == second->getSubsampleOffsetUs()
         && first->getCodecs() == second->getCodecs();
}

} // namespace

SampleQueue::SampleQueue(const std::shared_ptr<Allocator>& allocator, MediaType type) noexcept
    : mediaType(type),
      sampleDataQueue(allocator),
      upstreamFormatChangedListener(nullptr),
      length(0),
      absoluteFirstIndex(0),
      readPosition(0),
      startTimeUs(INT64_MIN),
      pendingDiscontinuityPositionUs(Definitions::kTimeUnset),
      largestQueuedTimestampUs(INT64_MIN),
      largestDiscardedTimestampUs(INT64_MIN),
      lastSampleQueued(false),
      upstreamKeyframeRequired(true),
      upstreamFormatRequired(true),
      sampleOffsetUs(0) {
}

SampleQueue::~SampleQueue() {
  release();
  upstreamFormatChangedListener = nullptr;
}

void SampleQueue::reset(bool resetUpstreamFormat) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  sampleDataQueue.reset();
  metaInfoQueue.clear();
  length = 0;
  absoluteFirstIndex = 0;
  readPosition = 0;
  startTimeUs = INT64_MIN;
  pendingDiscontinuityPositionUs = Definitions::kTimeUnset;
  largestQueuedTimestampUs = INT64_MIN;
  largestDiscardedTimestampUs = INT64_MIN;
  lastSampleQueued = false;

  if (resetUpstreamFormat) {
    upstreamFormat.reset();
    upstreamKeyframeRequired = true;
    downstreamFormat.reset();
  }
}

void SampleQueue::discardUpstreamSamples(int discardFromIndex) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  sampleDataQueue.discardUpstreamSampleBytes(discardUpstreamSampleMetadata(discardFromIndex));
}

void SampleQueue::discardUpstreamFrom(int64_t timeUs) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (length == 0) {
    return;
  }
  assert(timeUs > getLargestReadTimestampUs());
  int retainCount = countUnreadSamplesBefore(timeUs);
  const auto discardFromIndex = absoluteFirstIndex + retainCount;
  // The media that is about to leave the queue is reported before it is gone, so that a listener can
  // say which part of the media was given up.
  if (upstreamDiscardedListener != nullptr && discardFromIndex < getWriteIndex()) {
    const auto mediaStartTimeUs = metaInfoQueue[discardFromIndex - absoluteFirstIndex].timeUs;
    const auto mediaEndTimeUs = getLargestQueuedTimestampUs();
    upstreamDiscardedListener->onUpstreamDiscarded(mediaStartTimeUs, mediaEndTimeUs);
  }
  discardUpstreamSamples(discardFromIndex);
}

void SampleQueue::maybeThrowError() {}

const std::shared_ptr<MediaFormat>& SampleQueue::getUpstreamFormat() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  static const std::shared_ptr<MediaFormat> sEmptyFormat = nullptr;
  return upstreamFormatRequired ? sEmptyFormat : upstreamFormat;
}

int64_t SampleQueue::getLargestReadTimestampUs() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return std::max(largestDiscardedTimestampUs, getLargestTimestamp(readPosition));
}

int64_t SampleQueue::getFirstTimestampUs() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return length == 0 ? INT64_MIN : metaInfoQueue.front().timeUs;
}

bool SampleQueue::isReady(bool loadingFinished) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!hasNextSample()) {
    return loadingFinished || lastSampleQueued;
  }
  int relativeReadIndex = getRelativeIndex(readPosition);
  if (metaInfoQueue[relativeReadIndex].format != downstreamFormat) {
    // A format can be read.
    return true;
  }
  return mayReadSample(relativeReadIndex);
}

int SampleQueue::readSample(FormatHolder& formatHolder,
    AVPacketBuffer* packetBuffer,
    bool formatRequired,
    bool loadingFinished) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto result = readSampleMetadata(formatHolder, packetBuffer, formatRequired,
      loadingFinished, mExtrasHolder);
  if (result == Definitions::kResultBufferRead &&
      !packetBuffer->isEndOfStream() && !packetBuffer->flagsOnly()) {
    sampleDataQueue.readToBuffer(packetBuffer, mExtrasHolder);
  }
  return result;
}

bool SampleQueue::seekTo(int sampleIndex) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  rewind();
  if (sampleIndex < absoluteFirstIndex || sampleIndex > getWriteIndex()) {
    return false;
  }
  startTimeUs = INT64_MIN;
  readPosition = sampleIndex - absoluteFirstIndex;
  return true;
}

bool SampleQueue::seekTo(int64_t timeUs, bool allowTimeBeyondBuffer) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  rewind();
  int relativeReadIndex = getRelativeIndex(readPosition);
  if (!hasNextSample() || timeUs < metaInfoQueue[relativeReadIndex].timeUs ||
      (timeUs > largestQueuedTimestampUs && !allowTimeBeyondBuffer)) {
    return false;
  }
  int offset =
      findSampleBefore(relativeReadIndex, length - readPosition, timeUs,
          /* keyframe= */ true);
  if (offset == Definitions::kPositionUnset) {
    return false;
  }
  startTimeUs = timeUs;
  readPosition += offset;
  return true;
}

int SampleQueue::getSkipCount(int64_t timeUs, bool allowEndOfQueue) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  int relativeReadIndex = getRelativeIndex(readPosition);
  if (!hasNextSample() || timeUs < metaInfoQueue[relativeReadIndex].timeUs) {
    return 0;
  }
  if (timeUs > largestQueuedTimestampUs && allowEndOfQueue) {
    return length - readPosition;
  }
  int offset =
      findSampleBefore(relativeReadIndex, length - readPosition, timeUs,
          /* keyframe= */ true);
  if (offset == Definitions::kPositionUnset) {
    return 0;
  }
  return offset;
}

void SampleQueue::skip(int count) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  assert(count >= 0 && readPosition + count <= length);
  readPosition += count;
}

void SampleQueue::discardTo(int64_t timeUs, bool toKeyframe,
    bool stopAtReadPosition) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  sampleDataQueue.discardDownstreamTo(
      discardSampleMetadataTo(timeUs, toKeyframe, stopAtReadPosition));
}

void SampleQueue::discardToRead() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  sampleDataQueue.discardDownstreamTo(discardSampleMetadataToRead());
}

void SampleQueue::discardToEnd() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  sampleDataQueue.discardDownstreamTo(discardSampleMetadataToEnd());
}

void SampleQueue::setSampleOffsetUs(const int64_t newSampleOffsetUs) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  // Same as ExoPlayer: only a change of the offset invalidates the upstream format adjustment that
  // was applied to the format currently held. The parameter used to shadow the member, so the
  // comparison was always false and the offset was never applied.
  if (sampleOffsetUs != newSampleOffsetUs) {
    sampleOffsetUs = newSampleOffsetUs;
    invalidateUpstreamFormatAdjustment();
  }
}

std::shared_ptr<MediaFormat>
SampleQueue::getAdjustedUpstreamFormat(const std::shared_ptr<MediaFormat>& format) {
  // Same as ExoPlayer's SampleQueue: carry the sample offset into the format's subsample offset so
  // that everything downstream of the queue (and the renderers that use the format's offset) sees
  // the same timeline. A format that asks for per-sample offsets is left untouched.
  if (sampleOffsetUs != 0 && format != nullptr
      && format->getSubsampleOffsetUs() != MediaFormat::OFFSET_SAMPLE_RELATIVE) {
    auto adjustedFormat = format->buildUpon();
    adjustedFormat->setSubsampleOffsetUs(format->getSubsampleOffsetUs() + sampleOffsetUs);
    return adjustedFormat;
  }
  return format;
}

void SampleQueue::setUpstreamFormatChangeListener(UpstreamFormatChangedListener* listener) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  upstreamFormatChangedListener = listener;
}

void SampleQueue::setFormat(const std::shared_ptr<MediaFormat>& format) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (format == nullptr) {
    return;
  }
  auto adjustedFormat = getAdjustedUpstreamFormat(format);
  if (upstreamFormat != nullptr) {
    if (upstreamFormat == adjustedFormat) {
      return;
    }
    if (sameFormatContent(upstreamFormat, adjustedFormat)) {
      // Keep the pointer already stored in the queued sample metadata. Replacing an equivalent
      // format here would make readSampleMetadata believe every queued sample carries a new
      // format and return format instead of a buffer forever.
      upstreamFormatRequired = false;
      return;
    }
  }
  upstreamFormat.reset();
  upstreamFormat = std::move(adjustedFormat);
  upstreamFormatAdjustmentRequired = false;
  upstreamFormatRequired = false;
  if (upstreamFormatChangedListener != nullptr) {
    upstreamFormatChangedListener->onUpstreamFormatChanged(upstreamFormat);
  }
}

void SampleQueue::consume(const AVSample& sample) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (sample.isEndOfStream()) {
    // The end of the stream is not a sample: the queue derives it from the state of the load, see
    // readSampleMetadata, so that the renderers see it exactly once.
    return;
  }
  appendSampleData(sample.array(), static_cast<int>(sample.size()), sample.timeUs,
      toBufferFlags(sample.flags));
}

void SampleQueue::appendSampleData(const uint8_t* data, const int size, int64_t timeUs, const AVBufferFlags flags) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  sampleDataQueue.sampleData(data, size);
  if (upstreamFormatAdjustmentRequired) {
    setFormat(upstreamFormat);
  }
  bool isKeyframe = (flags & AVBufferFlags::kKeyFrame) != 0;
  if (upstreamKeyframeRequired) {
    if (!isKeyframe) {
      return;
    }
    upstreamKeyframeRequired = false;
  }
  timeUs += sampleOffsetUs;
  const int64_t absoluteOffset = sampleDataQueue.getTotalBytesWritten() - size;
  commitSample(timeUs, flags, absoluteOffset, size);
}

std::string SampleQueue::getName() const {
  std::stringstream out;
  out << "SampleQueue(" << convertToText(mediaType) << ")";
  return out.str();
}

void SampleQueue::rewind() {
  readPosition = 0;
  sampleDataQueue.rewind();
}

int SampleQueue::readSampleMetadata(FormatHolder& formatHolder,
    AVPacketBuffer* packetBuffer,
    bool formatRequired,
    bool loadingFinished,
    SampleExtrasHolder& sampleExtrasHolder) {
  assert(packetBuffer != nullptr);
  packetBuffer->clear();
  if (!hasNextSample()) {
    if (loadingFinished || lastSampleQueued) {
      packetBuffer->setFlags(AVBufferFlags::kEndOfStream);
      return Definitions::kResultBufferRead;
    } else if (upstreamFormat != nullptr
               && (formatRequired || upstreamFormat != downstreamFormat)) {
      onFormatResult(upstreamFormat, formatHolder);
      return Definitions::kResultFormatRead;
    } else {
      return Definitions::kResultNothingRead;
    }
  }

  int relativeReadIndex = getRelativeIndex(readPosition);
  // A sample is queued with the format the queue held when it was appended, which is the format the
  // extractor declared for the track. A sample that was appended before any format was known has none,
  // so the format the queue holds now is reported for it: handing out a null format would make the
  // consumer use whatever it had before, or dereference null as the audio renderer did.
  const auto& sampleFormat = metaInfoQueue[relativeReadIndex].format != nullptr
                               ? metaInfoQueue[relativeReadIndex].format
                               : upstreamFormat;
  if (sampleFormat == nullptr) {
    // Nothing can be read before a format is known.
    return Definitions::kResultNothingRead;
  }
  if (formatRequired || sampleFormat != upstreamFormat) {
    onFormatResult(sampleFormat, formatHolder);
    return Definitions::kResultFormatRead;
  }

  if (!mayReadSample(relativeReadIndex)) {
    packetBuffer->setWaitingForKeys(true);
    return Definitions::kResultNothingRead;
  }

  packetBuffer->setFlags(metaInfoQueue[relativeReadIndex].flags);
  packetBuffer->timeUs(metaInfoQueue[relativeReadIndex].timeUs);
  if (packetBuffer->timeUs() < startTimeUs) {
    packetBuffer->addFlag(AVBufferFlags::kDiscard);
  }
  // A pending discontinuity is the same as ExoPlayer's: the samples before the seek target are
  // decoded (they are what the sample at the target has to be decoded against) but must not reach
  // the output. Flag rather than drop them, so that the decoders still see them. The samples after
  // the target carry no discontinuity, so the marker is cleared on the first of them.
  if (pendingDiscontinuityPositionUs != Definitions::kTimeUnset) {
    if (packetBuffer->timeUs() < pendingDiscontinuityPositionUs) {
      packetBuffer->addFlag(AVBufferFlags::kDiscard);
    } else {
      pendingDiscontinuityPositionUs = Definitions::kTimeUnset;
    }
  }
  if (packetBuffer->flagsOnly()) {
    return Definitions::kResultBufferRead;
  }
  sampleExtrasHolder.size = metaInfoQueue[relativeReadIndex].size;
  sampleExtrasHolder.offset = metaInfoQueue[relativeReadIndex].offset;

  ++readPosition;
  return Definitions::kResultBufferRead;
}

int64_t SampleQueue::discardSampleMetadataTo(
    int64_t timeUs, bool toKeyframe, bool stopAtReadPosition) {
  if (length == 0 || timeUs < metaInfoQueue.front().timeUs) {
    return Definitions::kPositionUnset;
  }
  int searchLength =
      stopAtReadPosition && readPosition != length ? readPosition + 1 : length;
  int discardCount = findSampleBefore(0, searchLength, timeUs, toKeyframe);
  if (discardCount == Definitions::kPositionUnset) {
    return Definitions::kPositionUnset;
  }
  return discardSamples(discardCount);
}

int64_t SampleQueue::discardSampleMetadataToRead() {
  if (readPosition == 0) {
    return Definitions::kPositionUnset;
  }
  return discardSamples(readPosition);
}

int64_t SampleQueue::discardSampleMetadataToEnd() {
  if (length == 0) {
    return Definitions::kPositionUnset;
  }
  return discardSamples(length);
}

void SampleQueue::commitSample(int64_t timeUs, AVBufferFlags sampleFlags, int64_t offset, int size) {
  if (length > 0) {
    // Ensure sample data doesn't overlap.
    assert(metaInfoQueue.back().offset + metaInfoQueue.back().size <= offset);
  }

  //    // TODO how to indicates last sample? different with end of stream
  lastSampleQueued = (sampleFlags & AVBufferFlags::kLastSample) != 0;
  largestQueuedTimestampUs = std::max(largestQueuedTimestampUs, timeUs);

  // The sample is appended to the queue, which neither frees nor moves the samples that are already in
  // it: the playback thread reads them while this thread appends.
  metaInfoQueue.emplace_back(offset, size, timeUs, sampleFlags, upstreamFormat);
  ++length;
}

int64_t SampleQueue::discardUpstreamSampleMetadata(int discardFromIndex) {
  int discardCount = getWriteIndex() - discardFromIndex;
  assert(0 <= discardCount && discardCount <= (length - readPosition));
  length -= discardCount;
  largestQueuedTimestampUs =
      std::max(largestDiscardedTimestampUs, getLargestTimestamp(length));
  lastSampleQueued = (discardCount == 0 && lastSampleQueued);
  for (int i = 0; i < discardCount; ++i) {
    metaInfoQueue.pop_back();
  }
  if (length != 0) {
    return metaInfoQueue.back().offset + metaInfoQueue.back().size;
  }
  return 0;
}

void SampleQueue::onFormatResult(const std::shared_ptr<MediaFormat>& newFormat,
    FormatHolder& outputFormatHolder) {
  downstreamFormat.reset();
  downstreamFormat = newFormat;
  outputFormatHolder.format = newFormat;
  // TODO handle DRM session
}

bool SampleQueue::mayReadSample(int relativeReadIndex) const {
  // TODO handle DRM session
  return true;
}

int SampleQueue::findSampleBefore(int relativeStartIndex,
    int length,
    int64_t timeUs,
    bool keyframe) {
  int sampleCountToTarget = Definitions::kPositionUnset;
  int searchIndex = relativeStartIndex;
  for (int i = 0; i < length && metaInfoQueue[searchIndex].timeUs <= timeUs;
       i++) {
    if (!keyframe || (
                         metaInfoQueue[searchIndex].flags &
                         AVBufferFlags::kKeyFrame) != 0) {
      // We've found a suitable sample.
      sampleCountToTarget = i;
    }
    searchIndex++;
  }
  return sampleCountToTarget;
}

int SampleQueue::countUnreadSamplesBefore(int64_t timeUs) {
  int count = length;
  int relativeSampleIndex = length - 1;
  while (count > readPosition && metaInfoQueue[relativeSampleIndex].timeUs >= timeUs) {
    count--;
    relativeSampleIndex--;
  }
  return count;
}

int64_t SampleQueue::discardSamples(int discardCount) {
  largestDiscardedTimestampUs =
      std::max(largestDiscardedTimestampUs, getLargestTimestamp(discardCount));
  int64_t sampleOffset = 0;
  if (discardCount == length) {
    // The whole queue is discarded, and the position of the media that is left is the end of the last
    // sample that is discarded.
    sampleOffset = metaInfoQueue.back().offset + metaInfoQueue.back().size;
  }
  length -= discardCount;
  absoluteFirstIndex += discardCount;
  readPosition -= discardCount;
  if (readPosition < 0) {
    readPosition = 0;
  }
  for (int i = 0; i < discardCount; ++i) {
    metaInfoQueue.pop_front();
  }
  if (length == 0) {
    return sampleOffset;
  }
  return metaInfoQueue.front().offset;
}

int64_t SampleQueue::getLargestTimestamp(int length) {
  if (length == 0) {
    return INT64_MIN;
  }
  int64_t largestTimestampUs = INT64_MIN;
  int relativeSampleIndex = length - 1;
  for (int i = 0; i < length; i++) {
    largestTimestampUs = std::max(largestTimestampUs, metaInfoQueue[relativeSampleIndex].timeUs);
    if ((metaInfoQueue[relativeSampleIndex].flags & AVBufferFlags::kKeyFrame) != 0) {
      break;
    }
    relativeSampleIndex--;
  }
  return largestTimestampUs;
}

int SampleQueue::getRelativeIndex(int offset) const {
  return offset;
}

} // namespace coral
