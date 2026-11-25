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
 * <p>VideoToolbox currently hands the Darwin decoder a bi-planar NV12 `CVPixelBuffer`, while
 * `AppleIOSurfaceInputNode` imports one BGRA texture. Until the input node grows a bi-planar path,
 * the sink performs one GPU conversion (VideoToolbox's pixel transfer session) into a small ring of
 * IOSurface-backed BGRA buffers and submits those surfaces; the decoder surface and the pixels
 * never visit the CPU.
 *
 * <p>When VideoToolbox already produced BGRA (or the input node learns NV12), the conversion ring
 * is bypassed and the decoder's own IOSurface is submitted directly. The player core does not know
 * which of the two happened.
 */
class AppleVideoFrameSink final : public VideoFrameSink {
public:
  static std::shared_ptr<AppleVideoFrameSink> create(
      std::shared_ptr<AppleIOSurfaceInputNode> input, uint32_t width, uint32_t height);

  ~AppleVideoFrameSink() override;

  AppleVideoFrameSink(const AppleVideoFrameSink&) = delete;
  AppleVideoFrameSink& operator=(const AppleVideoFrameSink&) = delete;

  void setInputFormat(const std::shared_ptr<MediaFormat>& format) override;

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

  /** The number of frames the sink has converted or submitted; used by diagnostics and tests. */
  [[nodiscard]] uint64_t submittedFrameCount() const noexcept;

  /** Whether the sink has had to drop frames because all conversion slots were busy. */
  [[nodiscard]] uint64_t droppedFrameCount() const noexcept;

  struct State;

private:
  AppleVideoFrameSink(std::shared_ptr<State> state, std::shared_ptr<AppleIOSurfaceInputNode> input);

  std::shared_ptr<State> mState;
};

} // namespace coral

#endif // CORAL_PLAYBACK_APPLE_VIDEO_FRAME_SINK_H
