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

#ifndef CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_OUTPUT_NODE_FACTORY_H
#define CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_OUTPUT_NODE_FACTORY_H

#if defined(_WIN32)

#include <coral/playback/VideoOutput.h>

namespace coral {

/**
 * The Windows bridge from a player surface to the effect engine's swapchain output.
 *
 * <p>This is the Windows counterpart of {@code AppleVideoOutputNodeFactory}: the application hands the
 * player a `Win32WindowSurface` over its `HWND`, and the factory turns it into the graph's
 * `WindowsSwapChainOutputNode`, which presents the composed frame into that window through the RHI.
 *
 * <p>The factory understands a `VideoSurface` whose `getInstance()` is an `HWND`; that is what
 * {@link CreateWin32WindowSurface} provides. It does not own the window - the application keeps the
 * message loop and destroys it after the player - and `querySize()` answers the client-area size, so the
 * effect graph renders at exactly the pixels the window shows.
 *
 * <p>A host that composites the video itself, rather than presenting it in a window, uses
 * {@link WindowsTextureOutputNodeFactory} over a {@link Win32TextureSurface} instead.
 */
class WindowsVideoOutputNodeFactory final : public VideoOutputNodeFactory {
public:
  /**
   * Creates the swapchain output node for a Win32 window surface.
   *
   * @return the node, or null when @p surface is not a surface over a live `HWND`.
   */
  std::shared_ptr<EffectOutputNode> create(const std::shared_ptr<VideoSurface>& surface) override;

  /** Reports the window's client area, which is the size the swapchain presents at. */
  bool querySize(const std::shared_ptr<VideoSurface>& surface, uint32_t* width,
                 uint32_t* height) override;
};

} // namespace coral

#endif // _WIN32

#endif // CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_OUTPUT_NODE_FACTORY_H
