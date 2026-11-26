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

#ifndef CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_DEVICE_H
#define CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_DEVICE_H

#if defined(_WIN32)

#include <memory>

namespace coral {

/**
 * A Direct3D 11 device the Windows zero-copy path can decode and composite on.
 *
 * <p>The WMF decoder allocates its output textures on the device that
 * `WindowsVideoInput::createZeroCopyInput` is given, and those textures are shared with the effect
 * graph. That puts four requirements on the device, and a host that brings its own has to meet all of
 * them:
 *
 * - it is created with `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`, which Media Foundation's device manager
 *   requires before a decoder can draw into it;
 * - it is created with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`, because the shared BGRA textures the graph
 *   imports are created on it;
 * - it is multi-thread protected, because the decoder fills textures on its own threads while the
 *   render thread reads them;
 * - it has a video processor (`ID3D11VideoDevice`), which is what converts the decoder's NV12 output on
 *   the GPU.
 *
 * <p>This class creates exactly such a device for a host that has no device of its own - a player-only
 * application. A host that already renders with Direct3D 11 passes its device to
 * `WindowsVideoInput::createZeroCopyInput` directly and keeps it alive itself; {@link device()} returns
 * the same borrowed pointer for code that wants to use this class as the owner.
 *
 * <p>The device is only a decode and conversion target. The effect graph renders on its own RHI device,
 * and the shared textures are what crosses between them.
 */
class WindowsVideoDevice final {
public:
  /**
   * Creates a hardware device with the flags and protection the zero-copy path needs.
   *
   * @return the device, or null when the host has no Direct3D 11 device that Media Foundation accepts.
   */
  static std::shared_ptr<WindowsVideoDevice> create();

  ~WindowsVideoDevice();

  WindowsVideoDevice(const WindowsVideoDevice&) = delete;
  WindowsVideoDevice& operator=(const WindowsVideoDevice&) = delete;

  /** The borrowed `ID3D11Device*`; valid for as long as this object is alive. */
  [[nodiscard]] void* device() const noexcept;

  [[nodiscard]] bool isValid() const noexcept;

  struct State;

private:
  explicit WindowsVideoDevice(std::shared_ptr<State> state) noexcept;

  std::shared_ptr<State> mState;
};

} // namespace coral

#endif // _WIN32

#endif // CORAL_PLAYBACK_WIN32_WINDOWS_VIDEO_DEVICE_H
