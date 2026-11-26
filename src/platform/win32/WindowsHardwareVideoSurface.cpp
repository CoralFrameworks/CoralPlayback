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

#include <coral/playback/win32/WindowsHardwareVideoSurface.h>

#if defined(_WIN32)

#include <d3d10_1.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdio>

namespace coral {
namespace {

/**
 * Enables D3D11's multi-thread protection.
 *
 * <p>The device is touched from three places at once on this path: Media Foundation fills the decoder's
 * textures on its worker threads, the sink records the video processor pass, and the graph's consumer
 * lives on the render thread. Without the protection the immediate context can be entered concurrently
 * and the driver returns `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE` or corrupts state. Microsoft's device
 * manager documentation recommends it for exactly this reason.
 */
void enableMultithreadProtection(void* const device) {
  auto* const d3dDevice = static_cast<ID3D11Device*>(device);
  if (d3dDevice == nullptr) {
    return;
  }
  Microsoft::WRL::ComPtr<ID3D10Multithread> multithread;
  if (SUCCEEDED(d3dDevice->QueryInterface(IID_PPV_ARGS(&multithread))) && multithread != nullptr) {
    multithread->SetMultithreadProtected(TRUE);
  }
}

} // namespace

WindowsHardwareVideoSurface::WindowsHardwareVideoSurface(void* const device) noexcept
    : mDevice(device) {
}

std::shared_ptr<WindowsHardwareVideoSurface> WindowsHardwareVideoSurface::create(void* device) {
  if (device == nullptr) {
    // Unlike VideoToolbox, the Windows decoder cannot allocate textures without a device, so a
    // surface without one would silently do nothing.
    return nullptr;
  }
  enableMultithreadProtection(device);
  return std::shared_ptr<WindowsHardwareVideoSurface>(new WindowsHardwareVideoSurface(device));
}

WindowsHardwareVideoSurface::~WindowsHardwareVideoSurface() = default;

void* WindowsHardwareVideoSurface::getInstance() const {
  return mDevice;
}

void* WindowsHardwareVideoSurface::getNativeDevice() const {
  return mDevice;
}

bool WindowsHardwareVideoSurface::isValid() const {
  return mDevice != nullptr;
}

void WindowsHardwareVideoSurface::setDefaultBufferSize(const int width, const int height) {
  static_cast<void>(width);
  static_cast<void>(height);
}

void WindowsHardwareVideoSurface::release() {
  // The device is borrowed from the application; nothing to release.
}

} // namespace coral

#endif // _WIN32
