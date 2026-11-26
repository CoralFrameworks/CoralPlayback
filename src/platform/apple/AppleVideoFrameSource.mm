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

#include <coral/playback/apple/AppleVideoFrameSource.h>

#include <coral/playback/apple/AppleIOSurface.h>

#include <backend/Device.h>
#include <backend/Handles.h>

#include <utility>

namespace coral {
namespace {

backend::PixelFormat toBackendFormat(const EffectPixelFormat format) noexcept {
  switch (format) {
    case EffectPixelFormat::RGBA8:
      return backend::PixelFormat::R8G8B8A8_UNORM;
    case EffectPixelFormat::BGRA8:
      return backend::PixelFormat::B8G8R8A8_UNORM;
    case EffectPixelFormat::RGBA16F:
      return backend::PixelFormat::R16G16B16A16_SFLOAT;
    default:
      return backend::PixelFormat::Undefined;
  }
}

} // namespace

AppleVideoFrameSource::AppleVideoFrameSource(std::shared_ptr<VideoSurface> surface,
                                             backend::Device& device)
    : mSurface(std::move(surface)), mDevice(&device) {
}

std::shared_ptr<AppleVideoFrameSource> AppleVideoFrameSource::create(
    std::shared_ptr<VideoSurface> surface, backend::Device& device) {
  if (surface == nullptr || !surface->isValid()) {
    return nullptr;
  }
  return std::shared_ptr<AppleVideoFrameSource>(
      new AppleVideoFrameSource(std::move(surface), device));
}

AppleVideoFrameSource::~AppleVideoFrameSource() {
  releaseTexture();
}

void AppleVideoFrameSource::releaseTexture() {
  if (mDevice != nullptr && mTexture.valid()) {
    mDevice->destroyTexture(mTexture);
  }
  mTexture = backend::TextureHandle::invalid();
  mImage = {};
}

bool AppleVideoFrameSource::refresh(const VideoFrameOutput& output, std::string& error) {
  if (mDevice == nullptr || mSurface == nullptr || !mSurface->isValid()) {
    error = "the video frame source has no device or surface";
    return false;
  }
  if (mImage.isValid() && output.surface == mOutput.surface && output.sequence == mOutput.sequence) {
    return true;
  }
  const auto appleSurface = std::dynamic_pointer_cast<AppleIOSurface>(mSurface);
  if (appleSurface == nullptr || !appleSurface->isValid()) {
    error = "the player output is not an AppleIOSurface";
    return false;
  }
  const auto format = toBackendFormat(appleSurface->format());
  if (format == backend::PixelFormat::Undefined) {
    error = "the IOSurface format is not importable";
    return false;
  }
  releaseTexture();
  backend::NativeTextureDescriptor descriptor;
  descriptor.kind = backend::NativeTextureKind::IOSurface;
  descriptor.platformHandle = appleSurface->iosurface();
  descriptor.width = appleSurface->width();
  descriptor.height = appleSurface->height();
  descriptor.format = format;
  descriptor.plane = 0;
  if (!backend::isSuccess(mDevice->importNativeTexture(descriptor, &mTexture)) || !mTexture.valid()) {
    mTexture = backend::TextureHandle::invalid();
    error = "the UI device could not import the player IOSurface";
    return false;
  }
  mImage.texture = mTexture;
  mImage.width = appleSurface->width();
  mImage.height = appleSurface->height();
  mImage.format = format;
  mImage.premultiplied = false;
  mOutput = output;
  ++mImportCount;
  return true;
}

const graphics::ExternalImage& AppleVideoFrameSource::image() const noexcept {
  return mImage;
}

uint64_t AppleVideoFrameSource::importCount() const noexcept {
  return mImportCount;
}

} // namespace coral
