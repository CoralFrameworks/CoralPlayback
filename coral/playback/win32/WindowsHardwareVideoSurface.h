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

#ifndef CORAL_PLAYBACK_WIN32_WINDOWS_HARDWARE_VIDEO_SURFACE_H
#define CORAL_PLAYBACK_WIN32_WINDOWS_HARDWARE_VIDEO_SURFACE_H

#if defined(_WIN32)

#include <codec/HardwareVideo.h>

#include <memory>

namespace coral {

/**
 * The decoder surface that makes the Windows decoder allocate its frames on a Direct3D 11 device.
 *
 * <p>This is the Windows counterpart of the Apple `AppleHardwareVideoSurface`: handing it to
 * `LivePlayer::enableVideoEffectPipeline` as `VideoEffectInput::decoderSurface` makes the WMF video
 * decoder open a Media Foundation DXGI device manager on {@link device()} and hand back
 * `WmfHardwarePixelBuffer`s - D3D11 textures - instead of copying the NV12 planes to system memory.
 *
 * <p>The device is borrowed and has to outlive the surface, the player and every frame the decoder
 * hands out. It must be created with `D3D11_CREATE_DEVICE_VIDEO_SUPPORT` (Media Foundation's device
 * manager requires it) and with multi-thread protection enabled: the decoder fills the textures on its
 * own threads while the player's render thread reads them. {@link create} enables the protection when
 * the device offers it.
 *
 * <p>Unlike VideoToolbox on Apple, the Windows hardware path needs the device - a decoder without one
 * cannot allocate textures - so a null device makes {@link create} return null and
 * `VideoEffectInput::decoderSurface` empty, which makes the decoder fall back to CPU planes.
 *
 * <p>The zero-copy input aggregate that uses this surface is {@link WindowsVideoInput}.
 */
class WindowsHardwareVideoSurface final : public HardwareVideoSurface {
public:
  /**
   * @param d3d11Device an `ID3D11Device*` the decoder and the player share; borrowed, not owned.
   * @return the surface, or null when @p d3d11Device is null.
   */
  static std::shared_ptr<WindowsHardwareVideoSurface> create(void* d3d11Device);

  ~WindowsHardwareVideoSurface() override;

  WindowsHardwareVideoSurface(const WindowsHardwareVideoSurface&) = delete;
  WindowsHardwareVideoSurface& operator=(const WindowsHardwareVideoSurface&) = delete;

  /** The borrowed device, as the opaque handle the `VideoSurface` contract uses. */
  [[nodiscard]] void* getInstance() const override;

  /** The device the decoder allocates its output textures on. */
  [[nodiscard]] void* getNativeDevice() const override;

  [[nodiscard]] bool isValid() const override;

  /** Does nothing: the decoder's textures follow the negotiated output type. */
  void setDefaultBufferSize(int width, int height) override;

  /** Does nothing: the device is borrowed. */
  void release() override;

  /** The borrowed `ID3D11Device*`, or null. */
  [[nodiscard]] void* device() const noexcept {
    return mDevice;
  }

private:
  explicit WindowsHardwareVideoSurface(void* device) noexcept;

  void* mDevice = nullptr;
};

} // namespace coral

#endif // _WIN32

#endif // CORAL_PLAYBACK_WIN32_WINDOWS_HARDWARE_VIDEO_SURFACE_H
