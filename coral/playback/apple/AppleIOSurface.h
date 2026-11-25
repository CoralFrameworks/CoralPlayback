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

#ifndef CORAL_PLAYBACK_APPLE_APPLE_IOSURFACE_H
#define CORAL_PLAYBACK_APPLE_APPLE_IOSURFACE_H

#include <codec/MediaCodec.h>

#include <effect/EffectTypes.h>

#include <IOSurface/IOSurfaceRef.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace coral {

/**
 * A `VideoSurface` backed by an `IOSurface`.
 *
 * <p>This is the Apple counterpart of `Win32WindowSurface` and `AndroidSurface`: the surface the
 * application hands to `LivePlayer::setOutputSurface`. The difference is that the platform target is
 * not a window but the shared surface Core Video, VideoToolbox, Metal and Core Animation all use to
 * exchange frames without a CPU copy.
 *
 * <p>The player does not draw into the surface itself. `AppleVideoOutputNodeFactory` turns this
 * surface into an `AppleIOSurfaceOutputNode`, the effect graph's final node, which copies each composed
 * frame into the surface on the GPU. A host then presents the surface with whatever it already has:
 * a `CALayer` whose `contents` is the surface on macOS, a `CAMetalLayer` sampling it, or the encoder
 * input of an export session. On iOS `CALayer.contents` does not accept an `IOSurface`; wrap it in a
 * `CVPixelBuffer` (`CVPixelBufferCreateWithIOSurface`) and enqueue it in an
 * `AVSampleBufferDisplayLayer`, or sample it from a `CAMetalLayer`, instead.
 *
 * ## Pixel layout
 *
 * The default is `BGRA8` (`'BGRA'`), which is the layout Core Animation and the video encoders take
 * natively and the layout `AppleIOSurfaceOutputNode` expects. `RGBA8` and `RGBA16F` are accepted for
 * surfaces this class creates; other layouts (a bi-planar `420v`/`420f` surface) can be wrapped from
 * an existing surface but cannot be used as an effect output, because the output node writes one
 * RGBA texture.
 *
 * ## Lifetime
 *
 * The class owns one reference to the surface for as long as it lives. `fromSurface` retains the
 * caller's surface, so the caller may release its own reference immediately. `release` drops the
 * reference and invalidates the wrapper; a player that still holds the surface must have been given
 * another one or released first. `getInstance` returns the borrowed `IOSurfaceRef` for a platform
 * consumer.
 */
class AppleIOSurface final : public VideoSurface {
public:
  /**
   * Creates a new IOSurface with @p width by @p height pixels in @p format.
   *
   * @return the surface wrapper, or null when the size is zero, the format cannot be allocated, or
   *         `IOSurfaceCreate` fails on this host.
   */
  static std::shared_ptr<AppleIOSurface> create(
      uint32_t width, uint32_t height, EffectPixelFormat format = EffectPixelFormat::BGRA8);

  /**
   * Wraps an existing surface and retains it.
   *
   * <p>The colour format is inferred from the surface's FourCC where it is one of the layouts this
   * class understands; anything else is treated as `BGRA8`, which the effect output refuses for a
   * bi-planar YUV surface.
   */
  static std::shared_ptr<AppleIOSurface> fromSurface(IOSurfaceRef surface);

  /**
   * @return the effect pixel format of @p surface, or @p fallback when the FourCC is not one of the
   *         single-plane RGBA layouts.
   */
  static EffectPixelFormat formatOf(IOSurfaceRef surface,
                                    EffectPixelFormat fallback = EffectPixelFormat::BGRA8);

  ~AppleIOSurface() override;

  AppleIOSurface(const AppleIOSurface&) = delete;
  AppleIOSurface& operator=(const AppleIOSurface&) = delete;

  /** The IOSurface as an opaque platform handle, borrowed for the duration of the call. */
  [[nodiscard]] void* getInstance() const override;

  [[nodiscard]] bool isValid() const override;

  /** Does nothing: an IOSurface's size is fixed at creation. */
  void setDefaultBufferSize(int width, int height) override;

  /** Drops the reference this wrapper holds and invalidates it. Safe to call more than once. */
  void release() override;

  /** The wrapped surface, borrowed. The wrapper keeps its own reference. */
  [[nodiscard]] IOSurfaceRef iosurface() const noexcept {
    return mSurface;
  }

  [[nodiscard]] uint32_t width() const noexcept {
    return mWidth;
  }

  [[nodiscard]] uint32_t height() const noexcept {
    return mHeight;
  }

  [[nodiscard]] EffectPixelFormat format() const noexcept {
    return mFormat;
  }

  /** The distance between two rows in bytes, as the surface reports it. */
  [[nodiscard]] size_t rowBytes() const noexcept;

  /** The allocation size of the surface in bytes, or zero when it is invalid. */
  [[nodiscard]] size_t allocationSize() const noexcept;

private:
  AppleIOSurface(IOSurfaceRef surface, uint32_t width, uint32_t height,
                 EffectPixelFormat format) noexcept;

  IOSurfaceRef mSurface = nullptr;
  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
  EffectPixelFormat mFormat = EffectPixelFormat::BGRA8;
};

} // namespace coral

#endif // CORAL_PLAYBACK_APPLE_APPLE_IOSURFACE_H
