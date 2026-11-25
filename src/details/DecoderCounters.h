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

#ifndef CORAL_PLAYBACK_DETAILS_DECODER_COUNTERS_H
#define CORAL_PLAYBACK_DETAILS_DECODER_COUNTERS_H

#include <cstdint>

namespace coral {

/**
 * Maintains decoder event counts, for debugging purposes only.
 *
 * <p>Counters should be written from the playback thread only. Counters may be
 * read from any thread. To ensure that the counter values are made visible
 * across threads, users of this class should invoke {@link #ensureUpdated()}
 * prior to reading and after writing.
 */
class DecoderCounters final {
public:
  /**
   * The number of times a decoder has been initialized.
   */
  int decoderInitCount;
  /**
   * The number of times a decoder has been released.
   */
  int decoderReleaseCount;
  /**
   * The number of queued input buffers.
   */
  int inputBufferCount;
  /**
   * The number of skipped input buffers.
   * <p>
   * A skipped input buffer is an input buffer that was deliberately not sent to
   * the decoder.
   */
  int skippedInputBufferCount;
  /**
   * The number of rendered output buffers.
   */
  int renderedOutputBufferCount;
  /**
   * The number of skipped output buffers.
   * <p>
   * A skipped output buffer is an output buffer that was deliberately not
   * rendered.
   */
  int skippedOutputBufferCount;
  /**
   * The number of dropped buffers.
   * <p>
   * A dropped buffer is an buffer that was supposed to be decoded/rendered, but
   * was instead dropped because it could not be rendered in time.
   */
  int droppedBufferCount;
  /**
   * The maximum number of dropped buffers without an interleaving rendered
   * output buffer. <p> Skipped output buffers are ignored for the purposes of
   * calculating this value.
   */
  int maxConsecutiveDroppedBufferCount;
  /**
   * The number of times all buffers to a keyframe were dropped.
   * <p>
   * Each time buffers to a keyframe are dropped, this counter is increased by
   * one, and the dropped buffer counters are increased by one (for the current
   * output buffer) plus the number of buffers dropped from the source to
   * advance to the keyframe.
   */
  int droppedToKeyframeCount;
  /**
   * The sum of the video frame processing offsets in microseconds.
   *
   * <p>The processing offset for a video frame is the difference between the
   * time at which the frame became available to render, and the time at which
   * it was scheduled to be rendered. A positive value indicates the frame
   * became available early enough, whereas a negative value indicates that the
   * frame wasn't available until after the time at which it should have been
   * rendered.
   *
   * <p>Note: Use {@link #addVideoFrameProcessingOffset(long)} to update this
   * field instead of updating it directly.
   */
  int64_t totalVideoFrameProcessingOffsetUs;
  /**
   * The number of video frame processing offsets added.
   *
   * <p>Note: Use {@link #addVideoFrameProcessingOffset(long)} to update this
   * field instead of updating it directly.
   */
  int videoFrameProcessingOffsetCount;

  DecoderCounters() {
    decoderInitCount = 0;
    decoderReleaseCount = 0;
    inputBufferCount = 0;
    skippedInputBufferCount = 0;
    renderedOutputBufferCount = 0;
    skippedOutputBufferCount = 0;
    droppedBufferCount = 0;
    maxConsecutiveDroppedBufferCount = 0;
    droppedToKeyframeCount = 0;
    totalVideoFrameProcessingOffsetUs = 0;
    videoFrameProcessingOffsetCount = 0;
  }

  DecoderCounters(const DecoderCounters& other) = default;
  DecoderCounters& operator=(const DecoderCounters& other) = default;

  DecoderCounters operator+(const DecoderCounters& other) {
    this->decoderInitCount += other.decoderInitCount;
    this->decoderReleaseCount += other.decoderReleaseCount;
    this->inputBufferCount += other.inputBufferCount;
    this->skippedInputBufferCount += other.skippedInputBufferCount;
    this->renderedOutputBufferCount += other.renderedOutputBufferCount;
    this->skippedOutputBufferCount += other.skippedOutputBufferCount;
    this->droppedBufferCount += other.droppedBufferCount;
    this->maxConsecutiveDroppedBufferCount +=
      other.maxConsecutiveDroppedBufferCount;
    this->droppedToKeyframeCount += other.droppedToKeyframeCount;
    this->totalVideoFrameProcessingOffsetUs +=
      other.totalVideoFrameProcessingOffsetUs;
    this->videoFrameProcessingOffsetCount +=
      other.videoFrameProcessingOffsetCount;
    return *this;
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_DECODER_COUNTERS_H
