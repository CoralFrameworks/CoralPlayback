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

#include "AppleVideoFrameSink.h"

#include <codec/HardwareVideo.h>
#include <effect/platform/apple/AppleIOSurfaceInputNode.h>
#include <media/ColorInfo.h>
#include <media/PixelBuffer.h>
#include <minilog/log.h>

#include <IOSurface/IOSurface.h>
#include <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace coral {
namespace {

constexpr size_t kConversionSlotCount = 3;

bool isNv12(const CVPixelBufferRef buffer) {
  const auto format = CVPixelBufferGetPixelFormatType(buffer);
  return format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
         format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
}

bool isBgra(const CVPixelBufferRef buffer) {
  return CVPixelBufferGetPixelFormatType(buffer) == kCVPixelFormatType_32BGRA;
}

void applyColorInfo(const ColorInfo& color, PixelBufferView* view) {
  view->bt709 = color.colorSpace == ColorInfo::SpaceType::kSpaceBT709;
  view->fullRange = color.colorRange == ColorInfo::RangeType::kRangeFull;
  view->bt2020 = color.colorSpace == ColorInfo::SpaceType::kSpaceBT2020;
  view->hdr = view->bt2020 &&
              (color.colorTransfer == ColorInfo::TransferType::kTransferST2084 ||
               color.colorTransfer == ColorInfo::TransferType::kTransferHLG);
  view->hdrTransfer = color.colorTransfer == ColorInfo::TransferType::kTransferHLG
                        ? EffectHdrTransfer::HLG
                        : EffectHdrTransfer::PQ;
}

void setInt(CFMutableDictionaryRef dictionary, CFStringRef key, const int32_t value) {
  const CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
  if (number == nullptr) {
    return;
  }
  CFDictionarySetValue(dictionary, key, number);
  CFRelease(number);
}

/** Creates an IOSurface-backed BGRA pixel buffer the effect input can import. */
CVPixelBufferRef createBgraSurface(const uint32_t width, const uint32_t height) {
  CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  if (attributes == nullptr) {
    return nullptr;
  }
  setInt(attributes, kCVPixelBufferWidthKey, static_cast<int32_t>(width));
  setInt(attributes, kCVPixelBufferHeightKey, static_cast<int32_t>(height));
  setInt(attributes, kCVPixelBufferPixelFormatTypeKey,
         static_cast<int32_t>(kCVPixelFormatType_32BGRA));
  CFDictionarySetValue(attributes, kCVPixelBufferMetalCompatibilityKey, kCFBooleanTrue);

  CFMutableDictionaryRef surfaceProperties = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  if (surfaceProperties != nullptr) {
    CFDictionarySetValue(attributes, kCVPixelBufferIOSurfacePropertiesKey, surfaceProperties);
    CFRelease(surfaceProperties);
  }

  CVPixelBufferRef buffer = nullptr;
  const CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, width, height,
                                              kCVPixelFormatType_32BGRA, attributes, &buffer);
  CFRelease(attributes);
  return status == kCVReturnSuccess ? buffer : nullptr;
}

} // namespace anonymous

struct AppleVideoFrameSink::State {
  struct Slot {
    CVPixelBufferRef buffer = nullptr;
    bool inUse = false;
  };

  State(const uint32_t w, const uint32_t h) : width(w), height(h) {
  }

  ~State() {
    if (transfer != nullptr) {
      VTPixelTransferSessionInvalidate(transfer);
      CFRelease(transfer);
      transfer = nullptr;
    }
    for (Slot& slot : slots) {
      if (slot.buffer != nullptr) {
        CVPixelBufferRelease(slot.buffer);
        slot.buffer = nullptr;
      }
    }
  }

  void releaseSlot(const size_t index) {
    const std::lock_guard<std::mutex> lock(mutex);
    if (index < slots.size()) {
      slots[index].inUse = false;
    }
  }

  /**
   * Ensures the conversion ring matches the decoder's surface size.
   *
   * <p>The application creates the input node with the track size it knows, but the decoder is the
   * authority on the surface extent. The ring is therefore built lazily from the first frame and
   * rebuilt if the stream changes size. A rebuild waits until the graph has returned every slot; a
   * frame that arrives while a slot is still in use is dropped rather than resized underneath it.
   */
  bool ensureSlots(const uint32_t surfaceWidth, const uint32_t surfaceHeight) {
    const std::lock_guard<std::mutex> lock(mutex);
    if (surfaceWidth == ringWidth && surfaceHeight == ringHeight && !slots.empty()) {
      return true;
    }
    for (const Slot& slot : slots) {
      if (slot.inUse) {
        return false;
      }
    }
    for (Slot& slot : slots) {
      if (slot.buffer != nullptr) {
        CVPixelBufferRelease(slot.buffer);
        slot.buffer = nullptr;
      }
    }
    slots.clear();
    slots.resize(kConversionSlotCount);
    for (Slot& slot : slots) {
      slot.buffer = createBgraSurface(surfaceWidth, surfaceHeight);
      if (slot.buffer == nullptr) {
        for (Slot& created : slots) {
          if (created.buffer != nullptr) {
            CVPixelBufferRelease(created.buffer);
            created.buffer = nullptr;
          }
        }
        slots.clear();
        ringWidth = 0;
        ringHeight = 0;
        return false;
      }
    }
    ringWidth = surfaceWidth;
    ringHeight = surfaceHeight;
    return true;
  }

  std::shared_ptr<AppleIOSurfaceInputNode> input;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t ringWidth = 0;
  uint32_t ringHeight = 0;
  VTPixelTransferSessionRef transfer = nullptr;
  std::vector<Slot> slots;
  mutable std::mutex mutex;
  std::atomic<uint64_t> submitted { 0 };
  std::atomic<uint64_t> dropped { 0 };
  std::atomic_bool warnedRingFailure { false };
};

namespace {

/** Returns a conversion slot to the ring when the graph no longer samples its surface. */
class SlotLease final {
public:
  SlotLease(std::shared_ptr<AppleVideoFrameSink::State> state, const size_t index)
      : mState(std::move(state)), mIndex(index) {
  }

  ~SlotLease() {
    if (mState != nullptr) {
      mState->releaseSlot(mIndex);
    }
  }

  SlotLease(const SlotLease&) = delete;
  SlotLease& operator=(const SlotLease&) = delete;

private:
  std::shared_ptr<AppleVideoFrameSink::State> mState;
  size_t mIndex = 0;
};

/** Keeps a decoder buffer alive until the graph has sampled its surface and releases it then. */
class SourceLease final {
public:
  explicit SourceLease(std::shared_ptr<AVPixelBuffer> frame) : mFrame(std::move(frame)) {
  }

  ~SourceLease() {
    if (mFrame != nullptr) {
      mFrame->release();
    }
  }

  SourceLease(const SourceLease&) = delete;
  SourceLease& operator=(const SourceLease&) = delete;

private:
  std::shared_ptr<AVPixelBuffer> mFrame;
};

PixelBufferView makeView(const IOSurfaceRef surface, const uint32_t width, const uint32_t height,
                         const int64_t positionUs, const ColorInfo& color,
                         std::shared_ptr<void> lifetime) {
  PixelBufferView view;
  view.width = width;
  view.height = height;
  view.format = EffectPixelFormat::BGRA8;
  view.colorSpace = EffectColorSpace::SRGB;
  view.storage = PixelBufferStorageKind::IOSurface;
  view.capabilities = PixelBufferCapability::TextureSample;
  view.nativeKind = NativeHandleKind::IOSurface;
  view.nativeHandle = surface;
  view.source = EffectFrameSource::HardwareBuffer;
  view.timestamp = RenderTime::fromTicks(positionUs, 1'000'000);
  view.lifetime = std::move(lifetime);
  applyColorInfo(color, &view);
  return view;
}

} // namespace anonymous

AppleVideoFrameSink::AppleVideoFrameSink(std::shared_ptr<State> state,
                                         std::shared_ptr<AppleIOSurfaceInputNode> input)
    : mState(std::move(state)) {
  mState->input = std::move(input);
}

std::shared_ptr<AppleVideoFrameSink> AppleVideoFrameSink::create(
    std::shared_ptr<AppleIOSurfaceInputNode> input, const uint32_t width, const uint32_t height) {
  if (input == nullptr || width == 0 || height == 0) {
    return nullptr;
  }
  auto state = std::make_shared<State>(width, height);
  OSStatus status = VTPixelTransferSessionCreate(kCFAllocatorDefault, &state->transfer);
  if (status != noErr || state->transfer == nullptr) {
    MINILOG_ERROR("the Apple frame sink could not create a pixel transfer session ({})",
                  static_cast<int>(status));
    return nullptr;
  }
  VTSessionSetProperty(state->transfer, kVTPixelTransferPropertyKey_RealTime, kCFBooleanTrue);

  return std::shared_ptr<AppleVideoFrameSink>(
      new AppleVideoFrameSink(std::move(state), std::move(input)));
}

AppleVideoFrameSink::~AppleVideoFrameSink() = default;

void AppleVideoFrameSink::setInputFormat(const std::shared_ptr<MediaFormat>& format) {
  // The conversion slots are sized from the aggregate's width/height, which the application takes
  // from the track. A mid-stream size change is rare and currently drops frames with one warning.
  static_cast<void>(format);
}

bool AppleVideoFrameSink::submit(std::shared_ptr<AVPixelBuffer> frame, const int64_t positionUs) {
  if (mState == nullptr || mState->input == nullptr || frame == nullptr) {
    if (frame != nullptr) {
      frame->release();
    }
    return false;
  }
  const auto hardware = std::dynamic_pointer_cast<HardwarePixelBuffer>(frame);
  if (hardware == nullptr) {
    mState->dropped.fetch_add(1, std::memory_order_relaxed);
    frame->release();
    return false;
  }
  auto* const source = static_cast<CVPixelBufferRef>(hardware->getNativeTexture());
  if (source == nullptr) {
    mState->dropped.fetch_add(1, std::memory_order_relaxed);
    frame->release();
    return false;
  }

  const auto color = frame->getColorInfo();

  if (isBgra(source)) {
    // The decoder already produced the layout the input imports; publish its own surface and keep
    // the frame alive until the graph is done with it.
    const auto width = static_cast<uint32_t>(CVPixelBufferGetWidth(source));
    const auto height = static_cast<uint32_t>(CVPixelBufferGetHeight(source));
    IOSurfaceRef surface = CVPixelBufferGetIOSurface(source);
    if (surface == nullptr) {
      mState->dropped.fetch_add(1, std::memory_order_relaxed);
      frame->release();
      return false;
    }
    auto lease = std::make_shared<SourceLease>(std::move(frame));
    const PixelBufferView view = makeView(surface, width, height, positionUs, color,
                                          std::move(lease));
    if (mState->input->submit(view) != EffectResult::Success) {
      mState->dropped.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    mState->submitted.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  if (!isNv12(source)) {
    mState->dropped.fetch_add(1, std::memory_order_relaxed);
    frame->release();
    return false;
  }

  const auto width = static_cast<uint32_t>(CVPixelBufferGetWidth(source));
  const auto height = static_cast<uint32_t>(CVPixelBufferGetHeight(source));
  if (!mState->ensureSlots(width, height)) {
    if (!mState->warnedRingFailure.exchange(true)) {
      MINILOG_WARN("the Apple frame sink could not resize its conversion ring for {}x{}",
                   width, height);
    }
    mState->dropped.fetch_add(1, std::memory_order_relaxed);
    frame->release();
    return false;
  }

  size_t index = kConversionSlotCount;
  {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    for (size_t candidate = 0; candidate < mState->slots.size(); ++candidate) {
      if (!mState->slots[candidate].inUse) {
        mState->slots[candidate].inUse = true;
        index = candidate;
        break;
      }
    }
  }
  if (index == kConversionSlotCount) {
    mState->dropped.fetch_add(1, std::memory_order_relaxed);
    frame->release();
    return false;
  }
  auto lease = std::make_shared<SlotLease>(mState, index);
  CVPixelBufferRef destination = mState->slots[index].buffer;
  const OSStatus status = VTPixelTransferSessionTransferImage(mState->transfer, source,
                                                              destination);
  if (status != noErr) {
    mState->dropped.fetch_add(1, std::memory_order_relaxed);
    frame->release();
    return false;
  }
  IOSurfaceRef surface = CVPixelBufferGetIOSurface(destination);
  if (surface == nullptr) {
    mState->dropped.fetch_add(1, std::memory_order_relaxed);
    frame->release();
    return false;
  }

  const PixelBufferView view = makeView(surface, width, height, positionUs, color,
                                        std::move(lease));
  // The transfer is synchronous: the source decoder buffer can go back as soon as it returns.
  frame->release();
  if (mState->input->submit(view) != EffectResult::Success) {
    mState->dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  mState->submitted.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void AppleVideoFrameSink::discard(std::shared_ptr<AVPixelBuffer> frame) {
  if (frame != nullptr) {
    frame->release();
  }
}

void AppleVideoFrameSink::flush() {
  // Slots still held by the graph return themselves when their leases are released; dropping the
  // queued platform frames is the effect input node's job.
}

bool AppleVideoFrameSink::isZeroCopy() const {
  return mState != nullptr && mState->input != nullptr && mState->input->isZeroCopy();
}

uint64_t AppleVideoFrameSink::submittedFrameCount() const noexcept {
  return mState != nullptr ? mState->submitted.load(std::memory_order_relaxed) : 0;
}

uint64_t AppleVideoFrameSink::droppedFrameCount() const noexcept {
  return mState != nullptr ? mState->dropped.load(std::memory_order_relaxed) : 0;
}

} // namespace coral
