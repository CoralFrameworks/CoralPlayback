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

#ifndef CORAL_PLAYBACK_WIN32_WIN32_VIDEO_FRAME_SOURCE_H
#define CORAL_PLAYBACK_WIN32_WIN32_VIDEO_FRAME_SOURCE_H

#if defined(_WIN32)

#include <coral/playback/VideoFrameOutput.h>

#include <graphics/ExternalImage.h>

#include <cstdint>
#include <memory>
#include <string>

namespace backend {
class Device;
}

namespace coral {

class Win32TextureSurface;

/**
 * Imports the player's shared D3D11 ring texture into a UI device.
 *
 * <p>This is the Windows half of the player/UI surface bridge. It takes the slot the player wrote
 * last, exports its NT shared handle, and imports it through
 * {@code NativeTextureKind::Direct3D11Texture} on the UI device. The three-slot ring means the UI
 * may keep texture N while the player writes slots N+1/N+2; the caller must still finish sampling
 * before the ring wraps, or synchronise with the player's frame fence.
 *
 * <p>The source caches the import per ring slot. The caller must keep the D3D11 device and UI
 * device alive for the source's lifetime, and call it only while the UI device is current.
 */
class Win32VideoFrameSource final {
public:
  static std::shared_ptr<Win32VideoFrameSource> create(
      std::shared_ptr<VideoSurface> surface, backend::Device& device);

  ~Win32VideoFrameSource();

  Win32VideoFrameSource(const Win32VideoFrameSource&) = delete;
  Win32VideoFrameSource& operator=(const Win32VideoFrameSource&) = delete;

  /** Imports the current ring texture when it has not been imported yet. */
  bool refresh(const VideoFrameOutput& output, std::string& error);

  [[nodiscard]] const graphics::ExternalImage& image() const noexcept;

  [[nodiscard]] uint32_t textureIndex() const noexcept;

  [[nodiscard]] uint64_t importCount() const noexcept;

private:
  Win32VideoFrameSource(std::shared_ptr<VideoSurface> surface, backend::Device& device);

  void releaseTextures();

  std::shared_ptr<VideoSurface> mSurface;
  std::shared_ptr<Win32TextureSurface> mTextureSurface;
  backend::Device* mDevice = nullptr;
  static constexpr uint32_t kTextureCount = 3;
  backend::TextureHandle mTextures[kTextureCount] = {};
  graphics::ExternalImage mImage;
  uint32_t mTextureIndex = 0;
  uint64_t mImportCount = 0;
};

} // namespace coral

#endif // defined(_WIN32)

#endif // CORAL_PLAYBACK_WIN32_WIN32_VIDEO_FRAME_SOURCE_H
