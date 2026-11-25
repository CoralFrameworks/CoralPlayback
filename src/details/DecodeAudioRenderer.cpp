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

#include "DecodeAudioRenderer.h"
#include "TraceUtil.h"

#include <coral/audio/AudioTypes.h>
#include <core/time/SystemClock.h>

#include <codec/MediaCodecFactory.h>
#include <media/Utilities.h>
#include <minilog/log.h>
#include <utils/api_level.h>

#include <optional>

using namespace utils;

namespace coral {

DecodeAudioRenderer::DecodeAudioRenderer()
  : DecodeAudioRenderer(AudioOutputSink::CreateOutput()) {
}

DecodeAudioRenderer::DecodeAudioRenderer(const std::shared_ptr<AudioOutputSink>& audioOutput) noexcept
  : BaseRenderer(MediaType::kMediaTypeAudio),
    renderTimeLimitMs(Definitions::kTimeUnset),
    audioSink(audioOutput),
    wakeupListener(nullptr),
    encoderDelay(0),
    encoderPadding(0),
    decoderReinitializationState(kReinitializationStateNone),
    decoderReceivedBuffers(false),
    audioTrackNeedsConfigure(true),
    currentPositionUs(0),
    discardUntilUs(Definitions::kTimeUnset),
    allowFirstBufferPositionDiscontinuity(false),
    allowPositionDiscontinuity(false),
    inputStreamEnded(false),
    outputStreamEnded(false) {
  flagsOnlyBuffer = AVPacketBuffer::newFlagsOnlyInstance();
  audioSink->setListener(std::make_shared<OutputSinkListener>(*this));
}

DecodeAudioRenderer::~DecodeAudioRenderer() {
  if (audioSink) {
    audioSink->release();
    audioSink->setListener(nullptr);
  }
  audioSink.reset();
}

void DecodeAudioRenderer::setWakeupListener(const std::shared_ptr<WakeupListener>& listener) {
  wakeupListener = listener;
}

SelectionCapabilities::Capabilities
DecodeAudioRenderer::supportsFormat(const std::shared_ptr<MediaFormat>& format) {
  if (!format->isAudio()) {
    return SelectionCapabilities::create(FORMAT_UNSUPPORTED_TYPE);
  }
  auto formatSupport = supportsFormatInternal(format);
  if (formatSupport <= FORMAT_UNSUPPORTED_TYPE) {
    return SelectionCapabilities::create(formatSupport);
  }
  auto tunnelingSupport = api_level() >= 21 ? TUNNELING_SUPPORTED : TUNNELING_NOT_SUPPORTED;
  return SelectionCapabilities::create(formatSupport, ADAPTIVE_NOT_SEAMLESS, tunnelingSupport);
}

SelectionCapabilities::FormatSupport
DecodeAudioRenderer::supportsFormatInternal(const std::shared_ptr<MediaFormat>& format) {
  if (!format->isAudio()) {
    return FORMAT_UNSUPPORTED_TYPE;
  } else {
    return FORMAT_HANDLED;
  }
}

bool DecodeAudioRenderer::sinkSupportsFormat(const std::shared_ptr<MediaFormat>& format) {
  return audioSink->supportsFormat(format);
}

int DecodeAudioRenderer::getSinkFormatSupport(const std::shared_ptr<MediaFormat>& format) {
  return audioSink->getFormatSupport(format);
}

void DecodeAudioRenderer::render(int64_t positionUs, int64_t elapsedRealtimeUs) {
  if (outputStreamEnded) {
    audioSink->playToEndOfStream();
    return;
  }

  // Try and read a format if we don't have one already.
  if (inputFormat == nullptr) {
    // We don't have a format yet, so try and read one.
    auto& formatHolder = getFormatHolder();
    auto result = readSource(formatHolder, flagsOnlyBuffer.get(), true);
    if (result == ReadDataResult::kResultFormatRead) {
      onInputFormatChanged(formatHolder);
    } else if (result == ReadDataResult::kResultBufferRead) {
      inputStreamEnded = true;
      processEndOfStream();
      return;
    } else {
      // We still don't have a format and can't make progress without one.
      return;
    }
  }

  // If we don't have a decoder yet, we need to instantiate one.
  maybeInitDecoder();

  if (decoder != nullptr) {
    TraceUtil::beginSection("drainAndFeed");
    auto renderStartTimeMs = SystemClock::CurrentTimeMillis();
    while (drainOutputBuffer() && shouldContinueRendering(renderStartTimeMs)) {
    }
    while (feedInputBuffer() && shouldContinueRendering(renderStartTimeMs)) {
    }
    TraceUtil::endSection();
  }
}

bool DecodeAudioRenderer::isReady() {
  // The sink consumes on the audio callback, not from render(), so both sides have to be reported:
  //
  //  * Audio to play: the queue holds frames, or the stream can supply them. When the queue runs dry
  //    the player has to know, so that it waits for data instead of playing an empty queue (the
  //    device would underrun and the frames would stutter).
  //  * Room to take more: the queue can be filled. Without this the renderer looks unready whenever
  //    the queue is full and the stream is momentarily empty, the player falls back to buffering and
  //    stops the renderers -- and a stopped renderer never feeds the queue, so the queue stays empty
  //    and the player stays stuck in buffering at an arbitrary position.
  //
  // Once the input has ended there is nothing left to feed: only the queue matters, so that it can
  // drain and the player can reach the end of the stream.
  if (inputStreamEnded) {
    return audioSink->hasPendingData();
  }
  return audioSink->hasPendingData()
         || audioSink->hasCapacityForNextBuffer()
         || (inputFormat != nullptr && (isSourceReady() || outputBuffer != nullptr));
}

bool DecodeAudioRenderer::isEnded() {
  return outputStreamEnded && audioSink->isEnded();
}

MediaClock* DecodeAudioRenderer::getMediaClock() {
  return this;
}

int64_t DecodeAudioRenderer::getPositionUs() {
  if (getStatus() == Status::kStatusRendering) {
    updateCurrentPosition();
  }
  return currentPositionUs;
}

void DecodeAudioRenderer::setPlaybackParameters(const Parameters& playbackParameters) {
  if (mParameters != playbackParameters) {
    mParameters = playbackParameters;
    audioSink->setPitch(playbackParameters.pitch);
    audioSink->setRate(playbackParameters.speed);
  }
}

void DecodeAudioRenderer::setVolume(float volume) {
  audioSink->setVolume(volume);
}

void DecodeAudioRenderer::handleMessage(int type, const Message* msg) {
  if (msg == nullptr) {
    return;
  }
  if (type == MSG_SET_VOLUME) {
    setVolume(UseMessageData<Volume>(msg->msgData).value());
  }
}

void DecodeAudioRenderer::onEnabled(bool joining, bool mayRenderStartOfStream) {
  MINILOG_DEBUG("onEnabled");
  decoderCounters = DecoderCounters();
}

void DecodeAudioRenderer::onPositionReset(int64_t positionUs, bool joining) {
  MINILOG_DEBUG("onPositionReset");
  audioSink->flush();
  currentPositionUs = positionUs;
  // Everything the decoder outputs until it reaches the new position is decode-only output (see
  // drainOutputBuffer).
  discardUntilUs = positionUs;
  allowFirstBufferPositionDiscontinuity = true;
  allowPositionDiscontinuity = true;
  inputStreamEnded = false;
  outputStreamEnded = false;
  if (decoder != nullptr) {
    flushDecoder();
  }
}

void DecodeAudioRenderer::onStarted() {
  audioSink->play();
  MINILOG_DEBUG("onStarted");
}

void DecodeAudioRenderer::onStopped() {
  updateCurrentPosition();
  audioSink->pause();
  MINILOG_DEBUG("onStopped");
}

void DecodeAudioRenderer::onDisabled() {
  inputFormat.reset();
  audioTrackNeedsConfigure = true;
  releaseDecoder();
  audioSink->release();
  MINILOG_DEBUG("onDisabled");
}

void DecodeAudioRenderer::onReset() {
  audioSink->release();
  MINILOG_DEBUG("onReset");
}

bool DecodeAudioRenderer::drainOutputBuffer() {
  // request next output buffer
  if (outputBuffer == nullptr) {
    outputBuffer = decoder->dequeueOutputBuffer();
    if (outputBuffer == nullptr) {
      return false;
    }
    outputBufferOffset = 0;
    if (outputBuffer->skippedOutputBufferCount > 0) {
      decoderCounters.skippedOutputBufferCount += outputBuffer->skippedOutputBufferCount;
      audioSink->handleDiscontinuity();
    }
    //    if (outputBuffer->isFirstSample()) {
    //      processFirstSampleOfStream();
    //    }
  }

  // handle end of stream
  if (outputBuffer->isEndOfStream()) {
    outputBuffer->release();
    outputBufferOffset = 0;
    if (decoderReinitializationState == kReinitializationStateWaitEndOfStream) {
      // We're waiting to re-initialize the decoder, and have now processed all final buffers.
      releaseDecoder();
      maybeInitDecoder();
      // The audio track may need to be recreated once the new output format is known.
      audioTrackNeedsConfigure = true;
    } else {
      outputBuffer = nullptr;
      processEndOfStream();
    }
    return false;
  }

  // configure audio sink
  if (audioTrackNeedsConfigure) {
    auto outputFormat = decoder->getOutputFormat();
    auto outputRate = outputFormat->getSampleRate();
    auto outputChannels = outputFormat->getChannelCount();
    auto outputSampleFormat = outputFormat->getSampleFormat();
    resampler.reset();
    const auto inputRate = inputFormat != nullptr ? inputFormat->getSampleRate() : MediaFormat::NO_VALUE;
    if (inputFormat != nullptr && inputFormat->getMimeType() == MimeTypes::AUDIO_MPEG
        && inputRate > 0 && inputRate != outputRate) {
      // The Media Foundation MPEG audio decoder only offers 44.1 and 32 kHz output, even for a
      // 48 kHz stream. Convert its output back to the rate of the file so that the sink and the
      // media clock use the rate of the media.
      resampler = CreateBuiltinResampleFactory()->createResample(
        AudioConfiguration(static_cast<uint64_t>(outputRate), static_cast<uint32_t>(outputChannels),
          outputSampleFormat),
        AudioConfiguration(static_cast<uint64_t>(inputRate), static_cast<uint32_t>(outputChannels),
          outputSampleFormat));
      if (resampler != nullptr) {
        outputRate = inputRate;
      }
    }
    auto format =
      AudioFormat::InitWithCommonFormat(outputSampleFormat,
        outputRate,
        outputChannels,
        true);
    audioSink->configure(format.value());
    audioSink->flush();
    audioTrackNeedsConfigure = false;
  }

  // The audio decoder of some platforms (the WMF one, for example) does not restore the timestamps
  // of the frames it outputs after it has been flushed, so the first buffer written after a position
  // reset reports 0us. The sink anchors its media time to the timestamp of that first buffer, which
  // would restart the position from 0 instead of the seek target, so the renderer position is used
  // when the buffer timestamp is not usable.
  const int64_t bufferTimeUs = outputBuffer->timeUs();
  auto baseTimeUs = bufferTimeUs;
  if (baseTimeUs == 0 && currentPositionUs > 0) {
    baseTimeUs = currentPositionUs;
  }

  // The MPEG resampler converts the whole decoder buffer, so it replaces the buffer before the
  // chunked write. The original is no longer needed once the conversion succeeded.
  if (resampler != nullptr && inputFormat != nullptr) {
    const auto outputConfig = outputBuffer->getConfiguration();
    auto converted = std::make_shared<AVSampleBuffer>(
      AudioConfiguration(static_cast<uint64_t>(inputFormat->getSampleRate()),
        static_cast<uint32_t>(outputConfig.channelCount), outputConfig.sampleFormat));
    if (resampler->resample(outputBuffer, converted)) {
      outputBuffer->release();
      outputBuffer = std::move(converted);
      outputBufferOffset = 0;
    }
  }

  const auto outputConfig = outputBuffer->getConfiguration();
  const size_t bytesPerFrame =
    MediaUtils::getBytesPerFrame(outputConfig.sampleFormat, outputConfig.channelCount);
  const uint8_t* const data = outputBuffer->array();
  const size_t totalBytes = outputBuffer->size();
  if (data == nullptr || totalBytes == 0 || bytesPerFrame == 0) {
    outputBuffer->release();
    outputBuffer = nullptr;
    outputBufferOffset = 0;
    return true;
  }

  // A decoder is allowed to return more audio in one buffer than the sink can hold -- the Darwin
  // decoder decodes every queued packet into one buffer, which for a five-second AAC track is far
  // more than the sink's ring. Writing that buffer whole is rejected, and retrying it forever keeps
  // the renderer unready while the playback position never moves. Splitting the buffer keeps the
  // sink fed and makes the renderer independent of the decoder's output granularity.
  const size_t remainingBytes = totalBytes - outputBufferOffset;
  size_t chunkBytes = std::min<size_t>(remainingBytes, kMaxFramesPerSinkWrite * bytesPerFrame);
  chunkBytes -= chunkBytes % bytesPerFrame;
  if (chunkBytes == 0) {
    chunkBytes = std::min<size_t>(remainingBytes, bytesPerFrame);
  }

  int64_t chunkTimeUs = baseTimeUs;
  if (outputConfig.sampleRate > 0) {
    const auto offsetFrames = static_cast<int64_t>(outputBufferOffset / bytesPerFrame);
    chunkTimeUs += offsetFrames * Definitions::kMicrosPerSecond /
                   static_cast<int64_t>(outputConfig.sampleRate);
  }

  // The output before the position this renderer was last reset to comes from the samples the
  // sample queue flagged with kDiscard: they were decoded only so that the decoder could reach the
  // media at the seek target, and they belong to the timeline the seek left behind. They must not
  // reach the sink, because writing them moves the media clock that the video renderer is driven by
  // away from the target again. The decoders do not report that themselves on every platform (the
  // WMF one outputs these frames with their original timestamp and no flag), so the reset position
  // is the watermark. The video renderer skips the frames it flagged the same way. The check is per
  // chunk: a buffer that starts before the watermark can still contain audio that follows it.
  if (discardUntilUs != Definitions::kTimeUnset && chunkTimeUs < discardUntilUs) {
    outputBufferOffset += chunkBytes;
    decoderCounters.skippedOutputBufferCount++;
    if (outputBufferOffset >= totalBytes) {
      outputBuffer->release();
      outputBuffer = nullptr;
      outputBufferOffset = 0;
    }
    return true;
  }

  if (!audioSink->handleBuffer(data + outputBufferOffset, chunkBytes, chunkTimeUs)) {
    // The sink has no room (or is not configured yet); the same chunk is retried on the next call.
    return false;
  }

  outputBufferOffset += chunkBytes;
  if (outputBufferOffset < totalBytes) {
    return true;
  }

  decoderCounters.renderedOutputBufferCount++;
  outputBuffer->release();
  outputBuffer = nullptr;
  outputBufferOffset = 0;
  return true;
}

bool DecodeAudioRenderer::feedInputBuffer() {
  if (decoder == nullptr
      || decoderReinitializationState == kReinitializationStateWaitEndOfStream
      || inputStreamEnded) {
    // We need to reinitialize the decoder or the input stream has ended.
    return false;
  }

  // receives next input buffer
  if (inputBuffer == nullptr) {
    inputBuffer = decoder->dequeueInputBuffer();
    if (inputBuffer == nullptr) {
      return false;
    }
  }

  if (decoderReinitializationState == kReinitializationStateSignalEndOfStream) {
    inputBuffer->setFlags(AVBufferFlags::kEndOfStream);
    decoder->queueInputBuffer(inputBuffer);
    inputBuffer = nullptr;
    decoderReinitializationState = kReinitializationStateWaitEndOfStream;
    return false;
  }

  auto formatHolder = getFormatHolder();
  switch (readSource(formatHolder, inputBuffer.get(), false)) {
    case ReadDataResult::kResultNothingRead: {
      return false;
    }
    case ReadDataResult::kResultFormatRead: {
      onInputFormatChanged(formatHolder);
      return true;
    }
    case ReadDataResult::kResultBufferRead: {
      if (inputBuffer->isEndOfStream()) {
        inputStreamEnded = true;
        //        lastBufferInStreamPresentationTimeUs = largestQueuedPresentationTimeUs;
        decoder->queueInputBuffer(inputBuffer);
        inputBuffer = nullptr;
        return false;
      }

      //      if (!firstStreamSampleRead) {
      //        firstStreamSampleRead = true;
      //        inputBuffer->addFlag(BufferFlags::BUFFER_FLAG_FIRST_SAMPLE);
      //      }
      //      largestQueuedPresentationTimeUs = inputBuffer->timeUs();
      //      if (hasReadStreamToEnd() || inputBuffer->isLastSample()) {
      //        lastBufferInStreamPresentationTimeUs = largestQueuedPresentationTimeUs;
      //      }
      //      inputBuffer->flip();
      onQueueInputBuffer(inputBuffer);
      decoder->queueInputBuffer(inputBuffer);
      inputBuffer = nullptr;
      decoderReceivedBuffers = true;
      decoderCounters.inputBufferCount++;
      return true;
    }
    default: {
      MINILOG_ERROR("Unknown read source result");
      break;
    }
  }

  return false;
}

void DecodeAudioRenderer::processEndOfStream() {
  outputStreamEnded = true;
  audioSink->playToEndOfStream();
}

void DecodeAudioRenderer::maybeInitDecoder() {
  if (decoder != nullptr || !inputFormat) {
    return;
  }

#ifndef NDEBUG
  int64_t codecInitializingTimestamp = SystemClock::CurrentTimeMillis();
#endif

  TraceUtil::beginSection("createAudioDecoder");
  createDecoder(inputFormat);
  TraceUtil::endSection();

#ifndef NDEBUG
  int64_t codecInitializedTimestamp = SystemClock::CurrentTimeMillis();
  // log the initialization time
  MINILOG_DEBUG("initializationDurationMs: {}", (codecInitializedTimestamp - codecInitializingTimestamp));
#endif
  decoderCounters.decoderInitCount++;
}

void DecodeAudioRenderer::createDecoder(const std::shared_ptr<MediaFormat>& format) {
  TraceUtil::beginSection("createAudioDecoder");
  if (format->getMaxInputSize() == MediaFormat::NO_VALUE) {
    format->setMaxInputSize(DEFAULT_INPUT_BUFFER_SIZE);
  }
  decoder = CreateBuiltinAudioCodecFactory()->createDecoder(format->getMimeType(), false);
  if (decoder != nullptr) {
    decoder->setOutputStartTimeUs(getLastResetPositionUs());
    decoder->configure(format);
    decoder->start();
  }
  TraceUtil::endSection();
}

void DecodeAudioRenderer::releaseDecoder() {
  inputBuffer.reset();
  outputBuffer.reset();
  outputBufferOffset = 0;
  resampler.reset();
  decoderReinitializationState = kReinitializationStateNone;
  decoderReceivedBuffers = false;
  if (decoder != nullptr) {
    decoderCounters.decoderReleaseCount++;
    decoder->stop();
    decoder->release();
    decoder = nullptr;
  }
}

void DecodeAudioRenderer::flushDecoder() {
  if (decoderReinitializationState != kReinitializationStateNone) {
    releaseDecoder();
    maybeInitDecoder();
  } else {
    inputBuffer.reset();
    if (outputBuffer) {
      outputBuffer->release();
    }
    outputBuffer.reset();
    outputBufferOffset = 0;
    decoder->flush();
    decoderReceivedBuffers = false;
  }
}

void DecodeAudioRenderer::onInputFormatChanged(FormatHolder& formatHolder) {
  if (formatHolder.format == nullptr) {
    // A format read that carries no format cannot be acted on: the decoder and the gapless metadata
    // below both need it. This happens when a source reports a format for a sample that was queued
    // before the source knew one; ignoring it keeps the current format, which is what it means.
    MINILOG_WARN("A format read carried no format; it is ignored");
    return;
  }
  auto oldFormat = std::move(inputFormat);
  inputFormat = formatHolder.format;
  MINILOG_INFO("input format:{}", inputFormat->toString());
  if (decoder == nullptr) {
    maybeInitDecoder();
  } else {
    if (decoderReceivedBuffers) {
      // Signal end of stream and wait for any final output buffers before re-initialization.
      decoderReinitializationState = kReinitializationStateSignalEndOfStream;
    } else {
      // There aren't any final output buffers, so release the decoder immediately.
      releaseDecoder();
      maybeInitDecoder();
      audioTrackNeedsConfigure = true;
    }
  }

  encoderDelay = inputFormat->getEncoderDelay();
  encoderPadding = inputFormat->getEncoderPadding();
}

void DecodeAudioRenderer::onQueueInputBuffer(const std::shared_ptr<AVPacketBuffer>& buffer) {
  if (allowFirstBufferPositionDiscontinuity && !buffer->isDiscard()) {
    auto timeUs = buffer->timeUs();
    if (std::abs(timeUs - currentPositionUs) > 500000) {
      currentPositionUs = timeUs;
    }
    allowFirstBufferPositionDiscontinuity = false;
  }
}

void DecodeAudioRenderer::updateCurrentPosition() {
  const auto newCurrentPositionUs = audioSink->getCurrentPositionUs(isEnded());
  if (newCurrentPositionUs != AudioOutputSink::CURRENT_POSITION_NOT_SET) {
    currentPositionUs =
      allowPositionDiscontinuity
        ? newCurrentPositionUs
        : std::max(currentPositionUs, newCurrentPositionUs);
    allowPositionDiscontinuity = false;
  }
}

bool DecodeAudioRenderer::shouldContinueRendering(const int64_t renderStartTimeMs) const {
  // Same as ExoPlayer: the loop is bounded by the configured render time limit, not by the instant
  // rendering started. Comparing the elapsed time against the start timestamp made the condition
  // true forever, so a configured limit was ignored.
  return renderTimeLimitMs == Definitions::kTimeUnset ||
         SystemClock::CurrentTimeMillis() - renderStartTimeMs < renderTimeLimitMs;
}

//--------------------------------------------------------------------------------------------------

void DecodeAudioRenderer::OutputSinkListener::onPositionDiscontinuity() {
  // We are out of sync so allow currentPositionUs to jump backwards.
  renderer.allowPositionDiscontinuity = true;
  MINILOG_DEBUG("onPositionDiscontinuity");
}

void DecodeAudioRenderer::OutputSinkListener::onOffloadBufferEmptying() {
  MINILOG_DEBUG("onOffloadBufferEmptying");
  if (renderer.wakeupListener != nullptr) {
    renderer.wakeupListener->onWakeup();
  }
}

void DecodeAudioRenderer::OutputSinkListener::onOffloadBufferFull(const int64_t bufferEmptyingDeadlineMs) {
  MINILOG_DEBUG("onOffloadBufferFull: {}", bufferEmptyingDeadlineMs);
  if (renderer.wakeupListener != nullptr) {
    renderer.wakeupListener->onSleep(bufferEmptyingDeadlineMs);
  }
}

} // namespace coral
