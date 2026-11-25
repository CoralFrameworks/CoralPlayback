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

#ifndef CORAL_PLAYBACK_APPLE_APPLE_VIDEO_FRAME_SOURCE_H
#define CORAL_PLAYBACK_APPLE_APPLE_VIDEO_FRAME_SOURCE_H

#include <coral/playback/VideoFrameOutput.h>

#include <graphics/ExternalImage.h>

#include <cstdint>
#include <memory>
#include <string>

namespace backend {
class Device;
}

namespace coral {

/**
 * Imports the player's IOSurface output into a UI device.
 *
 * <p>This is the macOS half of the player/UI surface bridge. The player writes a composed frame
 * into its {@link AppleIOSurface}; the UI calls {@link refresh} on its render thread, with its
 * device current, and draws the returned {@link graphics::ExternalImage}. The import is the RHI's
 * IOSurface import, so no pixels are copied.
 *
 * <p>The source caches the imported texture until the player reports a new sequence or a different
 * surface. The caller must keep the device alive for the source's lifetime and must call the source
 * only while that device's context is current.
 *
 * <p>The caller should wait on {@link VideoFrameOutput::fence} before sampling the surface. The
 * source only imports the image; it does not wait for the player's GPU work.
 */
class AppleVideoFrameSource final {
public:
  /**
   * @param surface the application's output surface; retained for the source's lifetime.
   * @param device  the UI device that imports it; borrowed and must outlive the source.
   */
  static std::shared_ptr<AppleVideoFrameSource> create(
      std::shared_ptr<VideoSurface> surface, backend::Device& device);

  ~AppleVideoFrameSource();

  AppleVideoFrameSource(const AppleVideoFrameSource&) = delete;
  AppleVideoFrameSource& operator=(const AppleVideoFrameSource&) = delete;

  /**
   * Imports the surface when @p output is newer than the cached one.
   *
   * @return true when the image is ready; false when the surface is missing, not an IOSurface, or
   *         the RHI rejects the import.
   */
  bool refresh(const VideoFrameOutput& output, std::string& error);

  /** The image of the last successful refresh, or an invalid image before the first one. */
  [[nodiscard]] const graphics::ExternalImage& image() const noexcept;

  /** The number of imports performed, for diagnostics. */
  [[nodiscard]] uint64_t importCount() const noexcept;

private:
  AppleVideoFrameSource(std::shared_ptr<VideoSurface> surface, backend::Device& device);

  void releaseTexture();

  std::shared_ptr<VideoSurface> mSurface;
  backend::Device* mDevice = nullptr;
  backend::TextureHandle mTexture = backend::TextureHandle::invalid();
  graphics::ExternalImage mImage;
  VideoFrameOutput mOutput;
  uint64_t mImportCount = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_APPLE_APPLE_VIDEO_FRAME_SOURCE_H
