/*
 * Copyright (C) 2026 The Coral Project
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

#ifndef CORAL_PLAYBACK_VIDEO_FRAME_SINK_H
#define CORAL_PLAYBACK_VIDEO_FRAME_SINK_H

#include <cstdint>
#include <memory>

namespace coral {

class AVPixelBuffer;
class EffectInputNode;
class MediaFormat;
class VideoSurface;

/**
 * The platform seam that turns a decoded frame into input for the effect graph.
 *
 * <p>This is the modern counterpart of the old `codec::VideoFrameRenderer::setOutputBuffer`:
 * the player's video renderer hands the sink the `AVPixelBuffer` it decided to present, and the
 * sink - which lives in `src/platform/<platform>` - does whatever that platform needs. A CPU sink
 * converts the planes and uploads them; a Windows sink converts to a D3D11 texture and publishes a
 * `PixelBufferView`; an Apple sink publishes the `CVPixelBuffer`'s `IOSurface`; an Android
 * SurfaceTexture sink only releases the decoder buffer and lets its input node pull the frame.
 *
 * <p>The player core must not inspect the frame's platform type. Keeping that rule is what lets a
 * new platform - Linux DMA-BUF, HarmonyOS native buffer - be added by writing one sink and one
 * `EffectInputNode` instead of touching `DecodeVideoRenderer`.
 *
 * ## Ownership
 *
 * `submit` and `discard` take ownership of the frame. The sink releases it, hands it to a platform
 * input, or keeps it alive until the GPU has sampled it; the renderer never releases a frame it gave
 * to the sink. A sink that fails to consume a frame must still release it before returning.
 */
class VideoFrameSink {
public:
  virtual ~VideoFrameSink() = default;

  /**
   * Whether the input node samples the decoder's surface itself.
   *
   * <p>A pull-model sink (Android's SurfaceTexture) has no per-frame payload to publish: the decoder
   * writes into the surface and the input node takes the frame on the render thread. The renderer
   * may submit such a frame before its presentation time, because holding it would starve the
   * decoder; it still calls `submit` exactly once per output buffer so the sink can release it.
   */
  [[nodiscard]] virtual bool isPullModel() const {
    return false;
  }

  /**
   * Tells the sink the decoder's input format.
   *
   * <p>A CPU sink uses the display size to crop a decoder's macroblock padding; a platform sink may
   * use the colour description or ignore it. Called again when the stream changes.
   */
  virtual void setInputFormat(const std::shared_ptr<MediaFormat>& format) {
    static_cast<void>(format);
  }

  /**
   * Publishes one decoded frame at @p positionUs (media time, microseconds).
   *
   * @return true when the frame was accepted; false when it was dropped. The sink owns the frame in
   *         both cases.
   */
  virtual bool submit(std::shared_ptr<AVPixelBuffer> frame, int64_t positionUs) = 0;

  /** Releases a frame the renderer decided not to present. */
  virtual void discard(std::shared_ptr<AVPixelBuffer> frame) = 0;

  /** Drops queued platform frames and handles, for example after a seek. */
  virtual void flush() = 0;

  /** Whether the last submitted frame used the platform's zero-copy path. */
  [[nodiscard]] virtual bool isZeroCopy() const {
    return false;
  }
};

/**
 * Everything a player needs to feed the effect graph, passed as one object.
 *
 * <p>The platform bridge creates this struct and hands it to
 * `LivePlayer::enableVideoEffectPipeline`. The player uses `inputNode` to build the graph,
 * `decoderSurface` to configure the decoder, and `frameSink` to hand frames over. The fields are
 * deliberately grouped: a platform that adds an input node must also provide the sink that feeds it.
 *
 * <p>An application that only wants the CPU path can leave `frameSink` null when `inputNode` is a
 * `VideoFrameInputNode`; the player then installs its CPU sink. A null `inputNode` creates the
 * player's default CPU input.
 */
struct VideoEffectInput {
  /** The graph's input node. */
  std::shared_ptr<EffectInputNode> inputNode;

  /**
   * The surface the decoder writes into, or null for CPU frames.
   *
   * <p>Android passes a `VideoSurface` over the decoder's Java `Surface`; a hardware-output platform
   * passes its `HardwareVideoSurface`; a CPU sink leaves it null.
   */
  std::shared_ptr<VideoSurface> decoderSurface;

  /** The platform frame adapter that feeds `inputNode`. */
  std::shared_ptr<VideoFrameSink> frameSink;

  /**
   * Optional platform graphics context for the effect engine, such as an Android `EGLContext` or an
   * Apple `id<MTLDevice>`. The platform bridge owns it and keeps it alive.
   */
  void* sharedContext = nullptr;
};

} // namespace coral

#endif // CORAL_PLAYBACK_VIDEO_FRAME_SINK_H
