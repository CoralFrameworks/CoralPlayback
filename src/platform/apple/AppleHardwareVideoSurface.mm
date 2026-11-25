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

#include <coral/playback/apple/AppleHardwareVideoSurface.h>

namespace coral {

AppleHardwareVideoSurface::AppleHardwareVideoSurface(void* device) noexcept
    : mDevice(device) {
}

std::shared_ptr<AppleHardwareVideoSurface> AppleHardwareVideoSurface::create(void* device) {
  return std::shared_ptr<AppleHardwareVideoSurface>(new AppleHardwareVideoSurface(device));
}

AppleHardwareVideoSurface::~AppleHardwareVideoSurface() = default;

void* AppleHardwareVideoSurface::getInstance() const {
  return mDevice;
}

void* AppleHardwareVideoSurface::getNativeDevice() const {
  return mDevice;
}

bool AppleHardwareVideoSurface::isValid() const {
  // VideoToolbox does not need a device to allocate its IOSurface-backed pixel buffers, so the
  // absence of the optional hint does not make the surface invalid.
  return true;
}

void AppleHardwareVideoSurface::setDefaultBufferSize(const int width, const int height) {
  static_cast<void>(width);
  static_cast<void>(height);
}

void AppleHardwareVideoSurface::release() {
  // The device is borrowed; there is nothing to release.
}

} // namespace coral
