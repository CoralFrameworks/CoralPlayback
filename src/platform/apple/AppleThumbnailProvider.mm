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

#include <coral/playback/apple/AppleThumbnailProvider.h>

#if defined(__APPLE__)

#include <media/PixelBufferIOSurface.h>

#include <AVFoundation/AVFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreVideo/CoreVideo.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace coral {

struct AppleThumbnailProvider::State {
  State(std::string source, const uint32_t w, const uint32_t h)
      : url(std::move(source)), maxWidth(w), maxHeight(h) {
  }

  ~State() {
    {
      const std::lock_guard<std::mutex> lock(mutex);
      stop = true;
      pending = nullptr;
      ++generation;
    }
    condition.notify_all();
    if (worker.joinable()) {
      worker.join();
    }
  }

  void run() {
    @autoreleasepool {
      NSURL* const fileUrl = [NSURL fileURLWithPath:[NSString stringWithUTF8String:url.c_str()]];
      AVURLAsset* const asset = [AVURLAsset URLAssetWithURL:fileUrl options:nil];
      AVAssetImageGenerator* const generator = [[AVAssetImageGenerator alloc] initWithAsset:asset];
      generator.appliesPreferredTrackTransform = YES;
      generator.maximumSize = CGSizeMake(maxWidth, maxHeight);

      while (true) {
        int64_t positionUs = 0;
        Callback callback;
        uint64_t requestGeneration = 0;
        {
          std::unique_lock<std::mutex> lock(mutex);
          condition.wait(lock, [this] { return stop || pending != nullptr; });
          if (stop) {
            return;
          }
          positionUs = pendingPositionUs;
          callback = std::move(pending);
          pending = nullptr;
          requestGeneration = generation;
        }
        if (!callback) {
          continue;
        }
        auto thumbnail = generate(generator, positionUs);
        {
          const std::lock_guard<std::mutex> lock(mutex);
          if (stop || requestGeneration != generation) {
            continue;
          }
        }
        callback(std::move(thumbnail));
      }
    }
  }

  Thumbnail generate(AVAssetImageGenerator* generator, const int64_t positionUs) {
    Thumbnail thumbnail;
    thumbnail.positionUs = positionUs;
    const CMTime requested = CMTimeMake(positionUs, 1'000'000);
    CMTime actual = kCMTimeZero;
    NSError* error = nil;
    CGImageRef const image = [generator copyCGImageAtTime:requested actualTime:&actual error:&error];
    if (image == nullptr || error != nil) {
      if (image != nullptr) {
        CGImageRelease(image);
      }
      return thumbnail;
    }
    const size_t width = CGImageGetWidth(image);
    const size_t height = CGImageGetHeight(image);
    if (width == 0 || height == 0) {
      CGImageRelease(image);
      return thumbnail;
    }

    CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (attributes == nullptr) {
      CGImageRelease(image);
      return thumbnail;
    }
    const int32_t pixelWidth = static_cast<int32_t>(width);
    const int32_t pixelHeight = static_cast<int32_t>(height);
    const int32_t format = kCVPixelFormatType_32BGRA;
    CFNumberRef widthNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixelWidth);
    CFNumberRef heightNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixelHeight);
    CFNumberRef formatNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &format);
    if (widthNumber != nullptr) {
      CFDictionarySetValue(attributes, kCVPixelBufferWidthKey, widthNumber);
      CFRelease(widthNumber);
    }
    if (heightNumber != nullptr) {
      CFDictionarySetValue(attributes, kCVPixelBufferHeightKey, heightNumber);
      CFRelease(heightNumber);
    }
    if (formatNumber != nullptr) {
      CFDictionarySetValue(attributes, kCVPixelBufferPixelFormatTypeKey, formatNumber);
      CFRelease(formatNumber);
    }
    CFDictionarySetValue(attributes, kCVPixelBufferMetalCompatibilityKey, kCFBooleanTrue);
    CFMutableDictionaryRef surfaceProperties = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (surfaceProperties != nullptr) {
      CFDictionarySetValue(attributes, kCVPixelBufferIOSurfacePropertiesKey, surfaceProperties);
      CFRelease(surfaceProperties);
    }
    CVPixelBufferRef pixelBuffer = nullptr;
    const CVReturn created = CVPixelBufferCreate(kCFAllocatorDefault, width, height,
                                                 kCVPixelFormatType_32BGRA, attributes, &pixelBuffer);
    CFRelease(attributes);
    if (created != kCVReturnSuccess || pixelBuffer == nullptr) {
      CGImageRelease(image);
      return thumbnail;
    }

    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    void* const base = CVPixelBufferGetBaseAddress(pixelBuffer);
    const size_t rowBytes = CVPixelBufferGetBytesPerRow(pixelBuffer);
    CGColorSpaceRef const colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef const context = CGBitmapContextCreate(base, width, height, 8, rowBytes, colorSpace,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(colorSpace);
    if (context == nullptr) {
      CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
      CVPixelBufferRelease(pixelBuffer);
      CGImageRelease(image);
      return thumbnail;
    }
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    CGContextRelease(context);
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    CGImageRelease(image);

    ColorInfo color;
    color.colorSpace = ColorInfo::SpaceType::kSpaceBT709;
    color.colorRange = ColorInfo::RangeType::kRangeLimited;
    color.colorTransfer = ColorInfo::TransferType::kTransferSDR;
    thumbnail.frame = createIOSurfacePixelBuffer(pixelBuffer, static_cast<int>(width),
      static_cast<int>(height), PixelFormat::kFormatBGRA, color, positionUs,
      [pixelBuffer] { CVPixelBufferRelease(pixelBuffer); });
    return thumbnail;
  }

  const std::string url;
  const uint32_t maxWidth;
  const uint32_t maxHeight;
  std::mutex mutex;
  std::condition_variable condition;
  Callback pending;
  int64_t pendingPositionUs = 0;
  uint64_t generation = 0;
  bool stop = false;
  std::thread worker;
};

AppleThumbnailProvider::AppleThumbnailProvider(std::string url, const uint32_t maxWidth,
                                               const uint32_t maxHeight)
    : mState(std::make_shared<State>(std::move(url), maxWidth, maxHeight)) {
  mState->worker = std::thread([state = mState] { state->run(); });
}

std::shared_ptr<AppleThumbnailProvider> AppleThumbnailProvider::create(
    std::string url, const uint32_t maxWidth, const uint32_t maxHeight) {
  if (url.empty() || maxWidth == 0 || maxHeight == 0) {
    return nullptr;
  }
  return std::shared_ptr<AppleThumbnailProvider>(
      new AppleThumbnailProvider(std::move(url), maxWidth, maxHeight));
}

AppleThumbnailProvider::~AppleThumbnailProvider() = default;

void AppleThumbnailProvider::requestThumbnail(const int64_t positionUs, Callback callback) {
  {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    mState->pending = std::move(callback);
    mState->pendingPositionUs = positionUs;
    ++mState->generation;
  }
  mState->condition.notify_one();
}

void AppleThumbnailProvider::cancel() {
  {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    mState->pending = nullptr;
    ++mState->generation;
  }
  mState->condition.notify_one();
}

} // namespace coral

#endif // defined(__APPLE__)
