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

#ifndef CORAL_PLAYBACK_APPLE_APPLE_HARDWARE_VIDEO_SURFACE_H
#define CORAL_PLAYBACK_APPLE_APPLE_HARDWARE_VIDEO_SURFACE_H

#include <codec/HardwareVideo.h>

#include <memory>

namespace coral {

/**
 * The decoder surface that asks VideoToolbox for `IOSurface`-backed frames.
 *
 * <p>This is the Apple counterpart of the Windows `Win32HardwareVideoSurface`: handing it to
 * `LivePlayer::enableVideoEffectPipeline` as the decoder surface makes the Darwin decoder return
 * `CVPixelBuffer`s backed by an `IOSurface` instead of copying the planes to system memory. The
 * frames are then read by the Apple `VideoFrameSink`.
 *
 * <p>VideoToolbox does not need a Metal device to allocate its surfaces - a decoder's
 * `CVPixelBuffer` is IOSurface-backed and any Metal device wraps it - so the device is only an
 * optional hint and a null device still requests the hardware output. The current CoralMedia Darwin
 * decoder produces the platform's default bi-planar NV12 layout; converting it to the single-plane
 * layout `AppleIOSurfaceInputNode` imports is the sink's job.
 */
class AppleHardwareVideoSurface final : public HardwareVideoSurface {
public:
  /** @param device an optional, borrowed `id<MTLDevice>` hint; may be null. */
  static std::shared_ptr<AppleHardwareVideoSurface> create(void* device = nullptr);

  ~AppleHardwareVideoSurface() override;

  AppleHardwareVideoSurface(const AppleHardwareVideoSurface&) = delete;
  AppleHardwareVideoSurface& operator=(const AppleHardwareVideoSurface&) = delete;

  /** The optional device hint, or null. */
  [[nodiscard]] void* getInstance() const override;

  [[nodiscard]] void* getNativeDevice() const override;

  [[nodiscard]] bool isValid() const override;

  void setDefaultBufferSize(int width, int height) override;

  /** Does nothing: the surface borrows the device and owns no platform resource. */
  void release() override;

private:
  explicit AppleHardwareVideoSurface(void* device) noexcept;

  void* mDevice = nullptr;
};

} // namespace coral

#endif // CORAL_PLAYBACK_APPLE_APPLE_HARDWARE_VIDEO_SURFACE_H
