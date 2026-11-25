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

#ifndef CORAL_PLAYBACK_VIDEO_OUTPUT_H
#define CORAL_PLAYBACK_VIDEO_OUTPUT_H

#include <cstdint>
#include <memory>

namespace coral {

class EffectOutputNode;
class VideoSurface;

/**
 * Turns a platform surface handed to LivePlayer into the output node of its effect graph.
 *
 * <p>The player itself does not know whether a surface is an Android `ANativeWindow`, a Win32
 * `HWND` or an Apple `IOSurface`. A platform bridge implements this interface, creates the matching
 * output node, and installs it on the player. That keeps the platform types out of `coral/playback`
 * and lets the same `setOutputSurface` entry point work on every platform.
 */
class VideoOutputNodeFactory {
public:
  virtual ~VideoOutputNodeFactory() = default;

  /**
   * Creates the output node for @p surface.
   *
   * @return the node, or null when the surface is not one this factory understands or is invalid.
   */
  virtual std::shared_ptr<EffectOutputNode> create(
      const std::shared_ptr<VideoSurface>& surface) = 0;

  /**
   * Reports the surface's output size, when the platform can determine it.
   *
   * <p>The layout pass renders at the engine's output size, so a late-attached surface also has to
   * resize the engine. A factory that cannot answer leaves the current size alone.
   *
   * @return true when @p width and @p height were filled.
   */
  virtual bool querySize(const std::shared_ptr<VideoSurface>& surface, uint32_t* width,
                         uint32_t* height) {
    static_cast<void>(surface);
    static_cast<void>(width);
    static_cast<void>(height);
    return false;
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_VIDEO_OUTPUT_H
