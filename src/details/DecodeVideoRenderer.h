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

#ifndef CORAL_PLAYBACK_DETAILS_DECODE_VIDEO_RENDERER_H
#define CORAL_PLAYBACK_DETAILS_DECODE_VIDEO_RENDERER_H

#include "BaseRenderer.h"
#include "DecoderCounters.h"

#include <coral/playback/TimedFormatQueue.h>
#include <coral/source/FormatHolder.h>
#include <codec/VideoFrameMetadataListener.h>
#include <codec/MediaCodec.h>
#include <media/PacketBuffer.h>

namespace coral {

class VideoEffectGraph;
class VideoFrameSink;

/**
 * The implementation of the BaseRenderer for video track
 */
class DecodeVideoRenderer final : public BaseRenderer {
public:
  DecodeVideoRenderer();

  ~DecodeVideoRenderer() override;

  /**
   * Switches the renderer to the effect graph.
   *
   * <p>When a graph is set, presentation belongs to the graph's output node. Decoded frames are
   * handed to the sink installed by {@link setVideoFrameSink}; the legacy renderer surface is not
   * used. Passing null restores the legacy path.
   */
  void setVideoEffectGraph(std::shared_ptr<VideoEffectGraph> graph);

  /**
   * Installs the platform frame sink and the decoder surface that feeds it.
   *
   * <p>The sink is the only part of the effect path that knows how a decoded `AVPixelBuffer`
   * becomes an effect input. The renderer never inspects the frame's platform type. The decoder
   * surface is null for a CPU sink and is the platform `VideoSurface` (SurfaceTexture, D3D11,
   * IOSurface) for a hardware sink.
   *
   * <p>Must be called before the decoder is created; a later change reinitializes an existing
   * decoder.
   */
  void setVideoFrameSink(std::shared_ptr<VideoFrameSink> sink,
                         std::shared_ptr<VideoSurface> decoderSurface);

  [[nodiscard]] bool hasVideoEffectGraph() const noexcept { return mVideoEffectGraph != nullptr; }

  void setOutputSurface(const std::shared_ptr<VideoSurface>& surface);

  void setOperatingRate(float operatingRate) override;

  /**
   * Stores the listener the player reads the size of the decoded video from.
   *
   * @param listener The listener, or null to stop reporting the size.
   */
  void setVideoSizeListener(const std::shared_ptr<VideoSizeListener>& listener) override;

  void setVideoColorInfoListener(
      const std::shared_ptr<VideoColorInfoListener>& listener) override;

  void render(int64_t positionUs, int64_t elapsedRealtimeUs) override;

  bool isReady() override;

  bool isEnded() override;

  void handleMessage(int type, const Message* msg) override;

  Capabilities supportsFormat(const std::shared_ptr<MediaFormat>& format) override;

  const char* getName() const override {
    return "DecodeVideoRenderer";
  }

protected:
  void onEnabled(bool joining, bool mayRenderStartOfStream) override;

  void onPositionReset(int64_t positionUs, bool joining) override;

  void onStarted() override;

  void onStopped() override;

  void onDisabled() override;

  void onReset() override;

  void onStreamChanged(const std::vector<std::shared_ptr<MediaFormat>>& formats,
    int64_t startPositionUs,
    int64_t offsetUs) override;

private:
  void onDecoderInitialized(const std::string& name,
    int64_t initializedTimestampMs,
    int64_t initializationDurationMs);

  void flushDecoder();

  void releaseDecoder();

  void onInputFormatChanged(FormatHolder& formatHolder);

  void onQueueInputBuffer(const std::shared_ptr<AVPacketBuffer>& buffer);

  void onProcessedOutputBuffer(int64_t presentationTimeUs);

  /**
   * Returns whether the buffer being processed should be dropped.
   *
   * Mirrors ExoPlayer's {@code MediaCodecVideoRenderer.shouldDropOutputBuffer}: a buffer is dropped
   * when it is late and the renderer is not catching up, and never when it may be the last buffer of
   * the stream.
   *
   * @param earlyUs The time until the buffer should be presented in
   * microseconds. A negative value indicates that the buffer is late.
   */
  bool shouldDropOutputBuffer(int64_t earlyUs);

  /**
   * Returns whether to drop all buffers from the buffer being processed to the
   * keyframe at or after the current playback position, if possible.
   *
   * @param earlyUs The time until the current buffer should be presented in
   * microseconds. A negative value indicates that the buffer is late.
   */
  bool shouldDropBuffersToKeyframe(int64_t earlyUs);

  /**
   * Returns whether to force rendering an output buffer.
   *
   * @param earlyUs The time until the current buffer should be presented in
   * microseconds. A negative value indicates that the buffer is late.
   * @param elapsedSinceLastRenderUs The elapsed time since the last output
   * buffer was rendered, in microseconds.
   * @return Returns whether to force rendering an output buffer.
   */
  bool shouldForceRenderOutputBuffer(int64_t earlyUs, int64_t elapsedSinceLastRenderUs);

  /**
   * Returns whether the output buffer should be rendered without waiting, either because the first
   * frame has to be shown or because the renderer is catching up after the pipeline stalled.
   */
  bool shouldForceRender(int64_t positionUs, int64_t earlyUs);

  /**
   * Returns whether the renderer is catching up.
   *
   * Frames are late in two very different situations: the device cannot render fast enough (the
   * frames should be dropped), or the pipeline stalled and its data arrived after the playback clock
   * moved on (the frames should be rendered, otherwise everything after the stall is dropped, which
   * used to lose most of a short clip when its first frames arrived late). The renderer catches up
   * for a bounded time instead of dropping in the latter case.
   */
  [[nodiscard]] bool isCatchingUp();

  /**
   * Starts a bounded catch up window, in which late frames are rendered rather than dropped.
   */
  void beginCatchUp();

  /**
   * Skips the specified output buffer and releases it.
   *
   * @param frameBuffer The output buffer to skip.
   */
  void skipOutputBuffer(std::shared_ptr<AVPixelBuffer> frameBuffer);

  void discardOutputBuffer(std::shared_ptr<AVPixelBuffer> frameBuffer);

  /**
   * Drops frames from the current output buffer to the next keyframe at or
   * before the playback position. If no such keyframe exists, as the playback
   * position is inside the same group of pictures as the buffer being
   * processed, returns {@code false}. Returns {@code true} otherwise.
   *
   * @param positionUs The current playback position.
   * @return Whether any buffers were dropped.
   */
  bool maybeDropBuffersToKeyframe(int64_t positionUs);

  /**
   * Updates decoder counters to reflect that {@code droppedBufferCount}
   * additional buffers were dropped.
   *
   * @param droppedBufferCount The number of additional dropped buffers.
   */
  void updateDroppedBufferCounters(int droppedBufferCount);

  /**
   * Renders the specified output buffer.
   *
   * <p>The implementation of this method takes ownership of the output buffer
   * and is responsible for calling {@link VideoDecoderOutputBuffer#release()}
   * either immediately or in the future.
   *
   * @param frameBuffer {@link VideoDecoderOutputBuffer} to render.
   * @param presentationTimeUs Presentation time in microseconds.
   */
  void renderOutputBuffer(std::shared_ptr<AVPixelBuffer> frameBuffer,
    int64_t presentationTimeUs,
    const std::shared_ptr<MediaFormat>& mediaFormat);

  /**
   * Creates a new video decoder for renderer
   *
   * @param inputFormat the input format
   * @return  the video decoder pointer or nullptr if not available
   */
  std::unique_ptr<VideoDecoder>
  createDecoder(const std::shared_ptr<MediaFormat>& inputFormat);

  /**
   * The render type of the video frame
   */
  enum RenderType : int8_t {
    //!< Video frame format is not set.
    RENDER_TYPE_NONE = -1,
    //!< Video frame format that renders raw 4:2:0 YUV planes.
    RENDER_FORMAT_YUV = 0,
    //!< Video frame format that renders 4:2:0 YUV planes directly to a surface.
    RENDER_FORMAT_SURFACE = 1
  };

  /**
   * Sets the video render mode
   *
   * @param renderMode the specified render mode
   */
  virtual void setVideoRenderMode(const RenderType& renderMode) {
  }

  /**
   * Returns whether the existing decoder can be kept for a new format.
   *
   * @param oldFormat The previous format.
   * @param newFormat The new format.
   * @return Whether the existing decoder can be kept.
   */
  virtual bool canKeepCodec(const std::shared_ptr<MediaFormat>& oldFormat,
    const std::shared_ptr<MediaFormat>& newFormat) {
    return false;
  }

  void maybeInitDecoder();

  bool feedInputBuffer();

  /**
   * Attempts to dequeue an output buffer from the decoder and, if successful,
   * passes it to {@link
   * ::processOutputBuffer(long, long)}.
   *
   * @param positionUs        The player's current position.
   * @param elapsedRealtimeUs System time in microseconds, measured at the start
   * of the current iteration of the rendering loop.
   * @return                  Whether it may be possible to drain more output
   * data.
   */
  bool drainOutputBuffer(int64_t positionUs, int64_t elapsedRealtimeUs);

  /**
   * Processes {@link #outputBuffer} by rendering it, skipping it or doing
   * nothing, and returns whether it may be possible to process another output
   * buffer.
   *
   * @param positionUs        The player's current position.
   * @param elapsedRealtimeUs System time in microseconds, measured at the start
   * of the current iteration of the rendering loop.
   * @return                  Whether it may be possible to drain another output
   * buffer.
   */
  bool processOutputBuffer(int64_t positionUs, int64_t elapsedRealtimeUs);

  bool hasOutput() const { return mVideoEffectGraph != nullptr || mRenderType != RenderType::RENDER_TYPE_NONE; }

  void onOutputChanged();

  void onOutputRemoved();

  void onOutputReset();

  void setJoiningDeadlineMs();

  void clearRenderedFirstFrame();

  void maybeNotifyRenderedFirstFrame();

  void maybeRenotifyRenderedFirstFrame();

  void clearReportedVideoSize();

  void maybeNotifyVideoSizeChanged(int width, int height);

  void maybeRenotifyVideoSizeChanged();

  void maybeNotifyDroppedFrames();

  static bool isBufferLate(int64_t earlyUs);

  static bool isBufferVeryLate(int64_t earlyUs);

  /**
   * The decoder does not need to be re-initialized.
   */
  static constexpr int kReinitializationStateNone = 0;

  /** A frame is late when it should have been presented more than 30ms ago (ExoPlayer). */
  static constexpr int64_t kMinEarlyUsLateThreshold = -30000;

  /** A frame is very late when it should have been presented more than 500ms ago (ExoPlayer). */
  static constexpr int64_t kMinEarlyUsVeryLateThreshold = -500000;

  /** A late frame is rendered at least this often in the steady state, to avoid a frozen picture. */
  static constexpr int64_t kForceRenderIntervalUs = 100000;

  /** A buffer at most this early can be rendered without waiting. */
  static constexpr int64_t kMaxEarlyUsToRender = 30000;

  /** How long the renderer renders late frames after the pipeline stalled. */
  static constexpr int64_t kCatchUpWindowMs = 1000;

  /** How far the playback position may run ahead of the last rendered frame before the pipeline is
   * considered stalled. */
  static constexpr int64_t kStallThresholdUs = 300000;

  /**
   * The input format has changed in a way that requires the decoder to be
   * re-initialized, but we haven't yet signaled an end of stream to the
   * existing decoder. We need to do so in order to ensure that it outputs any
   * remaining buffers before we release it.
   */
  static constexpr int kReinitializationStateSignalEndOfStream = 1;

  /**
   * The input format has changed in a way that requires the decoder to be
   * re-initialized, and we've signaled an end of stream to the existing
   * decoder. We're waiting for the decoder to output an end of stream signal to
   * indicate that it has output any remaining buffers before we release it.
   */
  static constexpr int kReinitializationStateWaitEndOfStream = 2;

  int64_t mAllowedJoiningTimeMs;
  int mMaxDroppedFramesToNotify;

  std::shared_ptr<AVPacketBuffer> flagsOnlyBuffer;
  std::shared_ptr<MediaFormat> mInputFormat;
  std::shared_ptr<MediaFormat> mOutputFormat;
  //!< video decoder
  std::unique_ptr<VideoDecoder> mVideoDecoder;
  //!< input and output buffer
  std::shared_ptr<AVPacketBuffer> mInputBuffer;
  std::shared_ptr<AVPixelBuffer> mOutputBuffer;
  std::unique_ptr<TimedFormatQueue> mFormatQueue;

  //!< legacy presentation surface, used only when no effect graph is installed
  std::shared_ptr<VideoSurface> mSurface;
  //!< the effect graph when the player is in effect mode
  std::shared_ptr<VideoEffectGraph> mVideoEffectGraph;
  //!< the platform frame adapter for the effect graph, or null
  std::shared_ptr<VideoFrameSink> mFrameSink;
  //!< the decoder surface the sink's input samples, or null for CPU frames
  std::shared_ptr<VideoSurface> mEffectDecoderSurface;
  std::unique_ptr<VideoFrameMetadataListener> mFrameMetadataListener;
  RenderType mRenderType;

  int mDecoderReinitializationState;
  bool mDecoderReceivedBuffers;

  bool mRenderedFirstFrameAfterReset;
  bool mMayRenderFirstFrameAfterEnableIfNotStarted;
  bool mRenderedFirstFrameAfterEnable;
  int64_t initialPositionUs;
  int64_t mJoiningDeadlineMs;

  /** Deadline of the current catch up window, or {@link Definitions::kTimeUnset} when not catching up. */
  int64_t mCatchUpDeadlineMs;
  /** Presentation time of the last rendered frame, in microseconds. */
  int64_t mLastRenderedTimeUs;
  bool mWaitingForFirstSampleInFormat;

  bool mInputStreamEnded;
  bool mOutputStreamEnded;
  int mReportedWidth;
  int mReportedHeight;

  /** The listener the size of the decoded frames is reported to, or null. */
  std::shared_ptr<VideoSizeListener> videoSizeListener;

  /** The listener the colour metadata is reported to, or null. */
  std::shared_ptr<VideoColorInfoListener> videoColorInfoListener;

  int64_t mDroppedFrameAccumulationStartTimeMs;
  int mDroppedFrames;
  int mConsecutiveDroppedFrameCount;
  int mBuffersInCodecCount;
  int64_t mLastRenderTimeUs;
  int64_t mOutputStreamOffsetUs;

  DecoderCounters decoderCounters;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_DECODE_VIDEO_RENDERER_H
