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

#ifndef CORAL_PLAYBACK_SOURCE_SAMPLE_QUEUE_H
#define CORAL_PLAYBACK_SOURCE_SAMPLE_QUEUE_H

#include <coral/source/FormatHolder.h>
#include <coral/source/SampleDataQueue.h>
#include <coral/upstream/Allocator.h>

#include <extractor/Extractor.h>

#include <media/PacketBuffer.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace coral {

/**
 * A holder for sample metadata not held by {@link AVPacketBuffer}.
 */
struct SampleExtrasHolder {
  int size;
  int offset;

  SampleExtrasHolder() : SampleExtrasHolder(0, 0) {}

  explicit SampleExtrasHolder(int size, int offset) noexcept
      : size(size),
        offset(offset) {}

  SampleExtrasHolder(const SampleExtrasHolder& other) = default;
  SampleExtrasHolder& operator=(const SampleExtrasHolder& other) = default;
};

/**
 * A stream for caching PacketBuffer
 *
 * <p>The queue is the consumer of a track of an extractor: it receives the format and the samples of
 * the track, and it keeps them until the renderers read them. A caller that holds the raw bytes of a
 * sample rather than an {@link AVSample}, e.g. the packet stream of the compositor, uses
 * {@link #appendSampleData}, which is the same queue without the sample of the extractor module.
 */
class SampleQueue final : public AVTrackOutput::Consumer {
public:
  /**
   * The queue owns the allocations it holds and keeps a raw pointer to its format listener, so a
   * copy would release every allocation twice and would let both copies call the same listener.
   */
  SampleQueue(const SampleQueue& other) = delete;
  SampleQueue& operator=(const SampleQueue& other) = delete;
  SampleQueue(SampleQueue&& other) = delete;
  SampleQueue& operator=(SampleQueue&& other) = delete;

  /**
   * A listener for changes to the upstream format.
   */
  class UpstreamFormatChangedListener {
  public:
    virtual ~UpstreamFormatChangedListener() = default;

    /**
     * Called on the loading thread when an upstream format change occurs.
     *
     * @param format The new upstream format.
     */
    virtual void onUpstreamFormatChanged(const std::shared_ptr<MediaFormat>& format) = 0;
  };

  /**
   * A listener of the media that is removed from the back of the queue.
   *
   * <p>This is what the period turns into ExoPlayer's
   * {@code MediaSourceEventListener.onUpstreamDiscarded}: media that was read ahead is given up so
   * that it can be re-buffered differently, e.g. in another format.
   */
  class UpstreamDiscardedListener {
  public:
    virtual ~UpstreamDiscardedListener() = default;

    /**
     * Called when media was removed from the back of the queue.
     *
     * @param mediaStartTimeUs The start time of the media that was discarded.
     * @param mediaEndTimeUs The end time of the media that was discarded.
     */
    virtual void onUpstreamDiscarded(int64_t mediaStartTimeUs, int64_t mediaEndTimeUs) = 0;
  };

  /**
   * Sets the listener of the media that is discarded from the back of the queue.
   *
   * @param listener The listener, or null to stop reporting.
   */
  void setUpstreamDiscardedListener(UpstreamDiscardedListener* listener) {
    upstreamDiscardedListener = listener;
  }

  explicit SampleQueue(const std::shared_ptr<Allocator>& allocator,
      MediaType type) noexcept;

  ~SampleQueue() override;

  /**
   * Returns the media type of the SampleQueue
   */
  [[nodiscard]] MediaType getMediaType() const { return mediaType; }

  void release() { reset(true); }

  /**
   * Clears all resources
   */
  void reset() { reset(false); }

  void reset(bool resetUpstreamFormat);

  /**
   * Sets the start time for the queue. Samples with earlier timestamps will be
   * discarded or have the {@link SampleBuffer:Flag::BUFFER_FLAG_DISCARD} flag
   * set when read.
   *
   * @param newStartTimeUs The new start time in microseconds.
   */
  void setStartTimeUs(int64_t newStartTimeUs) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    startTimeUs = newStartTimeUs;
  }

  /**
   * Marks a pending discontinuity: samples with a timestamp before
   * {@code discontinuityPositionUs} that are read from now on are flagged with
   * {@link AVBufferFlags::kDiscard} so that the renderers decode but do not render them.
   *
   * <p>This mirrors ExoPlayer's {@code SampleQueue.pendingDiscontinuityPositionUs} /
   * {@code C#BUFFER_FLAG_DECODE_ONLY}. The samples before the seek target are still needed: they
   * carry the reference frames that the first sample at the target has to be decoded against, so
   * they must be decoded. They only have to be kept out of the output, which is what the flag does.
   *
   * <p>The marker is cleared automatically once a sample at or after the position is read; the
   * samples queued after it are all past the discontinuity.
   *
   * @param discontinuityPositionUs The seek target in microseconds, or
   * {@link Definitions::kTimeUnset} to clear the marker.
   */
  void setPendingDiscontinuityPositionUs(int64_t discontinuityPositionUs) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    pendingDiscontinuityPositionUs = discontinuityPositionUs;
  }

  /**
   * Returns the current absolute write index.
   */
  [[nodiscard]] int getWriteIndex() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return absoluteFirstIndex + length;
  }

  /**
   * Discards samples from the write side of the queue.
   *
   * @param discardFromIndex The absolute index of the first sample to be
   * discarded. Must be in the range [{@link #getReadIndex()}, {@link
   * #getWriteIndex()}].
   */
  void discardUpstreamSamples(int discardFromIndex);

  /**
   * Discards samples from the write side of the queue.
   *
   * @param timeUs Samples will be discarded from the write end of the queue
   * until a sample with a timestamp smaller than timeUs is encountered (this
   * sample is not discarded). Must be larger than {@link
   * #getLargestReadTimestampUs()}.
   */
  void discardUpstreamFrom(int64_t timeUs);

  /**
   * Calls {@link #discardToEnd()} and releases any resources owned by the
   * queue.
   */
  void preRelease() { discardToEnd(); }

  /**
   * Throws an error that's preventing data from being read. Does nothing if no
   * such error exists.
   */
  void maybeThrowError();

  /**
   * Returns the current absolute start index
   */
  [[nodiscard]] int getFirstIndex() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return absoluteFirstIndex;
  }

  /**
   * Returns the current absolute read index
   */
  [[nodiscard]] int getReadIndex() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return absoluteFirstIndex + readPosition;
  }

  /**
   * Returns the upstream {@link MediaFormat} pointer in which samples are being
   * queued.
   */
  [[nodiscard]] const std::shared_ptr<MediaFormat>& getUpstreamFormat() const;

  /**
   * Returns the largest sample timestamp that has been queued since the last
   * {@link #reset}.
   *
   * @return The largest sample timestamp that has been queued, or {@link
   * INT64_MIN} if no samples have been queued.
   */
  [[nodiscard]] int64_t getLargestQueuedTimestampUs() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return largestQueuedTimestampUs;
  }

  /**
   * Returns the largest sample timestamp that has been read since the last
   * {@link #clear}.
   *
   * @return The largest sample timestamp that has been read, or {@link
   * INT64_MIN} if no samples have been read.
   */
  int64_t getLargestReadTimestampUs();

  /**
   * Returns whether the last sample of the stream has knowingly been queued. A
   * return value of
   * {@code false} means that the last sample had not been queued or that it's
   * unknown whether the last sample has been queued.
   */
  [[nodiscard]] bool isLastSampleQueued() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return lastSampleQueued;
  }

  /**
   * Returns the timestamp of the first sample, or {@link INT64_MIN} if the
   * queue is empty.
   */
  [[nodiscard]] int64_t getFirstTimestampUs() const;

  /**
   * Returns whether there is data available for reading.
   *
   * @param loadingFinished Whether no more samples will be written to the
   * sample queue. When true, this method returns true if the sample queue is
   * empty, because an empty sample queue means the end of stream has been
   * reached. When false, this method returns false if the sample queue is
   * empty.
   */
  [[nodiscard]] bool isReady(bool loadingFinished) const;

  /**
   * Attempts to read from the queue.
   */
  int readSample(FormatHolder& formatHolder,
      AVPacketBuffer* packetBuffer,
      bool formatRequired,
      bool loadingFinished);

  /**
   * Attempts to seek the read position to the specified sample index.
   *
   * @param sampleIndex The sample index.
   * @return Whether the seek was successful.
   */
  bool seekTo(int sampleIndex);

  /**
   * Attempts to seek the read position to the keyframe before or at the
   * specified time.
   *
   * @param timeUs The time to seek to.
   * @param allowTimeBeyondBuffer Whether the operation can succeed if {@code
   * timeUs} is beyond the end of the queue, by seeking to the last sample (or
   * keyframe).
   * @return Whether the seek was successful.
   */
  bool seekTo(int64_t timeUs, bool allowTimeBeyondBuffer);

  /**
   * Returns the number of samples that need to be {@link #skip(int) skipped} to
   * advance the read position to the keyframe before or at the specified time.
   *
   * @param timeUs The time to advance to.
   * @param allowEndOfQueue Whether the end of the queue is considered a
   * keyframe when {@code timeUs} is larger than the largest queued timestamp.
   * @return The number of samples that need to be skipped, which may be equal
   * to 0.
   */
  int getSkipCount(int64_t timeUs, bool allowEndOfQueue);

  /**
   * Advances the read position by the specified number of samples.
   *
   * @param count The number of samples to advance the read position by. Must be
   * at least 0 and at most {@link #getWriteIndex()} - {@link #getReadIndex()}.
   */
  void skip(int count);

  /**
   * Discards up to but not including the sample immediately before or at the
   * specified time.
   *
   * @param timeUs The time to discard up to.
   * @param toKeyframe If true then discards samples up to the keyframe before
   * or at the specified time, rather than any sample before or at that time.
   * @param stopAtReadPosition If true then samples are only discarded if
   * they're before the read position. If false then samples at and beyond the
   * read position may be discarded, in which case the read position is advanced
   * to the first remaining sample.
   */
  void discardTo(int64_t timeUs, bool toKeyframe, bool stopAtReadPosition);

  /**
   * Discards up to but not including the read position.
   */
  void discardToRead();

  /**
   * Discards all samples in the queue and advances the read position.
   */
  void discardToEnd();

  /**
   * Sets an offset that will be added to the timestamps (and sub-sample
   * timestamps) of samples that are subsequently queued.
   *
   * @param sampleOffsetUs The timestamp offset in microseconds.
   */
  void setSampleOffsetUs(int64_t newSampleOffsetUs);

  /**
   * Sets a listener to be notified of changes to the upstream format.
   *
   * @param listener The listener.
   */
  void setUpstreamFormatChangeListener(UpstreamFormatChangedListener* listener);

  /**
   * Sets the format of the samples that will follow, i.e. the upstream format. A format that is equal
   * to the one the queue holds is ignored, as is a null format.
   */
  void setFormat(const std::shared_ptr<MediaFormat>& format) override;

  /**
   * Queues the samples of a sample of an extractor.
   */
  void consume(const AVSample& sample) override;

  /**
   * Queues the bytes of a sample of the given format flags.
   *
   * <p>This is what {@link #consume(const AVSample&)} forwards to, and what a caller that holds the raw
   * bytes of a sample uses, e.g. the packet stream of the compositor.
   */
  void appendSampleData(const uint8_t* data, int size, int64_t timeUs, AVBufferFlags flags);

protected:
  /**
   * Invalidates the last upstream format adjustment. {@link
   * #getAdjustedUpstreamFormat(Format)} will be called to adjust the upstream
   * {@link Format} again before the next sample is queued.
   */
  void invalidateUpstreamFormatAdjustment() {
    upstreamFormatAdjustmentRequired = true;
  }

  /**
   * Adjusts the upstream {@link MediaFormat} (i.e., the {@link MediaFormat}
   * that was most recently passed to {@link #setFormat(MediaFormat)}).
   *
   * <p>The default implementation incorporates the sample offset passed to
   * {@link #setSampleOffsetUs(long)} into {@link
   * MediaFormat#subsampleOffsetUs}.
   *
   * @param format The {@link Format} to adjust.
   * @return The adjusted {@link Format}.
   */
  std::shared_ptr<MediaFormat>
  getAdjustedUpstreamFormat(const std::shared_ptr<MediaFormat>& format);

private:
  [[nodiscard]] std::string getName() const;

  void rewind();

  int readSampleMetadata(FormatHolder& formatHolder,
      AVPacketBuffer* packetBuffer,
      bool formatRequired,
      bool loadingFinished,
      SampleExtrasHolder& sampleExtrasHolder);

  int64_t discardSampleMetadataTo(int64_t timeUs, bool toKeyframe,
      bool stopAtReadPosition);

  int64_t discardSampleMetadataToRead();

  int64_t discardSampleMetadataToEnd();

  void commitSample(int64_t timeUs, AVBufferFlags sampleFlags, int64_t offset, int size);

  int64_t discardUpstreamSampleMetadata(int discardFromIndex);

  [[nodiscard]] bool hasNextSample() const { return readPosition != length; }

  void onFormatResult(const std::shared_ptr<MediaFormat>& newFormat, FormatHolder& formatHolder);

  /**
   * Returns whether it's possible to read the next sample.
   *
   * @param relativeReadIndex The relative read index of the next sample.
   * @return Whether it's possible to read the next sample.
   */
  [[nodiscard]] bool mayReadSample(int relativeReadIndex) const;

  /**
   * Finds the sample in the specified range that's before or at the specified
   * time. If {@code keyframe} is {@code true} then the sample is additionally
   * required to be a keyframe.
   *
   * @param relativeStartIndex The relative index from which to start searching.
   * @param length The length of the range being searched.
   * @param timeUs The specified time.
   * @param keyframe Whether only keyframes should be considered.
   * @return The offset from the start of the queue to the found sample, or
   * -1 if no matching sample was found.
   */
  int findSampleBefore(int relativeStartIndex, int length, int64_t timeUs, bool keyframe);

  /**
   * Counts the number of samples that haven't been read that have a timestamp
   * smaller than {@code timeUs}.
   *
   * @param timeUs The specified time.
   * @return The number of unread samples with a timestamp smaller than {@code
   * timeUs}.
   */
  int countUnreadSamplesBefore(int64_t timeUs);

  /**
   * Discards the specified number of samples.
   *
   * @param discardCount The number of samples to discard.
   * @return The corresponding offset up to which data should be discarded.
   */
  int64_t discardSamples(int discardCount);

  /**
   * Finds the largest timestamp of any sample from the start of the queue up to
   * the specified length, assuming that the timestamps prior to a keyframe are
   * always less than the timestamp of the keyframe itself, and of subsequent
   * frames.
   *
   * @param length The length of the range being searched.
   * @return The largest timestamp, or {@link INT64_MIN} if {@code length == 0}.
   */
  int64_t getLargestTimestamp(int length);

  /**
   * Returns the index of the sample that is {@code offset} samples after the first one in the queue.
   *
   * <p>The queue is a first in first out sequence whose first sample is the first element of the
   * metadata, so the index of a sample is its offset. It is a {@link std::deque} rather than a ring over
   * a vector because the loading thread appends to the queue while the playback thread reads it: growing
   * a vector, or moving its elements to make room at its end, frees or moves the samples the playback
   * thread is reading, which is a data race that ends in a crash. Appending to a deque neither frees nor
   * moves the elements that are already in it.
   */
  [[nodiscard]] int getRelativeIndex(int offset) const;

  MediaType mediaType;
  SampleDataQueue sampleDataQueue;
  SampleExtrasHolder mExtrasHolder;

  std::shared_ptr<MediaFormat> upstreamFormat;
  std::shared_ptr<MediaFormat> downstreamFormat;
  UpstreamFormatChangedListener* upstreamFormatChangedListener;

  /**
   * The listener of the media that is discarded from the back of the queue, or null.
   *
   * <p>Like the format listener it is not owned, so the owner has to clear it before it goes away.
   */
  UpstreamDiscardedListener* upstreamDiscardedListener = nullptr;

  struct SampleMetaInfo {
    int64_t offset;
    int size;
    int64_t timeUs;
    AVBufferFlags flags;
    std::shared_ptr<MediaFormat> format;

    SampleMetaInfo()
        : offset(0),
          size(0),
          timeUs(Definitions::kTimeUnset),
          flags(AVBufferFlags::kNone),
          format(nullptr) {}

    explicit SampleMetaInfo(int64_t offset,
        int size,
        int64_t timeUs,
        AVBufferFlags flags,
        const std::shared_ptr<MediaFormat>& format) noexcept
        : offset(offset),
          size(size),
          timeUs(timeUs),
          flags(flags),
          format(format) {}

    SampleMetaInfo(const SampleMetaInfo& other) = default;
    SampleMetaInfo& operator=(const SampleMetaInfo& other) = default;
  };

  /**
   * The samples of the queue, oldest first.
   *
   * <p>Appending to it, and removing its first or its last elements, leaves the elements that stay in it
   * where they are, so the loading thread can append to it while the playback thread reads it.
   */
  std::deque<SampleMetaInfo> metaInfoQueue;

  /** The length of the sample queue. */
  int length;
  int absoluteFirstIndex;
  int readPosition;

  int64_t startTimeUs;
  /**
   * The seek target of a discontinuity whose samples have not been read yet, or
   * {@link Definitions::kTimeUnset} when no discontinuity is pending. See
   * {@link #setPendingDiscontinuityPositionUs(int64_t)}.
   */
  int64_t pendingDiscontinuityPositionUs;
  int64_t largestQueuedTimestampUs;
  int64_t largestDiscardedTimestampUs;
  bool lastSampleQueued;
  bool upstreamKeyframeRequired;
  bool upstreamFormatRequired;
  bool upstreamFormatAdjustmentRequired;
  int64_t sampleOffsetUs;

  /**
   * Serializes the loading thread, which appends samples, with the playback thread, which reads
   * them. The metadata queue and its bookkeeping are not thread safe on their own: a concurrent
   * append can grow the deque map while the reader is looking at an element, and the queue's format
   * pointer can be replaced under the reader.
   */
  mutable std::recursive_mutex mutex_;
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_SAMPLE_QUEUE_H
