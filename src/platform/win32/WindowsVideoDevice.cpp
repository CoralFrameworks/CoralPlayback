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

#include <coral/playback/win32/WindowsVideoDevice.h>

#if defined(_WIN32)

#include <d3d10_1.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <utility>

using Microsoft::WRL::ComPtr;

namespace coral {

struct WindowsVideoDevice::State {
  ComPtr<ID3D11Device> device;
  /** The device's lock, kept for multi-thread protection; null when the runtime does not offer it. */
  ComPtr<ID3D10Multithread> multithread;
};

WindowsVideoDevice::WindowsVideoDevice(std::shared_ptr<State> state) noexcept
    : mState(std::move(state)) {
}

std::shared_ptr<WindowsVideoDevice> WindowsVideoDevice::create() {
  // Feature levels down to 10_0: a decoder that works on 10_0 is still a hardware decoder, and asking
  // for 11_0 only would turn a working machine into "no GPU".
  const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                      D3D_FEATURE_LEVEL_10_0 };
  constexpr UINT kLevelCount = static_cast<UINT>(sizeof(levels) / sizeof(levels[0]));
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  ComPtr<ID3D11Device> device;
  const HRESULT created = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                              levels, kLevelCount, D3D11_SDK_VERSION, &device,
                                              &level, nullptr);
  if (FAILED(created) || device == nullptr) {
    return nullptr;
  }

  auto state = std::make_shared<State>();
  state->device = std::move(device);
  // The decoder fills textures on Media Foundation's threads while the render thread reads them; the
  // documentation for the device manager calls for exactly this protection.
  if (SUCCEEDED(state->device.As(&state->multithread)) && state->multithread != nullptr) {
    state->multithread->SetMultithreadProtected(TRUE);
  }
  return std::shared_ptr<WindowsVideoDevice>(new WindowsVideoDevice(std::move(state)));
}

WindowsVideoDevice::~WindowsVideoDevice() = default;

void* WindowsVideoDevice::device() const noexcept {
  return mState != nullptr ? mState->device.Get() : nullptr;
}

bool WindowsVideoDevice::isValid() const noexcept {
  return mState != nullptr && mState->device != nullptr;
}

} // namespace coral

#endif // _WIN32
