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

#ifndef CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_INPUT_H
#define CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_INPUT_H

#if defined(_WIN32)

#include <coral/playback/VideoFrameSink.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * Creates the input side of the Windows effect graph.
 *
 * <p>This is the Windows counterpart of `AppleVideoInput` and `AndroidVideoInput`: the player does not
 * create the platform input, the application does and passes it to
 * `LivePlayer::enableVideoEffectPipeline` inside a `VideoEffectInput`, together with the sink that
 * feeds it.
 *
 * ## Zero-copy
 *
 * {@link createZeroCopyInput} builds the hardware path:
 *
 * ```
 * WMF decoder -> ID3D11Texture2D (NV12) -> WindowsVideoFrameSink
 *     -> D3D11 video processor (NV12 -> BGRA, GPU) -> shared BGRA texture
 *     -> WindowsD3D11InputNode -> Vulkan import -> effect graph
 * ```
 *
 * The decoder allocates its output textures on the surface's device, so no decoded pixel visits the
 * CPU: the sink converts on the GPU when the decoder emits NV12 (which every hardware MFT does) and
 * submits the shared texture to the graph, which imports it into its Vulkan device. A frame the decoder
 * already wrote as RGBA is submitted without the conversion step.
 *
 * The host owns the `ID3D11Device` and has to keep it alive for as long as the player and its frames
 * exist. It must be created with `D3D11_CREATE_DEVICE_VIDEO_SUPPORT` (Media Foundation's device manager
 * requires it); multi-thread protection is enabled by the aggregate, because the decoder fills textures
 * on its own threads while the render thread reads them.
 *
 * ## Fallback
 *
 * The hardware path is a request, not a promise: an old driver, a codec without a hardware MFT, or a
 * machine without a `ID3D11VideoProcessor` produces no usable frames. {@link createCpuInput} builds the
 * portable fallback - `CpuVideoFrameSink` over `VideoFrameInputNode` - which is what the player installs
 * on its own when no sink is passed. A host that wants the fallback automatically keeps the two
 * aggregates apart: the input node is part of the graph, so one player runs either the hardware path or
 * the CPU path, and choosing between them is a matter of building the player again.
 */
class WindowsVideoInput {
public:
  /**
   * Creates the zero-copy aggregate: a `WindowsD3D11InputNode`, the hardware decoder surface and the
   * `WindowsVideoFrameSink`.
   *
   * @param d3d11Device the device the decoder and the graph's shared textures live on; borrowed.
   * @param width       the initial frame width, or 0 to size the input from the first decoded frame.
   * @param height      the initial frame height, or 0 to size the input from the first decoded frame.
   * @param queueDepth  how many textures may wait for the render thread; 0 chooses the node's default.
   * @return the aggregate; a null member when the device is missing or the input node could not be
   *         created.
   */
  static VideoEffectInput createZeroCopyInput(void* d3d11Device, uint32_t width = 0,
                                              uint32_t height = 0, uint32_t queueDepth = 4);

  /**
   * Creates the CPU fallback input: decoded planes are converted and uploaded on the render thread.
   *
   * @param width      frame width in pixels.
   * @param height     frame height in pixels.
   * @param queueDepth how many frames may wait for the render thread; 0 chooses the node's default.
   */
  static VideoEffectInput createCpuInput(uint32_t width, uint32_t height,
                                         uint32_t queueDepth = 4);
};

} // namespace coral

#endif // _WIN32

#endif // CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_INPUT_H
