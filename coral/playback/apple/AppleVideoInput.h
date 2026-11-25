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

#ifndef CORAL_PLAYBACK_APPLE_APPLE_VIDEO_INPUT_H
#define CORAL_PLAYBACK_APPLE_APPLE_VIDEO_INPUT_H

#include <effect/EffectInputNode.h>
#include <effect/EffectTypes.h>

#include <IOSurface/IOSurfaceRef.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * Creates the input side of the Apple effect graph.
 *
 * <p>Apple decoders produce frames in one of two places. A CPU decoder hands the caller I420/NV12
 * planes, which is the fallback path this class creates a `VideoFrameInputNode` for: the decoded
 * planes are copied into the node's queue and uploaded to the GPU on the render thread. A VideoToolbox
 * decoder can instead hand back a `CVPixelBuffer` backed by an `IOSurface`; that surface is sampled
 * in place by `AppleIOSurfaceInputNode` and no pixel ever reaches the CPU.
 *
 * <p>This is the Apple counterpart of `AndroidVideoInput`: the player itself does not create the
 * platform input, the application does and passes it to `enableVideoEffectPipeline` inside a
 * `VideoEffectInput` together with the sink that feeds it.
 *
 * ## Current wiring
 *
 * Every decoded frame currently reaches the graph through the CPU path: CoralMedia's Darwin decoder
 * converts its NV12 `CVPixelBuffer` into planes, the player's `CpuVideoFrameSink` converts them to
 * `VideoFrameDesc`, and `VideoFrameInputNode` uploads them. `createFrameInput` returns the node for
 * that path; the player installs the CPU sink when the aggregate omits one.
 *
 * `createIOSurfaceInput` creates the zero-copy node for a single-plane BGRA/RGBA surface. It needs a
 * platform `VideoFrameSink` to publish the decoder's `CVPixelBuffer` surface, and the decoder has to
 * emit that layout; the Apple sink and NV12/BGRA handling are the next milestone. Passing only the
 * node today would leave it without frames.
 */
class AppleVideoInput {
public:
  /**
   * Creates the CPU fallback input: decoded YUV planes are uploaded to the graph on the render thread.
   *
   * @param width      initial frame width; the node follows a size change on submit.
   * @param height     initial frame height.
   * @param queueDepth how many frames may wait for the render thread. 0 chooses the node's default.
   */
  static std::shared_ptr<EffectInputNode> createFrameInput(uint32_t width, uint32_t height,
                                                            uint32_t queueDepth = 4);

  /**
   * Creates the zero-copy input for a single-plane RGBA/BGRA IOSurface.
   *
   * @param surface    the shared surface a decoder or another producer writes; the node retains it.
   * @param width      pixel width, or 0 to ask the surface.
   * @param height     pixel height, or 0 to ask the surface.
   * @param format     the surface's colour layout.
   * @param queueDepth how many frames may wait for the render thread. 0 chooses the node's default.
   */
  static std::shared_ptr<EffectInputNode> createIOSurfaceInput(
      IOSurfaceRef surface, uint32_t width = 0, uint32_t height = 0,
      EffectPixelFormat format = EffectPixelFormat::BGRA8, uint32_t queueDepth = 4);
};

} // namespace coral

#endif // CORAL_PLAYBACK_APPLE_APPLE_VIDEO_INPUT_H
