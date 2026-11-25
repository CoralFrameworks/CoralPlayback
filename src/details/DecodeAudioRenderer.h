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

#ifndef CORAL_PLAYBACK_DETAILS_DECODE_AUDIO_RENDERER_H
#define CORAL_PLAYBACK_DETAILS_DECODE_AUDIO_RENDERER_H

#include "BaseRenderer.h"
#include <coral/audio/Resample.h>

#include "DecoderCounters.h"

#include <coral/playback/AudioRenderStats.h>

#include <coral/audio/AudioOutputSink.h>
#include <coral/source/FormatHolder.h>

#include <codec/MediaCodec.h>
#include <media/PacketBuffer.h>

#include <memory>

namespace coral {

/**
 * The implementation of the Renderer for decoding and rendering audio
 */
class DecodeAudioRenderer final : public BaseRenderer, public MediaClock {
public:
  DecodeAudioRenderer();

  explicit DecodeAudioRenderer(const std::shared_ptr<AudioOutputSink>& audioOutput) noexcept;

  ~DecodeAudioRenderer() override;

  void setWakeupListener(const std::shared_ptr<WakeupListener>& wakeupListener) override;

  void render(int64_t positionUs, int64_t elapsedRealtimeUs) override;

  void handleMessage(int type, const Message* msg) override;

  bool isReady() override;

  bool isEnded() override;

  MediaClock* getMediaClock() override;

  int64_t getPositionUs() override;

  void setPlaybackParameters(const Parameters& playbackParameters) override;

  const Parameters& getPlaybackParameters() const override {
    return mParameters;
  }

  void setVolume(float volume);

  /** Returns a snapshot of this renderer's decoder and sink counters. */
  [[nodiscard]] AudioRendererStats getRenderStats() const;

  const char* getName() const override {
    return "DecodeAudioRenderer";
  }

  Capabilities supportsFormat(const std::shared_ptr<MediaFormat>& format) override;

  AdaptiveSupport supportsMixedMimeTypeAdaptation() override {
    return ADAPTIVE_NOT_SEAMLESS;
  }

protected:
  static FormatSupport
  supportsFormatInternal(const std::shared_ptr<MediaFormat>& format);

  bool sinkSupportsFormat(const std::shared_ptr<MediaFormat>& format);

  int getSinkFormatSupport(const std::shared_ptr<MediaFormat>& format);

  void onPositionReset(int64_t positionUs, bool joining) override;

  void onStarted() override;

  void onStopped() override;

  void onDisabled() override;

  void onReset() override;

  void onEnabled(bool joining, bool mayRenderStartOfStream) override;

private:
  bool drainOutputBuffer();

  bool feedInputBuffer();

  void processEndOfStream();

  void maybeInitDecoder();

  void createDecoder(const std::shared_ptr<MediaFormat>& format);

  void releaseDecoder();

  void flushDecoder();

  void onInputFormatChanged(FormatHolder& formatHolder);

  void onQueueInputBuffer(const std::shared_ptr<AVPacketBuffer>& buffer);

  void updateCurrentPosition();

  bool shouldContinueRendering(int64_t renderStartTimeMs) const;

  /**
   * The decoder does not need to be re-initialized.
   */
  static constexpr int kReinitializationStateNone = 0;

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

  /** The default input buffer size. */
  static constexpr int DEFAULT_INPUT_BUFFER_SIZE = 960 * 6;

  int64_t renderTimeLimitMs;
  Parameters mParameters;
  std::shared_ptr<AudioOutputSink> audioSink;
  std::shared_ptr<WakeupListener> wakeupListener;

  DecoderCounters decoderCounters{};
  uint64_t sinkBackpressureCount = 0;
  std::shared_ptr<MediaFormat> inputFormat;
  int encoderDelay;
  int encoderPadding;
  std::unique_ptr<AudioDecoder> decoder;

  /**
   * Converts the output of a decoder which does not offer the sample rate of the media, e.g. the
   * Windows MP3 decoder, back to the rate of the original file.
   */
  std::unique_ptr<Resample> resampler;

  std::shared_ptr<AVPacketBuffer> flagsOnlyBuffer;
  std::shared_ptr<AVPacketBuffer> inputBuffer;
  std::shared_ptr<AVSampleBuffer> outputBuffer;
  /** Bytes of outputBuffer already written to the sink. */
  size_t outputBufferOffset = 0;
  /** The largest number of frames written to the sink in one call. */
  static constexpr size_t kMaxFramesPerSinkWrite = 1024;

  int decoderReinitializationState;
  bool decoderReceivedBuffers;
  bool audioTrackNeedsConfigure;

  int64_t currentPositionUs;
  /**
   * The position the renderer was last reset to, which is also the seek target: output with an
   * earlier timestamp comes from the samples the sample queue decoded only to reach that target and
   * must not be written to the sink. {@link Definitions::kTimeUnset} when there is no such output.
   */
  int64_t discardUntilUs;
  bool allowFirstBufferPositionDiscontinuity;
  bool allowPositionDiscontinuity;
  bool inputStreamEnded;
  bool outputStreamEnded;

  class OutputSinkListener final : public AudioOutputSink::Listener {
  private:
    DecodeAudioRenderer& renderer;

  public:
    explicit OutputSinkListener(DecodeAudioRenderer& renderer) noexcept
      : renderer(renderer) {
    }

    void onPositionDiscontinuity() override;

    void onOffloadBufferEmptying() override;

    void onOffloadBufferFull(int64_t bufferEmptyingDeadlineMs) override;
  };
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_DECODE_AUDIO_RENDERER_H
