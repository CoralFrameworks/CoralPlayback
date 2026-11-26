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

#include <coral/playback/win32/WindowsVideoFrameSink.h>

#if defined(_WIN32)

#include <codec/HardwareVideo.h>
#include <effect/platform/win/WindowsD3D11InputNode.h>
#include <media/ColorInfo.h>
#include <media/PixelBuffer.h>

#include <minilog/log.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace coral {

struct WindowsVideoFrameSink::State {
  struct Slot {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11VideoProcessorOutputView> view;
    bool inUse = false;
  };



  mutable std::mutex mutex;

  /** The decoder's device and its immediate context, borrowed with an own reference. */
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11VideoDevice> videoDevice;
  ComPtr<ID3D11VideoContext> videoContext;
  ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
  ComPtr<ID3D11VideoProcessor> processor;
  /** Signalled when the video processor's blit has completed. */
  ComPtr<ID3D11Query> completionQuery;

  /** The texture extent the processor was built for (the decoder may pad the visible picture). */
  uint32_t processorTextureWidth = 0;
  uint32_t processorTextureHeight = 0;

  std::vector<Slot> slots;
  uint32_t ringWidth = 0;
  uint32_t ringHeight = 0;
  uint32_t nextSlot = 0;

  ColorInfo color;
  bool colorKnown = false;

  std::atomic_bool warnedConversion { false };
  std::atomic_bool warnedColor { false };
  std::atomic_bool warnedHdr { false };
  std::atomic_bool decoderMutexSeen { false };

  std::atomic<uint64_t> submitted { 0 };
  std::atomic<uint64_t> direct { 0 };
  std::atomic<uint64_t> converted { 0 };
  std::atomic<uint64_t> dropped { 0 };
};

namespace {

using namespace std::chrono_literals;

/**
 * How many converted textures the sink keeps in rotation.
 *
 * <p>The player's two frames in flight plus the input node's repeated "current" texture need to stay
 * inside the ring: a slot is reclaimed only when the graph has finished with the frame that named it.
 * Four slots give the sink a free slot while the node still shows the previous image, and a wider ring
 * would only add shared-memory pressure.
 */
constexpr uint32_t kSlotCount = 4;

/** The decoder's texture has to be a single-mip NV12 surface for the video processor to read it. */
bool isNv12Texture(ID3D11Texture2D* texture) {
  D3D11_TEXTURE2D_DESC description{};
  texture->GetDesc(&description);
  return description.MipLevels == 1 && description.ArraySize >= 1 &&
         description.Format == DXGI_FORMAT_NV12;
}

bool sameColor(const ColorInfo& left, const ColorInfo& right) {
  return left.colorSpace == right.colorSpace && left.colorRange == right.colorRange &&
         left.colorTransfer == right.colorTransfer;
}

bool isHdr(const ColorInfo& color) {
  return color.colorSpace == ColorInfo::SpaceType::kSpaceBT2020 ||
         color.colorTransfer == ColorInfo::TransferType::kTransferST2084 ||
         color.colorTransfer == ColorInfo::TransferType::kTransferHLG;
}

/**
 * The stream's colour description for the video processor.
 *
 * <p>The processor has one switch for the YUV matrix (BT.601 or BT.709) and a range field, so this is
 * the whole of the conversion. An unknown matrix is guessed from the picture height the way every
 * player does: HD and above is BT.709, SD is BT.601. An unknown range is read as limited, which is the
 * range almost every file uses.
 */
D3D11_VIDEO_PROCESSOR_COLOR_SPACE toInputColorSpace(const ColorInfo& color,
                                                    const uint32_t frameHeight) {
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE space{};
  // 0 = playback (the processor is allowed to do what a player wants), not video processing.
  space.Usage = 0;
  // The output side is RGB; this field only matters for an RGB input.
  space.RGB_Range = 0;
  switch (color.colorSpace) {
    case ColorInfo::SpaceType::kSpaceBT601:
      space.YCbCr_Matrix = 0;
      break;
    case ColorInfo::SpaceType::kSpaceBT709:
      space.YCbCr_Matrix = 1;
      break;
    case ColorInfo::SpaceType::kSpaceUnknown:
    case ColorInfo::SpaceType::kSpaceBT2020:
    default:
      space.YCbCr_Matrix = frameHeight >= 720 ? 1 : 0;
      break;
  }
  space.YCbCr_xvYCC = 0;
  // `D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235` is 1 and `_0_255` is 2 - not the other way round.
  // An unknown range is read as limited, which is what almost every file uses; getting this backwards
  // converts a limited-range stream as if it were full-range, which raises black to 16 and drops white
  // to 235 (the picture looks washed out rather than wrong).
  switch (color.colorRange) {
    case ColorInfo::RangeType::kRangeFull:
      space.Nominal_Range = 2;
      break;
    case ColorInfo::RangeType::kRangeLimited:
    case ColorInfo::RangeType::kRangeUnknown:
    default:
      space.Nominal_Range = 1;
      break;
  }
  return space;
}

/** The output texture is BGRA and the graph treats it as full-range sRGB. */
D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColorSpace() {
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE space{};
  space.Usage = 0;
  space.RGB_Range = 0;
  space.YCbCr_Matrix = 1;
  space.YCbCr_xvYCC = 0;
  // The output is RGB, so `Nominal_Range` does not apply; full range is the honest value.
  space.Nominal_Range = 2;
  return space;
}

/** Releases the decoder's buffer exactly once, when the graph has finished sampling the texture. */
std::shared_ptr<void> makeDecoderLifetime(std::shared_ptr<AVPixelBuffer> frame) {
  return std::shared_ptr<void>(frame.get(), [frame](void*) mutable { frame->release(); });
}

/** Returns one conversion slot to the ring when the graph no longer samples its texture. */
class SlotLease final {
public:
  SlotLease(std::shared_ptr<WindowsVideoFrameSink::State> state, const uint32_t index)
      : mState(std::move(state)), mIndex(index) {
  }

  ~SlotLease() {
    if (mState != nullptr) {
      const std::lock_guard<std::mutex> lock(mState->mutex);
      if (mIndex < mState->slots.size()) {
        mState->slots[mIndex].inUse = false;
      }
    }
  }

  SlotLease(const SlotLease&) = delete;
  SlotLease& operator=(const SlotLease&) = delete;

private:
  std::shared_ptr<WindowsVideoFrameSink::State> mState;
  uint32_t mIndex = 0;
};

/**
 * Creates or resizes the video processor and its conversion ring.
 *
 * <p>The processor is built for the decoder texture's extent and writes at the visible frame size: a
 * decoder that pads the coded picture (1088 lines for a 1080 line film) is cropped by the stream's
 * source rectangle rather than scaled. A rebuild is refused while the graph still samples a slot of the
 * old ring; the frame that asked for it is dropped and the next one retries.
 */
bool ensureProcessorLocked(WindowsVideoFrameSink::State& state, ID3D11Texture2D* texture,
                           const uint32_t frameWidth, const uint32_t frameHeight,
                           std::string& error) {
  D3D11_TEXTURE2D_DESC textureDescription{};
  texture->GetDesc(&textureDescription);
  if (state.processor != nullptr && state.processorTextureWidth == textureDescription.Width &&
      state.processorTextureHeight == textureDescription.Height && state.ringWidth == frameWidth &&
      state.ringHeight == frameHeight) {
    return true;
  }

  for (const WindowsVideoFrameSink::State::Slot& slot : state.slots) {
    if (slot.inUse) {
      error = "the conversion ring is still in use by an earlier frame";
      return false;
    }
  }
  state.slots.clear();
  state.enumerator.Reset();
  state.processor.Reset();
  state.processorTextureWidth = 0;
  state.processorTextureHeight = 0;
  state.ringWidth = 0;
  state.ringHeight = 0;
  state.colorKnown = false;

  if (state.device == nullptr) {
    ID3D11Device* device = nullptr;
    texture->GetDevice(&device);
    state.device.Attach(device);
    if (state.device != nullptr) {
      ID3D11DeviceContext* context = nullptr;
      state.device->GetImmediateContext(&context);
      state.context.Attach(context);
    }
    if (state.device == nullptr || state.context == nullptr) {
      error = "the frame's D3D11 device is unavailable";
      return false;
    }
    if (FAILED(state.device.As(&state.videoDevice)) || state.videoDevice == nullptr ||
        FAILED(state.context.As(&state.videoContext)) || state.videoContext == nullptr) {
      error = "the D3D11 device has no video processor interface";
      return false;
    }
    D3D11_QUERY_DESC queryDescription{};
    queryDescription.Query = D3D11_QUERY_EVENT;
    if (FAILED(state.device->CreateQuery(&queryDescription, &state.completionQuery)) ||
        state.completionQuery == nullptr) {
      error = "the video processor's completion query could not be created";
      return false;
    }
  }

  D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
  content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  content.InputFrameRate.Numerator = 30;
  content.InputFrameRate.Denominator = 1;
  content.InputWidth = textureDescription.Width;
  content.InputHeight = textureDescription.Height;
  content.OutputWidth = frameWidth;
  content.OutputHeight = frameHeight;
  content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  if (FAILED(state.videoDevice->CreateVideoProcessorEnumerator(&content, &state.enumerator)) ||
      state.enumerator == nullptr) {
    error = "the video processor could not describe an NV12 conversion";
    return false;
  }
  UINT inputSupport = 0;
  UINT outputSupport = 0;
  if (FAILED(state.enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &inputSupport)) ||
      (inputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0) {
    error = "the video processor does not accept NV12 input";
    return false;
  }
  if (FAILED(state.enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                         &outputSupport)) ||
      (outputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
    error = "the video processor does not produce BGRA output";
    return false;
  }
  if (FAILED(state.videoDevice->CreateVideoProcessor(state.enumerator.Get(), 0, &state.processor)) ||
      state.processor == nullptr) {
    error = "the video processor could not be created";
    return false;
  }
  const D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputSpace = outputColorSpace();
  state.videoContext->VideoProcessorSetOutputColorSpace(state.processor.Get(), &outputSpace);

  state.slots.resize(kSlotCount);
  for (uint32_t index = 0; index < kSlotCount; ++index) {
    WindowsVideoFrameSink::State::Slot& slot = state.slots[index];
    D3D11_TEXTURE2D_DESC description{};
    description.Width = frameWidth;
    description.Height = frameHeight;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    description.MiscFlags =
        D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    const HRESULT hr = state.device->CreateTexture2D(&description, nullptr, &slot.texture);
    if (FAILED(hr) || slot.texture == nullptr) {
      char digits[9] = {};
      std::snprintf(digits, sizeof(digits), "%08lx", static_cast<unsigned long>(hr));
      error = "the conversion texture could not be created: 0x";
      error += digits;
      state.slots.clear();
      return false;
    }
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC viewDescription{};
    viewDescription.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    viewDescription.Texture2D.MipSlice = 0;
    if (FAILED(state.videoDevice->CreateVideoProcessorOutputView(
                   slot.texture.Get(), state.enumerator.Get(), &viewDescription, &slot.view)) ||
        slot.view == nullptr) {
      error = "the video processor's output view could not be created";
      state.slots.clear();
      return false;
    }
  }

  state.processorTextureWidth = textureDescription.Width;
  state.processorTextureHeight = textureDescription.Height;
  state.ringWidth = frameWidth;
  state.ringHeight = frameHeight;
  state.nextSlot = 0;
  return true;
}

/** Reserves the next free slot, round-robin so a slot is reused only after the others. */
bool acquireSlotLocked(WindowsVideoFrameSink::State& state, uint32_t* index) {
  if (index == nullptr || state.slots.empty()) {
    return false;
  }
  for (uint32_t offset = 0; offset < state.slots.size(); ++offset) {
    const auto candidate = static_cast<uint32_t>((state.nextSlot + offset) % state.slots.size());
    if (!state.slots[candidate].inUse) {
      state.slots[candidate].inUse = true;
      state.nextSlot = static_cast<uint32_t>((candidate + 1) % state.slots.size());
      *index = candidate;
      return true;
    }
  }
  return false;
}

/** Waits until the recorded blit has completed, so the texture is safe to sample from another API. */
bool waitForCompletion(WindowsVideoFrameSink::State& state, std::string& error) {
  if (state.context == nullptr || state.completionQuery == nullptr) {
    return true;
  }
  state.context->End(state.completionQuery.Get());
  state.context->Flush();
  const auto deadline = std::chrono::steady_clock::now() + 200ms;
  for (;;) {
    const HRESULT status = state.context->GetData(state.completionQuery.Get(), nullptr, 0, 0);
    if (status == S_OK) {
      return true;
    }
    if (status != S_FALSE) {
      error = "the video processor completion query failed";
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      error = "the video processor did not finish in time";
      return false;
    }
    std::this_thread::yield();
  }
}

/** Runs the NV12-to-BGRA conversion of one decoded subresource into @p index's slot. */
bool convertLocked(WindowsVideoFrameSink::State& state, ID3D11Texture2D* texture,
                   const uint32_t subresource, const uint32_t width, const uint32_t height,
                   const ColorInfo& color, const uint32_t index, std::string& error) {
  if (index >= state.slots.size() || state.processor == nullptr) {
    error = "the conversion slot is not available";
    return false;
  }
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDescription{};
  inputDescription.FourCC = 0;
  inputDescription.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  inputDescription.Texture2D.MipSlice = 0;
  inputDescription.Texture2D.ArraySlice = subresource;
  ComPtr<ID3D11VideoProcessorInputView> inputView;
  if (FAILED(state.videoDevice->CreateVideoProcessorInputView(
          texture, state.enumerator.Get(), &inputDescription, &inputView)) ||
      inputView == nullptr) {
    error = "the decoder's texture cannot be read by the video processor";
    return false;
  }

  if (!state.colorKnown || !sameColor(state.color, color)) {
    const D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputSpace = toInputColorSpace(color, height);
    state.videoContext->VideoProcessorSetStreamColorSpace(state.processor.Get(), 0, &inputSpace);
    state.color = color;
    state.colorKnown = true;
  }
  RECT source = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
  state.videoContext->VideoProcessorSetStreamSourceRect(state.processor.Get(), 0, TRUE, &source);
  RECT destination = { 0, 0, static_cast<LONG>(state.ringWidth),
                       static_cast<LONG>(state.ringHeight) };
  state.videoContext->VideoProcessorSetStreamDestRect(state.processor.Get(), 0, TRUE,
                                                      &destination);
  state.videoContext->VideoProcessorSetStreamFrameFormat(state.processor.Get(), 0,
                                                         D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);

  D3D11_VIDEO_PROCESSOR_STREAM stream{};
  stream.Enable = TRUE;
  stream.OutputIndex = 0;
  stream.InputFrameOrField = 0;
  stream.pInputSurface = inputView.Get();

  // The conversion textures are shared with the effect graph, so they carry a keyed mutex; D3D11
  // requires the producer to take it before the video processor writes the texture, even on the device
  // that created it. The mutex is released once the blit has completed, before the graph samples the
  // texture through Vulkan.
  ComPtr<IDXGIKeyedMutex> outputMutex;
  if (SUCCEEDED(state.slots[index].texture.As(&outputMutex)) && outputMutex != nullptr) {
    if (outputMutex->AcquireSync(0, 100) != S_OK) {
      error = "the conversion texture is still busy";
      return false;
    }
  }
  const HRESULT blitted = state.videoContext->VideoProcessorBlt(
      state.processor.Get(), state.slots[index].view.Get(), 0, 1, &stream);
  bool converted = false;
  if (FAILED(blitted)) {
    error = "the video processor blit failed: 0x";
    char digits[9] = {};
    std::snprintf(digits, sizeof(digits), "%08lx", static_cast<unsigned long>(blitted));
    error += digits;
  } else {
    converted = waitForCompletion(state, error);

  }
  if (outputMutex != nullptr) {
    outputMutex->ReleaseSync(0);
  }
  return converted;
}

} // namespace

WindowsVideoFrameSink::WindowsVideoFrameSink(std::shared_ptr<State> state,
                                             std::shared_ptr<WindowsD3D11InputNode> input)
    : mState(std::move(state)), mInput(std::move(input)) {
}

std::shared_ptr<WindowsVideoFrameSink> WindowsVideoFrameSink::create(
    std::shared_ptr<WindowsD3D11InputNode> input, const uint32_t width, const uint32_t height) {
  if (input == nullptr) {
    return nullptr;
  }
  auto state = std::make_shared<State>();
  // A size of zero means "follow the first decoded frame", so the ring is created lazily.
  state->ringWidth = width;
  state->ringHeight = height;
  return std::shared_ptr<WindowsVideoFrameSink>(
      new WindowsVideoFrameSink(std::move(state), std::move(input)));
}

WindowsVideoFrameSink::~WindowsVideoFrameSink() = default;

void WindowsVideoFrameSink::setInputFormat(const std::shared_ptr<MediaFormat>& format) {
  // The first decoded frame's size is authoritative: the decoder may pad the coded picture, and the
  // ring and the video processor are sized from the texture that actually arrives.
  static_cast<void>(format);
}

PixelFormat WindowsVideoFrameSink::processFormat() const {
  // Keep the hardware decoder's native NV12 output: asking a hardware decoder for RGBA moves decoding
  // to the CPU, which is exactly the copy this sink exists to avoid.
  return PixelFormat::kFormatInvalid;
}

bool WindowsVideoFrameSink::submit(std::shared_ptr<AVPixelBuffer> frame, const int64_t positionUs) {
  if (mInput == nullptr || frame == nullptr) {
    if (frame != nullptr) {
      frame->release();
    }
    if (mState != nullptr) {
      ++mState->dropped;
    }
    return false;
  }
  const auto hardware = std::dynamic_pointer_cast<HardwarePixelBuffer>(frame);
  if (hardware == nullptr) {
    // A software MFT produced this frame; there is no texture to sample, and the graph's input node
    // is the D3D11 one. Count it and let the decoder's next frame try again.
    ++mState->dropped;
    frame->release();
    return false;
  }
  auto* const texture = static_cast<ID3D11Texture2D*>(hardware->getNativeTexture());
  if (texture == nullptr) {
    ++mState->dropped;
    frame->release();
    return false;
  }
  const auto width = static_cast<uint32_t>(hardware->width());
  const auto height = static_cast<uint32_t>(hardware->height());
  if (width == 0 || height == 0) {
    ++mState->dropped;
    frame->release();
    return false;
  }
  const RenderTime timestamp = RenderTime::fromTicks(positionUs, 1'000'000);

  if (hardware->getTextureFormat() == HardwarePixelBuffer::TextureFormat::kRgba) {
    // The decoder wrote a layout the input node imports as-is: sample its own allocation.
    const auto result = mInput->submit(texture, width, height, EffectPixelFormat::RGBA8, timestamp,
                                       makeDecoderLifetime(frame));
    if (result != EffectResult::Success) {
      ++mState->dropped;
      return false;
    }
    if (mState->direct.fetch_add(1, std::memory_order_relaxed) == 0) {
      MINILOG_INFO("the Windows sink samples the decoder's RGBA texture directly (zero-copy)");
    }
    ++mState->submitted;
    return true;
  }

  if (hardware->getTextureFormat() != HardwarePixelBuffer::TextureFormat::kNv12 ||
      !isNv12Texture(texture)) {
    if (!mState->warnedConversion.exchange(true, std::memory_order_relaxed)) {
      MINILOG_WARN("the Windows sink cannot read this frame's texture layout; the CPU input is the "
                   "fallback for it");
    }
    ++mState->dropped;
    frame->release();
    return false;
  }

  if (isHdr(hardware->getColorInfo()) &&
      !mState->warnedHdr.exchange(true, std::memory_order_relaxed)) {
    // The video processor would apply the SDR matrix to HDR content; correct tone mapping is a later
    // path, and the CPU input is the one that handles HDR today.
    MINILOG_WARN("the Windows sink converts HDR frames with the SDR matrix; use the CPU input for "
                 "an HDR title");
  }
  if (!mState->warnedColor.exchange(true, std::memory_order_relaxed)) {
    const auto& color = hardware->getColorInfo();
    MINILOG_DEBUG("the Windows sink sees color space={} range={} transfer={}",
                 static_cast<int>(color.colorSpace), static_cast<int>(color.colorRange),
                 static_cast<int>(color.colorTransfer));
  }

  uint32_t index = 0;
  std::string error;
  bool converted = false;
  {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    if (ensureProcessorLocked(*mState, texture, width, height, error)) {
      if (acquireSlotLocked(*mState, &index)) {
        // A hardware MFT may hold the keyed mutex of its output texture while it decodes; acquiring
        // it is what makes us wait for the picture to be complete before the video processor reads it.
        ComPtr<IDXGIKeyedMutex> decoderMutex;
        const bool hasDecoderMutex =
            SUCCEEDED(texture->QueryInterface(IID_PPV_ARGS(&decoderMutex))) && decoderMutex != nullptr;
        if (hasDecoderMutex && decoderMutex->AcquireSync(0, 1000) != S_OK) {
          error = "the decoder texture is still busy";
        } else {
          if (!mState->decoderMutexSeen.exchange(true, std::memory_order_relaxed)) {
            MINILOG_INFO("the Windows sink waits on the decoder texture's keyed mutex");
          }
          converted = convertLocked(*mState, texture, hardware->getSubresource(), width, height,
                                    hardware->getColorInfo(), index, error);
          if (hasDecoderMutex) {
            decoderMutex->ReleaseSync(0);
          }
        }
        if (!converted) {
          mState->slots[index].inUse = false;
        }
      } else if (error.empty()) {
        error = "all conversion slots are in use";
      }
    }
  }
  if (!converted) {
    if (!error.empty() && !mState->warnedConversion.exchange(true, std::memory_order_relaxed)) {
      MINILOG_WARN("the Windows sink dropped an NV12 frame: {}", error);
    }
    ++mState->dropped;
    frame->release();
    return false;
  }

  // The blit has completed, so the decoder may reuse its texture; the converted slot stays leased to
  // the graph until the frame that samples it is done.
  auto lease = std::make_shared<SlotLease>(mState, index);
  const auto result = mInput->submit(mState->slots[index].texture.Get(), width, height,
                                     EffectPixelFormat::BGRA8, timestamp, std::move(lease));
  frame->release();
  if (result != EffectResult::Success) {
    ++mState->dropped;
    return false;
  }
  if (mState->converted.fetch_add(1, std::memory_order_relaxed) == 0) {
    MINILOG_INFO("the Windows sink converts the decoder's NV12 textures on the GPU ({}x{})", width,
                 height);
  }
  ++mState->submitted;
  return true;
}

void WindowsVideoFrameSink::discard(std::shared_ptr<AVPixelBuffer> frame) {
  if (frame != nullptr) {
    frame->release();
  }
}

void WindowsVideoFrameSink::flush() {
  // The decoder's buffers were already returned; the conversion slots come back through their leases
  // when the graph retires the frames that named them.
}

bool WindowsVideoFrameSink::isZeroCopy() const {
  return mInput != nullptr && mInput->isZeroCopy();
}

uint64_t WindowsVideoFrameSink::submittedFrameCount() const noexcept {
  return mState != nullptr ? mState->submitted.load(std::memory_order_relaxed) : 0;
}

uint64_t WindowsVideoFrameSink::directFrameCount() const noexcept {
  return mState != nullptr ? mState->direct.load(std::memory_order_relaxed) : 0;
}

uint64_t WindowsVideoFrameSink::convertedFrameCount() const noexcept {
  return mState != nullptr ? mState->converted.load(std::memory_order_relaxed) : 0;
}

uint64_t WindowsVideoFrameSink::droppedFrameCount() const noexcept {
  return mState != nullptr ? mState->dropped.load(std::memory_order_relaxed) : 0;
}

} // namespace coral

#endif // _WIN32










