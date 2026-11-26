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

#ifndef CORAL_PLAYBACK_WIN32_WINDOWS_TEXTURE_OUTPUT_NODE_FACTORY_H
#define CORAL_PLAYBACK_WIN32_WINDOWS_TEXTURE_OUTPUT_NODE_FACTORY_H

#if defined(_WIN32)

#include <coral/playback/VideoOutput.h>

namespace coral {

/**
 * Turns a {@link Win32TextureSurface} into the player's
 * {@code WindowsD3D11EncoderOutputNode}.
 *
 * <p>The node renders into shared D3D11 textures and exposes the ring through the surface, which is
 * what lets CoralUI import the current texture as an external image. The factory also records the
 * node on the surface; {@code Win32VideoFrameSource} reads it from there.
 */
class WindowsTextureOutputNodeFactory final : public VideoOutputNodeFactory {
public:
  std::shared_ptr<EffectOutputNode> create(const std::shared_ptr<VideoSurface>& surface) override;

  bool querySize(const std::shared_ptr<VideoSurface>& surface, uint32_t* width,
                 uint32_t* height) override;
};

} // namespace coral

#endif // defined(_WIN32)

#endif // CORAL_PLAYBACK_WIN32_WINDOWS_TEXTURE_OUTPUT_NODE_FACTORY_H
