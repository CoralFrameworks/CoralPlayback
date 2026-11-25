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

#include <coral/playback/apple/AppleIOSurface.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>

#include <utility>

namespace coral {
namespace {

/** The FourCC of an IOSurface pixel format. */
constexpr uint32_t fourCC(const char a, const char b, const char c, const char d) noexcept {
  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
}

bool isSupportedOutputFormat(const EffectPixelFormat format) noexcept {
  return format == EffectPixelFormat::BGRA8 || format == EffectPixelFormat::RGBA8 ||
         format == EffectPixelFormat::RGBA16F;
}

uint32_t bytesPerElement(const EffectPixelFormat format) noexcept {
  return format == EffectPixelFormat::RGBA16F ? 8u : 4u;
}

uint32_t surfacePixelFormat(const EffectPixelFormat format) noexcept {
  switch (format) {
    case EffectPixelFormat::RGBA8:
      return fourCC('R', 'G', 'B', 'A');
    case EffectPixelFormat::RGBA16F:
      return fourCC('R', 'G', 'h', 'A');
    case EffectPixelFormat::BGRA8:
    default:
      return fourCC('B', 'G', 'R', 'A');
  }
}

void setInt(CFMutableDictionaryRef dictionary, CFStringRef key, const int32_t value) {
  const CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
  if (number == nullptr) {
    return;
  }
  CFDictionarySetValue(dictionary, key, number);
  CFRelease(number);
}

IOSurfaceRef createSurface(const uint32_t width, const uint32_t height,
                           const EffectPixelFormat format) {
  const uint32_t bytes = bytesPerElement(format);
  const uint64_t rowBytes = static_cast<uint64_t>(width) * bytes;
  const uint64_t allocSize = rowBytes * height;
  // The properties carry 32-bit numbers; refuse a surface that would overflow rather than allocate
  // a smaller surface than the caller asked for.
  if (rowBytes > 0x7FFFFFFFull || allocSize > 0x7FFFFFFFull) {
    return nullptr;
  }

  CFMutableDictionaryRef properties = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  if (properties == nullptr) {
    return nullptr;
  }
  setInt(properties, kIOSurfaceWidth, static_cast<int32_t>(width));
  setInt(properties, kIOSurfaceHeight, static_cast<int32_t>(height));
  setInt(properties, kIOSurfaceBytesPerElement, static_cast<int32_t>(bytes));
  setInt(properties, kIOSurfaceBytesPerRow, static_cast<int32_t>(rowBytes));
  setInt(properties, kIOSurfacePixelFormat, static_cast<int32_t>(surfacePixelFormat(format)));
  setInt(properties, kIOSurfaceAllocSize, static_cast<int32_t>(allocSize));

  IOSurfaceRef surface = IOSurfaceCreate(properties);
  CFRelease(properties);
  return surface;
}

} // namespace

AppleIOSurface::AppleIOSurface(IOSurfaceRef surface, const uint32_t width, const uint32_t height,
                               const EffectPixelFormat format) noexcept
    : mSurface(surface),
      mWidth(width),
      mHeight(height),
      mFormat(format) {
}

std::shared_ptr<AppleIOSurface> AppleIOSurface::create(const uint32_t width, const uint32_t height,
                                                       const EffectPixelFormat format) {
  if (width == 0 || height == 0 || !isSupportedOutputFormat(format)) {
    return nullptr;
  }
  IOSurfaceRef surface = createSurface(width, height, format);
  if (surface == nullptr) {
    return nullptr;
  }
  // The creation reference is the wrapper's reference; the destructor releases it.
  return std::shared_ptr<AppleIOSurface>(new AppleIOSurface(surface, width, height, format));
}

std::shared_ptr<AppleIOSurface> AppleIOSurface::fromSurface(IOSurfaceRef surface) {
  if (surface == nullptr) {
    return nullptr;
  }
  CFRetain(surface);
  return std::shared_ptr<AppleIOSurface>(new AppleIOSurface(
      surface, static_cast<uint32_t>(IOSurfaceGetWidth(surface)),
      static_cast<uint32_t>(IOSurfaceGetHeight(surface)), formatOf(surface)));
}

EffectPixelFormat AppleIOSurface::formatOf(const IOSurfaceRef surface,
                                           const EffectPixelFormat fallback) {
  if (surface == nullptr) {
    return fallback;
  }
  switch (static_cast<uint32_t>(IOSurfaceGetPixelFormat(surface))) {
    case fourCC('R', 'G', 'B', 'A'):
      return EffectPixelFormat::RGBA8;
    case fourCC('R', 'G', 'h', 'A'):
    case fourCC('R', 'G', 'f', 'A'):
      return EffectPixelFormat::RGBA16F;
    case fourCC('B', 'G', 'R', 'A'):
      return EffectPixelFormat::BGRA8;
    default:
      return fallback;
  }
}

AppleIOSurface::~AppleIOSurface() {
  release();
}

void* AppleIOSurface::getInstance() const {
  return mSurface;
}

bool AppleIOSurface::isValid() const {
  return mSurface != nullptr;
}

void AppleIOSurface::setDefaultBufferSize(const int width, const int height) {
  static_cast<void>(width);
  static_cast<void>(height);
}

void AppleIOSurface::release() {
  if (mSurface != nullptr) {
    CFRelease(mSurface);
    mSurface = nullptr;
  }
  mWidth = 0;
  mHeight = 0;
}

size_t AppleIOSurface::rowBytes() const noexcept {
  return mSurface != nullptr ? IOSurfaceGetBytesPerRow(mSurface) : 0;
}

size_t AppleIOSurface::allocationSize() const noexcept {
  return mSurface != nullptr ? IOSurfaceGetAllocSize(mSurface) : 0;
}

} // namespace coral
