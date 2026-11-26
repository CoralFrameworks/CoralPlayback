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

#ifndef CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_FRAME_SINK_H
#define CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_FRAME_SINK_H

#if defined(_WIN32)

#include <coral/playback/VideoFrameSink.h>

#include <cstdint>
#include <memory>

namespace coral {

class WindowsD3D11InputNode;

/**
 * The Windows sink: a WMF decoder's D3D11 texture becomes a `WindowsD3D11InputNode` frame.
 *
 * <p>This is the Windows counterpart of `AppleVideoFrameSink`. The decoder produces
 * `WmfHardwarePixelBuffer`s whose native texture is an `ID3D11Texture2D` on the surface's device. A
 * frame the decoder already wrote in a layout the input node imports (RGBA) is submitted directly, so
 * the engine samples the decoder's own allocation. A frame in the decoder's native NV12 layout is first
 * converted to BGRA on the GPU with a `ID3D11VideoProcessor` and the result is submitted; the CPU never
 * touches the pixels either way.
 *
 * <p>The converted frames go through a small ring of shared textures. Each slot is handed to the input
 * node with a lease, so the sink does not overwrite a slot whose frame the graph may still sample; the
 * ring is sized so that the engine's two frames in flight fit inside it. The conversion is synchronous -
 * the sink waits for the video processor's completion query - which is what lets the decoder's buffer go
 * back to its pool immediately while the graph samples the converted texture.
 *
 * <p>A frame the sink cannot use (no hardware texture, P010, a video processor that refuses the
 * device) is dropped and counted; the host that needs those frames uses the CPU input instead (the
 * player's default, or {@link WindowsVideoInput::createCpuInput}).
 *
 * <p>The sink reads `VideoFrameSink::processFormat` as the decoder's native default: WMF hardware
 * decoders output NV12 textures, and asking a hardware MFT for RGBA would move decoding to the CPU.
 */
class WindowsVideoFrameSink final : public VideoFrameSink {
public:
  /**
   * @param input      the graph's D3D11 input node; the sink keeps it alive and submits frames to it.
   * @param width      the initial frame width, or 0 to size the conversion ring from the first frame.
   * @param height     the initial frame height, or 0 to size the ring from the first frame.
   */
  static std::shared_ptr<WindowsVideoFrameSink> create(
      std::shared_ptr<WindowsD3D11InputNode> input, uint32_t width = 0, uint32_t height = 0);

  ~WindowsVideoFrameSink() override;

  WindowsVideoFrameSink(const WindowsVideoFrameSink&) = delete;
  WindowsVideoFrameSink& operator=(const WindowsVideoFrameSink&) = delete;

  void setInputFormat(const std::shared_ptr<MediaFormat>& format) override;

  /** `kFormatInvalid`: the decoder keeps its native NV12 hardware output. */
  [[nodiscard]] PixelFormat processFormat() const override;

  bool submit(std::shared_ptr<AVPixelBuffer> frame, int64_t positionUs) override;

  void discard(std::shared_ptr<AVPixelBuffer> frame) override;

  void flush() override;

  /**
   * Reports the input node's last import: true when the shared texture was imported instead of staged.
   *
   * <p>It does not say the frame avoided the NV12-to-BGRA conversion; that is what
   * {@link directFrameCount} and {@link convertedFrameCount} distinguish.
   */
  [[nodiscard]] bool isZeroCopy() const override;

  /** The number of frames the sink accepted and handed to the input node. */
  [[nodiscard]] uint64_t submittedFrameCount() const noexcept;

  /** Frames submitted straight from the decoder's own RGBA texture. */
  [[nodiscard]] uint64_t directFrameCount() const noexcept;

  /** Frames the GPU converted from NV12 to BGRA before submitting. */
  [[nodiscard]] uint64_t convertedFrameCount() const noexcept;

  /** Frames the sink had to drop because it could not use their storage. */
  [[nodiscard]] uint64_t droppedFrameCount() const noexcept;

  struct State;

private:
  WindowsVideoFrameSink(std::shared_ptr<State> state, std::shared_ptr<WindowsD3D11InputNode> input);

  std::shared_ptr<State> mState;
  std::shared_ptr<WindowsD3D11InputNode> mInput;
};

} // namespace coral

#endif // _WIN32

#endif // CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_FRAME_SINK_H
