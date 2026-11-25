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

#ifndef CORAL_PLAYBACK_APPLE_VIDEO_FRAME_SINK_H
#define CORAL_PLAYBACK_APPLE_VIDEO_FRAME_SINK_H

#include <coral/playback/VideoFrameSink.h>

#include <cstdint>
#include <memory>

namespace coral {

class AppleIOSurfaceInputNode;

/**
 * The Apple sink: a decoded `CVPixelBuffer` becomes an `AppleIOSurfaceInputNode` frame.
 *
 * <p>The sink asks the decoder for the layout its input node can import, through
 * `VideoFrameSink::processFormat` (which the renderer writes into `MediaFormat::processFormat`). On
 * Metal it requests the decoder's native NV12: the apple input imports both planes and converts them
 * in one GPU pass, so the decoder's own IOSurface is submitted directly. On OpenGL/ES it requests
 * BGRA and the decoder performs the YUV-to-RGB conversion, which is the layout the OpenGL bridge can
 * sample. The player core does not know the layout was requested.
 *
 * <p>A decoder that ignores the request and keeps NV12 on OpenGL falls back to one GPU conversion per
 * frame: `VTPixelTransferSession` writes into a small ring of IOSurface-backed BGRA buffers that the
 * sink submits instead. HDR NV12 also takes that ring until the input node's conversion handles HDR.
 * The pixels never visit the CPU either way.
 */
class AppleVideoFrameSink final : public VideoFrameSink {
public:
  static std::shared_ptr<AppleVideoFrameSink> create(
      std::shared_ptr<AppleIOSurfaceInputNode> input, uint32_t width, uint32_t height);

  ~AppleVideoFrameSink() override;

  AppleVideoFrameSink(const AppleVideoFrameSink&) = delete;
  AppleVideoFrameSink& operator=(const AppleVideoFrameSink&) = delete;

  void setInputFormat(const std::shared_ptr<MediaFormat>& format) override;

  void setEngineBackend(EffectBackend backend) override;

  /**
   * NV12 on Metal, where the input node imports the two planes; BGRA on OpenGL/ES, where the
   * decoder performs the YUV-to-RGB conversion and the input node samples one texture.
   */
  [[nodiscard]] PixelFormat processFormat() const override;

  bool submit(std::shared_ptr<AVPixelBuffer> frame, int64_t positionUs) override;

  void discard(std::shared_ptr<AVPixelBuffer> frame) override;

  void flush() override;

  /**
   * Reports the input node's import: true when the engine sampled the submitted IOSurface in place.
   *
   * <p>It does not negate the NV12-to-BGRA conversion this sink performs while CoralMedia emits the
   * platform's default layout; see the class documentation.
   */
  [[nodiscard]] bool isZeroCopy() const override;

  /** The number of frames the sink has accepted; used by diagnostics and tests. */
  [[nodiscard]] uint64_t submittedFrameCount() const noexcept;

  /** Frames submitted straight from the decoder's own BGRA IOSurface. */
  [[nodiscard]] uint64_t directFrameCount() const noexcept;

  /** Frames that needed the NV12-to-BGRA conversion fallback. */
  [[nodiscard]] uint64_t convertedFrameCount() const noexcept;

  /** Whether the sink has had to drop frames because all conversion slots were busy. */
  [[nodiscard]] uint64_t droppedFrameCount() const noexcept;

  struct State;

private:
  AppleVideoFrameSink(std::shared_ptr<State> state, std::shared_ptr<AppleIOSurfaceInputNode> input);

  std::shared_ptr<State> mState;
};

} // namespace coral

#endif // CORAL_PLAYBACK_APPLE_VIDEO_FRAME_SINK_H
