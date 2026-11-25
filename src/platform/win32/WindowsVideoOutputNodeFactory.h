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
 * <p>The factory understands a `VideoSurface` whose `getInstance()` is an `HWND`; that is what
 * `Win32WindowSurface` provides. It does not own the window.
 */
class WindowsVideoOutputNodeFactory final : public VideoOutputNodeFactory {
public:
  std::shared_ptr<EffectOutputNode> create(
      const std::shared_ptr<VideoSurface>& surface) override;

  bool querySize(const std::shared_ptr<VideoSurface>& surface, uint32_t* width,
                 uint32_t* height) override;
};

} // namespace coral

#endif // _WIN32

#endif // CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_OUTPUT_NODE_FACTORY_H
