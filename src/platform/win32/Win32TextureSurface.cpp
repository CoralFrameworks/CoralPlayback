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

#include <coral/playback/win32/Win32TextureSurface.h>

#if defined(_WIN32)

#include <utility>

namespace coral {

Win32TextureSurface::Win32TextureSurface(void* const device, const uint32_t width,
                                         const uint32_t height) noexcept
    : mDevice(device), mWidth(width), mHeight(height) {
}

std::shared_ptr<Win32TextureSurface> Win32TextureSurface::create(void* const d3d11Device,
                                                                 const uint32_t width,
                                                                 const uint32_t height) {
  if (d3d11Device == nullptr || width == 0 || height == 0) {
    return nullptr;
  }
  return std::shared_ptr<Win32TextureSurface>(
      new Win32TextureSurface(d3d11Device, width, height));
}

Win32TextureSurface::~Win32TextureSurface() = default;

void* Win32TextureSurface::getInstance() const {
  return mDevice;
}

bool Win32TextureSurface::isValid() const {
  return mDevice != nullptr && mWidth > 0 && mHeight > 0;
}

void Win32TextureSurface::release() {
  mOutputNode.reset();
}

void Win32TextureSurface::setDefaultBufferSize(const int width, const int height) {
  static_cast<void>(width);
  static_cast<void>(height);
}

void Win32TextureSurface::setOutputNode(std::shared_ptr<WindowsD3D11EncoderOutputNode> node) {
  mOutputNode = std::move(node);
}

ID3D11Texture2D* Win32TextureSurface::currentTexture() const noexcept {
  return mOutputNode != nullptr ? mOutputNode->texture(mOutputNode->lastTextureIndex()) : nullptr;
}

uint32_t Win32TextureSurface::currentTextureIndex() const noexcept {
  return mOutputNode != nullptr ? mOutputNode->lastTextureIndex() : 0;
}

} // namespace coral

#endif // defined(_WIN32)
