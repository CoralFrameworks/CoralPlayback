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

#ifndef CORAL_PLAYBACK_APPLE_APPLE_VIDEO_OUTPUT_NODE_FACTORY_H
#define CORAL_PLAYBACK_APPLE_APPLE_VIDEO_OUTPUT_NODE_FACTORY_H

#include <coral/playback/VideoOutput.h>

namespace coral {

class AppleIOSurface;

/**
 * The Apple bridge from a player surface to the effect engine's IOSurface output.
 *
 * <p>It accepts the `AppleIOSurface` surface and returns the `AppleIOSurfaceOutputNode` that copies the
 * graph's final frame into it on the GPU. That node supports Metal (a writeback `id<MTLTexture>`) and
 * OpenGL (a `GL_TEXTURE_2D` the bridge pushes into the surface); a surface whose format is not a
 * single-plane RGBA layout is refused, as is anything that is not an `AppleIOSurface`.
 *
 * <p>The factory creates a new node for every surface, so a host that replaces or resizes its output
 * surface only has to hand the new `AppleIOSurface` to `LivePlayer::setOutputSurface`; the player
 * reconnects the graph and the engine prepares the new output.
 *
 * <p>This is the Apple counterpart of `AndroidVideoOutputNodeFactory` and the Win32
 * `WindowsVideoOutputNodeFactory`.
 */
class AppleVideoOutputNodeFactory final : public VideoOutputNodeFactory {
public:
  std::shared_ptr<EffectOutputNode> create(
      const std::shared_ptr<VideoSurface>& surface) override;

  /**
   * Reports the surface's pixel size, so `setOutputSurface` can also resize the effect engine.
   *
   * @return true when @p surface is a valid `AppleIOSurface`.
   */
  bool querySize(const std::shared_ptr<VideoSurface>& surface, uint32_t* width,
                 uint32_t* height) override;
};

} // namespace coral

#endif // CORAL_PLAYBACK_APPLE_APPLE_VIDEO_OUTPUT_NODE_FACTORY_H
