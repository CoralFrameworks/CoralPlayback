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

#ifndef CORAL_PLAYBACK_WIN32_WINDOW_SURFACE_H
#define CORAL_PLAYBACK_WIN32_WINDOW_SURFACE_H

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <codec/MediaCodec.h>

#include <memory>

namespace coral {

/**
 * The Win32 window a player presents its composed video into.
 *
 * <p>This is the Windows counterpart of the Android `JniPreviewSurface`: a thin `VideoSurface`
 * over an `HWND`. The window is owned by the application and must outlive the surface. It is the
 * handle a {@link VideoOutputNodeFactory} turns into a `WindowsSwapChainOutputNode`.
 */
class Win32WindowSurface final : public VideoSurface {
public:
  explicit Win32WindowSurface(HWND window) noexcept;

  ~Win32WindowSurface() override = default;

  [[nodiscard]] void* getInstance() const override;

  [[nodiscard]] bool isValid() const override;

  /**
   * Does nothing: the window belongs to the application.
   *
   * Releasing it here would destroy a window the application still owns, so the surface only drops
   * its non-owning handle when it is destroyed.
   */
  void release() override;

private:
  HWND mWindow = nullptr;
};

/** Creates a non-owning surface for @p window, or null when the window is not valid. */
[[nodiscard]] std::shared_ptr<VideoSurface> CreateWin32WindowSurface(HWND window);

} // namespace coral

#endif // _WIN32

#endif // CORAL_PLAYBACK_WIN32_WINDOW_SURFACE_H
