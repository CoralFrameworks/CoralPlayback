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

#include "DecodeVideoRenderer.h"
#include "TraceUtil.h"
#include "VideoEffectGraph.h"

#include <core/time/SystemClock.h>

#include <codec/MediaCodecFactory.h>
#include <codec/Utilities.h>
#include <media/Utilities.h>
#include <minilog/log.h>

namespace coral {

DecodeVideoRenderer::DecodeVideoRenderer()
  : BaseRenderer(MediaType::kMediaTypeVideo),
    mAllowedJoiningTimeMs(Definitions::kTimeUnset),
    mMaxDroppedFramesToNotify(50),
    mRenderType(RENDER_TYPE_NONE),
    mDecoderReinitializationState(kReinitializationStateNone),
    mDecoderReceivedBuffers(false),
    mRenderedFirstFrameAfterReset(false),
    mMayRenderFirstFrameAfterEnableIfNotStarted(false),
    mRenderedFirstFrameAfterEnable(false),
    initialPositionUs(Definitions::kTimeUnset),
    mJoiningDeadlineMs(Definitions::kTimeUnset),
    mCatchUpDeadlineMs(Definitions::kTimeUnset),
    mLastRenderedTimeUs(Definitions::kTimeUnset),
    mWaitingForFirstSampleInFormat(false),
    mInputStreamEnded(false),
    mOutputStreamEnded(false),
    mReportedWidth(0),
    mReportedHeight(0),
    mDroppedFrameAccumulationStartTimeMs(Definitions::kTimeUnset),
    mDroppedFrames(0),
    mConsecutiveDroppedFrameCount(0),
    mBuffersInCodecCount(0),
    mLastRenderTimeUs(0),
    mOutputStreamOffsetUs(0) {
  clearReportedVideoSize();
  flagsOnlyBuffer = AVPacketBuffer::newFlagsOnlyInstance();
  mFormatQueue = std::make_unique<TimedFormatQueue>();
}

DecodeVideoRenderer::~DecodeVideoRenderer() {
  releaseDecoder();
  mInputFormat.reset();
  mVideoEffectGraph.reset();
  if (mSurface != nullptr) {
    mSurface->release();
  }
  mSurface.reset();
  flagsOnlyBuffer.reset();
  mFormatQueue.reset();
}

void DecodeVideoRenderer::setVideoEffectGraph(std::shared_ptr<VideoEffectGraph> graph) {
  mVideoEffectGraph = std::move(graph);
  if (mVideoEffectGraph != nullptr) {
    // The CPU graph owns presentation; the decoder must output YUV frames unless a zero-copy
    // surface is installed afterwards with setEffectInputSurface.
    if (!mZeroCopyEffectInput && mSurface != nullptr) {
      mSurface->release();
      mSurface.reset();
    }
    if (!mZeroCopyEffectInput) {
      mRenderType = RenderType::RENDER_TYPE_NONE;
    }
  }
}

void DecodeVideoRenderer::setEffectInputSurface(const std::shared_ptr<VideoSurface>& surface) {
  if (mVideoEffectGraph == nullptr || surface == nullptr) {
    return;
  }
  if (mSurface != surface) {
    mSurface = surface;
  }
  mZeroCopyEffectInput = true;
  mRenderType = RenderType::RENDER_FORMAT_SURFACE;
  if (mVideoDecoder != nullptr) {
    setVideoRenderMode(mRenderType);
  }
  onOutputChanged();
}

void DecodeVideoRenderer::render(int64_t positionUs, int64_t elapsedRealtimeUs) {
  if (mOutputStreamEnded) {
    return;
  }

  if (mInputFormat == nullptr) {
    auto& formatHolder = getFormatHolder();
    flagsOnlyBuffer->clear();
    auto result = readSource(formatHolder, flagsOnlyBuffer.get(), true);
    if (result == ReadDataResult::kResultFormatRead) {
      onInputFormatChanged(formatHolder);
    } else if (result == ReadDataResult::kResultBufferRead) {
      // End of stream read having not read a format.
      mInputStreamEnded = true;
      mOutputStreamEnded = true;
      return;
    } else {
      // We still don't have a format and can't make progress without one.
      return;
    }
  }

  // If we don't have a decoder yet, we need to instantiate one.
  maybeInitDecoder();

  if (mVideoDecoder != nullptr) {
    TraceUtil::beginSection("drainAndFeed");
    while (drainOutputBuffer(positionUs, elapsedRealtimeUs)) {
    }
    while (feedInputBuffer()) {
    }
    TraceUtil::endSection();
  }
}

void DecodeVideoRenderer::handleMessage(int type, const Message* msg) {
  if (msg == nullptr) {
    return;
  }
  if (type == MSG_SET_SURFACE) {
    setOutputSurface(UseSharedMsgData<VideoSurface>(msg->msgData));
  } else if (type == MSG_SET_SCALING_MODE) {
    auto mode = UseMessageData<int>(msg->msgData);
  }
}

bool DecodeVideoRenderer::isReady() {
  if (mInputFormat != nullptr
      && (isSourceReady() || mOutputBuffer != nullptr)
      && (mRenderedFirstFrameAfterReset || !hasOutput())) {
    // Ready. If we were joining then we've now joined, so clear the joining
    // deadline.
    mJoiningDeadlineMs = Definitions::kTimeUnset;
    return true;
  } else if (mJoiningDeadlineMs == Definitions::kTimeUnset) {
    // Not joining.
    return false;
  } else if (SystemClock::CurrentTimeMillis() < mJoiningDeadlineMs) {
    return true;
  } else {
    // The joining deadline has been exceeded. Give up and clear the deadline.
    mJoiningDeadlineMs = Definitions::kTimeUnset;
    return false;
  }
}

bool DecodeVideoRenderer::isEnded() {
  return mOutputStreamEnded;
}

SelectionCapabilities::Capabilities
DecodeVideoRenderer::supportsFormat(const std::shared_ptr<MediaFormat>& format) {
  if (!format->isVideo()) {
    return SelectionCapabilities::create(FORMAT_UNSUPPORTED_TYPE);
  }
  return SelectionCapabilities::create(FORMAT_HANDLED, ADAPTIVE_SEAMLESS, TUNNELING_NOT_SUPPORTED);
}

void DecodeVideoRenderer::onEnabled(bool joining, bool mayRenderStartOfStream) {
  decoderCounters = DecoderCounters();
  mMayRenderFirstFrameAfterEnableIfNotStarted = mayRenderStartOfStream;
  mRenderedFirstFrameAfterEnable = false;
}

void DecodeVideoRenderer::onPositionReset(int64_t positionUs, bool joining) {
  mInputStreamEnded = false;
  mOutputStreamEnded = false;
  clearRenderedFirstFrame();
  initialPositionUs = Definitions::kTimeUnset;
  mConsecutiveDroppedFrameCount = 0;
  mLastRenderedTimeUs = Definitions::kTimeUnset;
  // A seek makes the following frames late until the renderer restarted from the new position, so
  // they are rendered rather than dropped.
  beginCatchUp();
  if (mVideoDecoder != nullptr) {
    flushDecoder();
  }
  if (joining) {
    setJoiningDeadlineMs();
  } else {
    mJoiningDeadlineMs = Definitions::kTimeUnset;
  }
}

void DecodeVideoRenderer::onStarted() {
  mDroppedFrames = 0;
  mDroppedFrameAccumulationStartTimeMs = SystemClock::CurrentTimeMillis();
  mLastRenderTimeUs =
    mDroppedFrameAccumulationStartTimeMs * Definitions::kMicrosPerMillisecond;
  mLastRenderedTimeUs = Definitions::kTimeUnset;
}

void DecodeVideoRenderer::onStopped() {
  mJoiningDeadlineMs = Definitions::kTimeUnset;
  mCatchUpDeadlineMs = Definitions::kTimeUnset;
  maybeNotifyDroppedFrames();
}

void DecodeVideoRenderer::onDisabled() {
  mInputFormat.reset();
  mCatchUpDeadlineMs = Definitions::kTimeUnset;
  mLastRenderedTimeUs = Definitions::kTimeUnset;
  clearReportedVideoSize();
  clearRenderedFirstFrame();
  releaseDecoder();
}

void DecodeVideoRenderer::onReset() {
  // do nothing
}

void DecodeVideoRenderer::onStreamChanged(const std::vector<std::shared_ptr<MediaFormat>>& formats,
  int64_t startPositionUs,
  int64_t offsetUs) {
  mOutputStreamOffsetUs = offsetUs;
}

void DecodeVideoRenderer::onDecoderInitialized(const std::string& name,
  int64_t initializedTimestampMs,
  int64_t initializationDurationMs) {
  //    eventDispatcher->decoderInitialized(name, initializedTimestampMs,
  //    initializationDurationMs);
}

void DecodeVideoRenderer::flushDecoder() {
  mBuffersInCodecCount = 0;
  if (mDecoderReinitializationState != kReinitializationStateNone) {
    releaseDecoder();
    maybeInitDecoder();
  } else {
    mInputBuffer.reset();
    mOutputBuffer.reset();
    mVideoDecoder->flush();
    mDecoderReceivedBuffers = false;
  }
}

void DecodeVideoRenderer::releaseDecoder() {
  mInputBuffer.reset();
  mOutputBuffer.reset();
  mDecoderReinitializationState = kReinitializationStateNone;
  mDecoderReceivedBuffers = false;
  mBuffersInCodecCount = 0;
  if (mVideoDecoder != nullptr) {
    mVideoDecoder->stop();
    mVideoDecoder->release();
  }
  mVideoDecoder.reset();
}

void DecodeVideoRenderer::onInputFormatChanged(FormatHolder& formatHolder) {
  if (formatHolder.format == nullptr) {
    // See DecodeAudioRenderer::onInputFormatChanged: a format read without a format is ignored rather
    // than dereferenced.
    MINILOG_WARN("A format read carried no format; it is ignored");
    return;
  }
  mWaitingForFirstSampleInFormat = true;
  auto oldFormat = std::move(mInputFormat);
  mInputFormat = formatHolder.format;
  if (mVideoDecoder == nullptr) {
    maybeInitDecoder();
  } else if (!canKeepCodec(oldFormat, mInputFormat)) {
    if (mDecoderReceivedBuffers) {
      // Signal end of stream and wait for any final output buffers before
      // re-initialization.
      mDecoderReinitializationState = kReinitializationStateSignalEndOfStream;
    } else {
      // There aren't any final output buffers, so release the decoder
      // immediately.
      releaseDecoder();
      maybeInitDecoder();
    }
  }
  oldFormat.reset();
}

void DecodeVideoRenderer::onQueueInputBuffer(const std::shared_ptr<AVPacketBuffer>& buffer) {
  // do nothing
}

void DecodeVideoRenderer::onProcessedOutputBuffer(int64_t presentationTimeUs) {
  mBuffersInCodecCount--;
}

bool DecodeVideoRenderer::shouldDropOutputBuffer(int64_t earlyUs) {
  // ExoPlayer drops a late buffer unless it may be the last one, which would leave the stream without
  // its final frame. A catch up window takes precedence: see isCatchingUp().
  return !isCatchingUp() && !mInputStreamEnded && isBufferLate(earlyUs);
}

bool DecodeVideoRenderer::shouldDropBuffersToKeyframe(int64_t earlyUs) {
  return !isCatchingUp() && !mInputStreamEnded && isBufferVeryLate(earlyUs);
}

bool DecodeVideoRenderer::shouldForceRenderOutputBuffer(
  int64_t earlyUs, int64_t elapsedSinceLastRenderUs) {
  // While catching up every late frame is rendered so that the picture reaches the playback position
  // again; in the steady state ExoPlayer renders a late frame at most every 100ms to avoid a frozen
  // picture.
  return isBufferLate(earlyUs)
         && (isCatchingUp() || elapsedSinceLastRenderUs > kForceRenderIntervalUs);
}

bool DecodeVideoRenderer::shouldForceRender(int64_t positionUs, int64_t earlyUs) {
  const auto shouldRenderFirstFrame =
    !mRenderedFirstFrameAfterEnable
      ? (getStatus() == Status::kStatusRendering || mMayRenderFirstFrameAfterEnableIfNotStarted)
      : !mRenderedFirstFrameAfterReset;
  const auto elapsedSinceLastRenderUs =
    SystemClock::CurrentTimeMillis() * Definitions::kMicrosPerMillisecond - mLastRenderTimeUs;
  return mJoiningDeadlineMs == Definitions::kTimeUnset
         && positionUs >= mOutputStreamOffsetUs
         && (shouldRenderFirstFrame || shouldForceRenderOutputBuffer(earlyUs, elapsedSinceLastRenderUs));
}

bool DecodeVideoRenderer::isCatchingUp() {
  if (mCatchUpDeadlineMs == Definitions::kTimeUnset) {
    return false;
  }
  if (SystemClock::CurrentTimeMillis() < mCatchUpDeadlineMs) {
    return true;
  }
  // The window expired: from now on a late frame means that rendering cannot keep up.
  mCatchUpDeadlineMs = Definitions::kTimeUnset;
  MINILOG_DEBUG("the catch up window expired, dropping late frames again");
  return false;
}

void DecodeVideoRenderer::beginCatchUp() {
  if (mCatchUpDeadlineMs != Definitions::kTimeUnset) {
    // Already catching up: the caller runs on every rendering iteration, so only the first one starts
    // (and reports) the window.
    return;
  }
  mCatchUpDeadlineMs = SystemClock::CurrentTimeMillis() + kCatchUpWindowMs;
  MINILOG_DEBUG("catching up for {}ms, late frames are rendered", kCatchUpWindowMs);
}

void DecodeVideoRenderer::skipOutputBuffer(std::shared_ptr<AVPixelBuffer> frameBuffer) {
  if (!frameBuffer) {
    return;
  }
  decoderCounters.skippedOutputBufferCount++;
  discardOutputBuffer(std::move(frameBuffer));
}

void DecodeVideoRenderer::discardOutputBuffer(std::shared_ptr<AVPixelBuffer> frameBuffer) {
  if (mVideoEffectGraph != nullptr) {
    // The graph copies the frame during submit; hand the decoder's buffer back immediately.
    if (mVideoDecoder != nullptr) {
      mVideoDecoder->releaseOutputBuffer(std::move(frameBuffer), false);
    } else if (frameBuffer != nullptr) {
      frameBuffer->release();
    }
    return;
  }

  auto bufferMode = RenderType::RENDER_TYPE_NONE;
  if (frameBuffer->getFormat() == PixelFormat::kFormatSurface) {
    bufferMode = RenderType::RENDER_FORMAT_SURFACE;
  } else {
    bufferMode = RenderType::RENDER_FORMAT_YUV;
  }
  auto renderSurface = (bufferMode == RenderType::RENDER_FORMAT_SURFACE) && (mSurface != nullptr);
  if (!renderSurface) {
    if (mVideoDecoder) {
      mVideoDecoder->releaseOutputBuffer(frameBuffer, false);
    } else {
      frameBuffer->release();
    }
  } else {
    if (mVideoDecoder) {
      mVideoDecoder->releaseOutputBuffer(frameBuffer, false);
    } else {
      frameBuffer->release();
    }
  }
}

bool DecodeVideoRenderer::maybeDropBuffersToKeyframe(int64_t positionUs) {
  int droppedSourceBufferCount = skipSource(positionUs);
  if (droppedSourceBufferCount == 0) {
    return false;
  }
  MINILOG_DEBUG("dropping {} source buffers to catch up at {}us", droppedSourceBufferCount,
    positionUs);
  decoderCounters.droppedToKeyframeCount++;
  // We dropped some buffers to catch up, so update the decoder counters and
  // flush the decoder, which releases all pending buffers buffers including the
  // current output buffer.
  updateDroppedBufferCounters(mBuffersInCodecCount + droppedSourceBufferCount);
  if (mOutputBuffer) {
    discardOutputBuffer(std::move(mOutputBuffer));
    mOutputBuffer = nullptr;
  }
  flushDecoder();
  return true;
}

void DecodeVideoRenderer::updateDroppedBufferCounters(int droppedBufferCount) {
  decoderCounters.droppedBufferCount += droppedBufferCount;
  mDroppedFrames += droppedBufferCount;
  mConsecutiveDroppedFrameCount += droppedBufferCount;
  decoderCounters.maxConsecutiveDroppedBufferCount =
    std::max(mConsecutiveDroppedFrameCount, decoderCounters.maxConsecutiveDroppedBufferCount);
  if (mMaxDroppedFramesToNotify > 0 &&
      mDroppedFrames >= mMaxDroppedFramesToNotify) {
    maybeNotifyDroppedFrames();
  }
}

void DecodeVideoRenderer::renderOutputBuffer(std::shared_ptr<AVPixelBuffer> frameBuffer,
  int64_t presentationTimeUs,
  const std::shared_ptr<MediaFormat>& mediaFormat) {
  if (mFrameMetadataListener != nullptr) {
    mFrameMetadataListener->onVideoFrameAboutToBeRendered(
      presentationTimeUs, SystemClock::CurrentTimeNanos(), mediaFormat);
  }
  mLastRenderTimeUs = SystemClock::CurrentTimeMillis() * Definitions::kMicrosPerMillisecond;

  if (mVideoEffectGraph != nullptr) {
    if (frameBuffer == nullptr) {
      updateDroppedBufferCounters(1);
      return;
    }
    if (frameBuffer->getFormat() == PixelFormat::kFormatSurface) {
      if (mZeroCopyEffectInput) {
        // The input node samples the SurfaceTexture the decoder rendered into; there is no CPU
        // frame to submit. The decoder already released a sampled buffer in that mode.
        mConsecutiveDroppedFrameCount = 0;
        decoderCounters.renderedOutputBufferCount++;
        mLastRenderedTimeUs = presentationTimeUs + mOutputStreamOffsetUs;
        mVideoEffectGraph->setPositionUs(presentationTimeUs + mOutputStreamOffsetUs);
        maybeNotifyRenderedFirstFrame();
        if (mVideoDecoder != nullptr) {
          mVideoDecoder->releaseOutputBuffer(std::move(frameBuffer), false);
        } else {
          frameBuffer->release();
        }
        return;
      }
      // The graph decodes into YUV; a surface buffer here means a legacy surface was still
      // configured. Drop it rather than handing the graph a buffer it cannot sample.
      updateDroppedBufferCounters(1);
      frameBuffer->addFlag(AVBufferFlags::kDiscard);
      if (mVideoDecoder != nullptr) {
        mVideoDecoder->releaseOutputBuffer(std::move(frameBuffer), false);
      } else {
        frameBuffer->release();
      }
      return;
    }

    VideoFrameDesc desc;
    const int displayWidth = mInputFormat != nullptr ? mInputFormat->getWidth() : 0;
    const int displayHeight = mInputFormat != nullptr ? mInputFormat->getHeight() : 0;
    if (!mFrameConverter.convert(frameBuffer, &desc, displayWidth, displayHeight)) {
      updateDroppedBufferCounters(1);
      if (mVideoDecoder != nullptr) {
        mVideoDecoder->releaseOutputBuffer(std::move(frameBuffer), false);
      } else {
        frameBuffer->release();
      }
      return;
    }

    const EffectResult result = mVideoEffectGraph->submit(desc);
    if (result != EffectResult::Success) {
      MINILOG_WARN("the effect input rejected a frame: {}", toString(result));
      updateDroppedBufferCounters(1);
    } else {
      mConsecutiveDroppedFrameCount = 0;
      decoderCounters.renderedOutputBufferCount++;
      mLastRenderedTimeUs = presentationTimeUs + mOutputStreamOffsetUs;
      mVideoEffectGraph->setPositionUs(desc.timestamp.value);
      maybeNotifyRenderedFirstFrame();
    }

    // `submit` copies the planes; the decoder's buffer can go back immediately.
    if (mVideoDecoder != nullptr) {
      mVideoDecoder->releaseOutputBuffer(std::move(frameBuffer), false);
    } else {
      frameBuffer->release();
    }
    maybeNotifyVideoSizeChanged(static_cast<int>(desc.width), static_cast<int>(desc.height));
    return;
  }

  auto bufferMode = RenderType::RENDER_TYPE_NONE;
  if (frameBuffer->getFormat() == PixelFormat::kFormatSurface) {
    bufferMode = RenderType::RENDER_FORMAT_SURFACE;
  } else {
    bufferMode = RenderType::RENDER_FORMAT_YUV;
  }
  const bool renderSurface =
    (bufferMode == RenderType::RENDER_FORMAT_SURFACE) && (mSurface != nullptr);
  if (!renderSurface) {
    // The effect graph is the only presentation path for CPU frames; without a graph the frame
    // has nowhere to go.
    updateDroppedBufferCounters(1);
    frameBuffer->addFlag(AVBufferFlags::kDiscard);
    mVideoDecoder->releaseOutputBuffer(std::move(frameBuffer), false);
    MINILOG_DEBUG("skip video frame: render surface: {}, surface is null: {}",
      renderSurface, (mSurface == nullptr));
  } else {
    maybeNotifyVideoSizeChanged(frameBuffer->width(), frameBuffer->height());
    mVideoDecoder->releaseOutputBuffer(std::move(frameBuffer), true);
    mConsecutiveDroppedFrameCount = 0;
    decoderCounters.renderedOutputBufferCount++;
    // Stored without the stream offset so that it can be compared with the playback position.
    mLastRenderedTimeUs = presentationTimeUs + mOutputStreamOffsetUs;
    maybeNotifyRenderedFirstFrame();
  }
}

void DecodeVideoRenderer::setOutputSurface(const std::shared_ptr<VideoSurface>& surface) {
  if (mVideoEffectGraph != nullptr) {
    // Presentation belongs to the graph's output node. In zero-copy mode mSurface is the decoder
    // input SurfaceTexture, not a presentation surface, so an output rebind must not release it.
    if (!mZeroCopyEffectInput && mSurface != nullptr) {
      mSurface->release();
      mSurface.reset();
    }
    if (mZeroCopyEffectInput) {
      return;
    }
    mRenderType = RenderType::RENDER_TYPE_NONE;
    return;
  }
  if (mSurface != surface) {
    mSurface = surface;
    if (mSurface != nullptr) {
      mRenderType = RenderType::RENDER_FORMAT_SURFACE;
      if (mVideoDecoder != nullptr) {
        setVideoRenderMode(mRenderType);
        releaseDecoder();
        maybeInitDecoder();
      }
      onOutputChanged();
    } else {
      // The output has been removed. We leave the outputMode of the underlying
      // decoder unchanged in anticipation that a subsequent output will likely
      // be of the same type.
      mRenderType = RenderType::RENDER_TYPE_NONE;
      onOutputRemoved();
    }
  } else if (surface != nullptr) {
    // The output is unchanged and non-null.
    onOutputReset();
  }
}

std::unique_ptr<VideoDecoder>
DecodeVideoRenderer::createDecoder(const std::shared_ptr<MediaFormat>& inputFormat) {
  if (inputFormat == nullptr || !inputFormat->isVideo()) {
    return nullptr;
  }

  // If we rendering to a surface, we should wait for the surface prepared before creating decoder.
  if ((mSurface != nullptr && !mSurface->isValid())) {
    MINILOG_DEBUG("Waiting for the Surface prepared....");
    return nullptr;
  }

  auto videoCodecType = inputFormat->getVideoCodecType();
  auto manufacturer = CodecUtils::getVideoCodecManufacturer(inputFormat->getMimeType(), false);
  auto decoder = CreateBuiltinVideoCodecFactory()->createVideoDecoder(videoCodecType, manufacturer);
  if (decoder != nullptr) {
    decoder->configure(inputFormat, mSurface);
    decoder->start();
  }

  return decoder;
}

void DecodeVideoRenderer::maybeInitDecoder() {
  if (mVideoDecoder != nullptr) {
    return;
  }

  int64_t decoderInitializingTimestamp = SystemClock::CurrentTimeMillis();
  mVideoDecoder = createDecoder(mInputFormat);
  if (mVideoDecoder != nullptr) {
    setVideoRenderMode(mRenderType);
    int64_t decoderInitializedTimestamp = SystemClock::CurrentTimeMillis();
    onDecoderInitialized(mVideoDecoder->getName(), decoderInitializedTimestamp,
      decoderInitializedTimestamp - decoderInitializingTimestamp);
    decoderCounters.decoderInitCount++;
  }
}

bool DecodeVideoRenderer::feedInputBuffer() {
  if (mVideoDecoder == nullptr ||
      mDecoderReinitializationState == kReinitializationStateWaitEndOfStream ||
      mInputStreamEnded) {
    // We need to reinitialize the decoder or the input stream has ended.
    return false;
  }

  // receives new input buffer
  if (mInputBuffer == nullptr) {
    mInputBuffer = mVideoDecoder->dequeueInputBuffer();
    if (mInputBuffer == nullptr) {
      return false;
    }
  }

  if (mDecoderReinitializationState == kReinitializationStateSignalEndOfStream) {
    mInputBuffer->clear();
    mInputBuffer->setFlags(AVBufferFlags::kEndOfStream);
    mVideoDecoder->queueInputBuffer(mInputBuffer);
    mInputBuffer = nullptr;
    mDecoderReinitializationState = kReinitializationStateWaitEndOfStream;
    return false;
  }

  auto& formatHolder = getFormatHolder();
  switch (readSource(formatHolder, mInputBuffer.get(), false)) {
    case ReadDataResult::kResultNothingRead: {
      return false;
    }
    case ReadDataResult::kResultFormatRead: {
      onInputFormatChanged(formatHolder);
      return true;
    }
    case ReadDataResult::kResultBufferRead: {
      if (mInputBuffer->isEndOfStream()) {
        mInputStreamEnded = true;
        mVideoDecoder->queueInputBuffer(std::move(mInputBuffer));
        mInputBuffer = nullptr;
        return false;
      }
      if (mWaitingForFirstSampleInFormat) {
        mFormatQueue->add(mInputBuffer->timeUs(), mInputFormat);
        mWaitingForFirstSampleInFormat = false;
      }
      onQueueInputBuffer(mInputBuffer);
      mVideoDecoder->queueInputBuffer(std::move(mInputBuffer));
      mBuffersInCodecCount++;
      mDecoderReceivedBuffers = true;
      decoderCounters.inputBufferCount++;
      mInputBuffer = nullptr;
      return true;
    }
    default: {
      MINILOG_ERROR("Unknown read source result");
    }
  }
  return false;
}

bool DecodeVideoRenderer::drainOutputBuffer(int64_t positionUs, int64_t elapsedRealtimeUs) {
  if (mOutputBuffer == nullptr) {
    mOutputBuffer = mVideoDecoder->dequeueOutputBuffer();
    if (mOutputBuffer == nullptr) {
      // The decoder has nothing to give while the playback position runs ahead of the last rendered
      // frame: the pipeline stalled rather than being too slow, so the frames that arrive once it
      // resumes are late and would all be dropped. Catch up instead.
      if (mLastRenderedTimeUs != Definitions::kTimeUnset
          && positionUs - mLastRenderedTimeUs > kStallThresholdUs) {
        beginCatchUp();
      }
      return false;
    }
    decoderCounters.skippedOutputBufferCount += mOutputBuffer->skippedOutputBufferCount;
    mBuffersInCodecCount -= mOutputBuffer->skippedOutputBufferCount;
  }

  // handle end of stream
  if (mOutputBuffer->isEndOfStream()) {
    mOutputBuffer->release();
    mOutputBuffer = nullptr;
    if (mDecoderReinitializationState == kReinitializationStateWaitEndOfStream) {
      // We're waiting to re-initialize the decoder, and have now processed all
      // final buffers.
      releaseDecoder();
      maybeInitDecoder();
    } else {
      mOutputStreamEnded = true;
    }
    return false;
  }

  auto timeUs = mOutputBuffer->timeUs();
  bool processedOutputBuffer = processOutputBuffer(positionUs, elapsedRealtimeUs);
  if (processedOutputBuffer) {
    onProcessedOutputBuffer(timeUs);
    mOutputBuffer.reset();
  }
  return processedOutputBuffer;
}

bool DecodeVideoRenderer::processOutputBuffer(int64_t positionUs, int64_t elapsedRealtimeUs) {
  if (initialPositionUs == Definitions::kTimeUnset) {
    initialPositionUs = positionUs;
  }

  // A zero-copy surface frame is already in the SurfaceTexture queue. Holding it for the renderer
  // clock starves the decoder (and the SurfaceTexture) at position 0; hand it back immediately and
  // let the GPU consumer take the next one.
  if (mVideoEffectGraph != nullptr && mZeroCopyEffectInput && mOutputBuffer != nullptr &&
      mOutputBuffer->getFormat() == PixelFormat::kFormatSurface) {
    const int64_t presentationTimeUs = mOutputBuffer->timeUs() - mOutputStreamOffsetUs;
    renderOutputBuffer(std::move(mOutputBuffer), presentationTimeUs, mOutputFormat);
    mOutputBuffer = nullptr;
    return true;
  }

  if (mOutputBuffer->isDiscard()) {
    skipOutputBuffer(std::move(mOutputBuffer));
    mOutputBuffer = nullptr;
    return true;
  }

  int64_t earlyUs = mOutputBuffer->timeUs() - positionUs;
  if (!hasOutput()) {
    // Skip frames in sync with playback, so we'll be at the right frame if the mode changes.
    if (isBufferLate(earlyUs)) {
      skipOutputBuffer(std::move(mOutputBuffer));
      mOutputBuffer = nullptr;
      return true;
    }
    MINILOG_DEBUG("hasOutput() is false, surface is null ? {}", (mSurface == nullptr));
    return false;
  }

  int64_t bufferPresentationTimeUs = mOutputBuffer->timeUs();
  int64_t presentationTimeUs = bufferPresentationTimeUs - mOutputStreamOffsetUs;
  auto format = mFormatQueue->pollFloor(presentationTimeUs);
  if (format != nullptr) {
    mOutputFormat = std::move(format);
  }

  int64_t elapsedRealtimeNowUs =
    SystemClock::CurrentTimeMillis() * Definitions::kMicrosPerMillisecond;
  int64_t elapsedSinceLastRenderUs = elapsedRealtimeNowUs - mLastRenderTimeUs;
  bool isStarted = (getStatus() == Status::kStatusRendering);

  // The renderer is in sync again, so a late frame from now on means that rendering cannot keep up.
  if (!isBufferLate(earlyUs)) {
    if (mCatchUpDeadlineMs != Definitions::kTimeUnset) {
      MINILOG_DEBUG("the renderer caught up with the playback position");
    }
    mCatchUpDeadlineMs = Definitions::kTimeUnset;
  }

  // Don't force output until we joined and the position reached the current stream.
  if (shouldForceRender(positionUs, earlyUs)) {
    renderOutputBuffer(std::move(mOutputBuffer), presentationTimeUs, mOutputFormat);
    mOutputBuffer = nullptr;
    return true;
  }

  if (!isStarted || initialPositionUs == positionUs) {
    return false;
  }

  if (shouldDropBuffersToKeyframe(earlyUs) && maybeDropBuffersToKeyframe(positionUs)) {
    return false;
  } else if (shouldDropOutputBuffer(earlyUs)) {
    updateDroppedBufferCounters(1);
    discardOutputBuffer(std::move(mOutputBuffer));
    mOutputBuffer = nullptr;
    return true;
  }

  if (earlyUs < kMaxEarlyUsToRender) {
    renderOutputBuffer(std::move(mOutputBuffer), presentationTimeUs, mOutputFormat);
    mOutputBuffer = nullptr;
    return true;
  }

  // We're either not playing, or it's not time to render the frame yet.
  return false;
}

void DecodeVideoRenderer::onOutputChanged() {
  // If we know the video size, report it again immediately.
  maybeRenotifyVideoSizeChanged();
  // We haven't rendered to the new output yet.
  clearRenderedFirstFrame();
  if (getStatus() == Status::kStatusRendering) {
    setJoiningDeadlineMs();
  }
}

void DecodeVideoRenderer::onOutputRemoved() {
  clearReportedVideoSize();
  clearRenderedFirstFrame();
}

void DecodeVideoRenderer::onOutputReset() {
  // The output is unchanged and non-null. If we know the video size and/or have
  // already rendered to the output, report these again immediately.
  maybeRenotifyVideoSizeChanged();
  maybeRenotifyRenderedFirstFrame();
}

void DecodeVideoRenderer::setJoiningDeadlineMs() {
  mJoiningDeadlineMs = mAllowedJoiningTimeMs > 0
                         ? (SystemClock::CurrentTimeMillis() + mAllowedJoiningTimeMs)
                         : Definitions::kTimeUnset;
}

void DecodeVideoRenderer::clearRenderedFirstFrame() {
  mRenderedFirstFrameAfterReset = false;
}

void DecodeVideoRenderer::maybeNotifyRenderedFirstFrame() {
  mRenderedFirstFrameAfterEnable = true;
  if (!mRenderedFirstFrameAfterReset) {
    mRenderedFirstFrameAfterReset = true;
    //        eventDispatcher->renderedFirstFrame(surface);
    MINILOG_DEBUG("render first frame");
  }
}

void DecodeVideoRenderer::maybeRenotifyRenderedFirstFrame() {
  //    if (mRenderedFirstFrameAfterReset) {
  //        eventDispatcher->renderedFirstFrame(surface);
  //    }
}

void DecodeVideoRenderer::setVideoSizeListener(const std::shared_ptr<VideoSizeListener>& listener) {
  videoSizeListener = listener;
  // The renderer reports the size only when it changes, so a new listener is told the size that is
  // already known.
  if (listener != nullptr) {
    maybeRenotifyVideoSizeChanged();
  }
}

void DecodeVideoRenderer::clearReportedVideoSize() {
  mReportedWidth = Definitions::kInvalidValue;
  mReportedHeight = Definitions::kInvalidValue;
}

void DecodeVideoRenderer::maybeNotifyVideoSizeChanged(int width, int height) {
  if (mReportedWidth != width || mReportedHeight != height) {
    mReportedWidth = width;
    mReportedHeight = height;
    if (videoSizeListener != nullptr) {
      videoSizeListener->onVideoSizeChanged(width, height);
    }
  }
}

void DecodeVideoRenderer::maybeRenotifyVideoSizeChanged() {
  if (mReportedWidth != Definitions::kInvalidValue ||
      mReportedHeight != Definitions::kInvalidValue) {
    if (videoSizeListener != nullptr) {
      videoSizeListener->onVideoSizeChanged(mReportedWidth, mReportedHeight);
    }
  }
}

void DecodeVideoRenderer::maybeNotifyDroppedFrames() {
  if (mDroppedFrames > 0) {
    int64_t now = SystemClock::CurrentTimeMillis();
    //        int64_t elapsedMs = now - mDroppedFrameAccumulationStartTimeMs;
    //        eventDispatcher->droppedFrames(droppedFrames, elapsedMs);
    mDroppedFrames = 0;
    mDroppedFrameAccumulationStartTimeMs = now;
  }
}

bool DecodeVideoRenderer::isBufferLate(int64_t earlyUs) {
  // A buffer is late if it should have been presented more than 30ms ago.
  return earlyUs < kMinEarlyUsLateThreshold;
}

bool DecodeVideoRenderer::isBufferVeryLate(int64_t earlyUs) {
  // A buffer is very late if it should have been presented more than 500ms ago.
  return earlyUs < kMinEarlyUsVeryLateThreshold;
}

void DecodeVideoRenderer::setOperatingRate(float operatingRate) {
  Renderer::setOperatingRate(operatingRate);
}

} // namespace coral
