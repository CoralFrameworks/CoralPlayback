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

#include <coral/playback/win32/Win32VideoFrameSource.h>

#if defined(_WIN32)

#include <coral/playback/win32/Win32TextureSurface.h>

#include <backend/Device.h>
#include <backend/Handles.h>
#include <effect/platform/win/WindowsD3D11EncoderOutputNode.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <handleapi.h>

#include <utility>

namespace coral {

Win32VideoFrameSource::Win32VideoFrameSource(std::shared_ptr<VideoSurface> surface,
                                             backend::Device& device)
    : mSurface(std::move(surface)), mDevice(&device) {
  mTextureSurface = std::dynamic_pointer_cast<Win32TextureSurface>(mSurface);
}

std::shared_ptr<Win32VideoFrameSource> Win32VideoFrameSource::create(
    std::shared_ptr<VideoSurface> surface, backend::Device& device) {
  if (surface == nullptr || !surface->isValid()) {
    return nullptr;
  }
  return std::shared_ptr<Win32VideoFrameSource>(
      new Win32VideoFrameSource(std::move(surface), device));
}

Win32VideoFrameSource::~Win32VideoFrameSource() {
  releaseTextures();
}

void Win32VideoFrameSource::releaseTextures() {
  if (mDevice != nullptr) {
    for (auto& texture : mTextures) {
      if (texture.valid()) {
        mDevice->destroyTexture(texture);
      }
      texture = backend::TextureHandle::invalid();
    }
  }
  mImage = {};
}

bool Win32VideoFrameSource::refresh(const VideoFrameOutput& output, std::string& error) {
  if (mDevice == nullptr || mTextureSurface == nullptr || !mTextureSurface->isValid()) {
    error = "the Win32 frame source has no UI device or texture surface";
    return false;
  }
  const auto node = mTextureSurface->outputNode();
  if (node == nullptr) {
    error = "the player has not created its D3D11 output node yet";
    return false;
  }
  const uint32_t index = node->lastTextureIndex();
  if (index >= kTextureCount || node->texture(index) == nullptr) {
    error = "the player has not produced a D3D11 texture yet";
    return false;
  }
  if (mTextures[index].valid()) {
    mTextureIndex = index;
    return true;
  }

  ID3D11Texture2D* const texture = node->texture(index);
  IDXGIResource1* resource = nullptr;
  if (FAILED(texture->QueryInterface(__uuidof(IDXGIResource1),
             reinterpret_cast<void**>(&resource))) || resource == nullptr) {
    error = "the D3D11 output texture does not expose IDXGIResource1";
    return false;
  }
  HANDLE sharedHandle = nullptr;
  const HRESULT created = resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                                       &sharedHandle);
  resource->Release();
  if (FAILED(created) || sharedHandle == nullptr) {
    error = "the D3D11 output texture could not be exported as a shared handle";
    return false;
  }

  backend::NativeTextureDescriptor descriptor;
  descriptor.kind = backend::NativeTextureKind::Direct3D11Texture;
  descriptor.platformHandle = sharedHandle;
  descriptor.width = node->width();
  descriptor.height = node->height();
  descriptor.format = backend::PixelFormat::B8G8R8A8_UNORM;
  const auto result = mDevice->importNativeTexture(descriptor, &mTextures[index]);
  // The RHI duplicates the NT handle at import, so the exported handle is not needed afterwards.
  CloseHandle(sharedHandle);
  if (!backend::isSuccess(result) || !mTextures[index].valid()) {
    mTextures[index] = backend::TextureHandle::invalid();
    error = "the UI device could not import the D3D11 shared texture";
    return false;
  }
  mImage.texture = mTextures[index];
  mImage.width = node->width();
  mImage.height = node->height();
  mImage.format = backend::PixelFormat::B8G8R8A8_UNORM;
  mImage.premultiplied = false;
  mTextureIndex = index;
  ++mImportCount;
  return true;
}

const graphics::ExternalImage& Win32VideoFrameSource::image() const noexcept {
  return mImage;
}

uint32_t Win32VideoFrameSource::textureIndex() const noexcept {
  return mTextureIndex;
}

uint64_t Win32VideoFrameSource::importCount() const noexcept {
  return mImportCount;
}

} // namespace coral

#endif // defined(_WIN32)
