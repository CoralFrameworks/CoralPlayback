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

#include <coral/playback/win32/WindowsThumbnailProvider.h>

#if defined(_WIN32)

#include <media/PixelBufferHandle.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace coral {
namespace {

using Microsoft::WRL::ComPtr;

/** A CPU BGRA frame that owns its pixels and exposes the PixelBufferStorage contract. */
class BgraPixelBufferStorage final : public PixelBufferStorage {
public:
  BgraPixelBufferStorage(const uint32_t width, const uint32_t height, std::vector<uint8_t> pixels,
                         const int64_t timeUs)
      : mPixels(std::move(pixels)) {
    mInfo.width = static_cast<int>(width);
    mInfo.height = static_cast<int>(height);
    mInfo.format = PixelFormat::kFormatBGRA;
    mInfo.storage = PixelBufferStorageType::Cpu;
    mInfo.capabilities = PixelBufferStorageCapability::CpuRead |
                         PixelBufferStorageCapability::CpuWrite;
    mInfo.timeUs = timeUs;
    mColor.colorSpace = ColorInfo::SpaceType::kSpaceBT709;
    mColor.colorRange = ColorInfo::RangeType::kRangeLimited;
    mColor.colorTransfer = ColorInfo::TransferType::kTransferSDR;
    mInfo.color = mColor;
  }

  const PixelBufferInfo& info() const override {
    return mInfo;
  }

  bool lock(const PixelBufferLockMode mode, PixelBufferPlane* planes,
            uint32_t* planeCount) override {
    static_cast<void>(mode);
    if (planes == nullptr || planeCount == nullptr || mPixels.empty()) {
      return false;
    }
    planes[0].data = mPixels.data();
    planes[0].rowBytes = mInfo.width * 4;
    planes[0].width = mInfo.width;
    planes[0].height = mInfo.height;
    *planeCount = 1;
    return true;
  }

  void unlock() override {
  }

  void release() override {
  }

private:
  std::vector<uint8_t> mPixels;
  PixelBufferInfo mInfo;
  ColorInfo mColor;
};

std::wstring utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
    static_cast<int>(value.size()), nullptr, 0);
  if (length <= 0) {
    return {};
  }
  std::wstring result(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
    result.data(), length);
  return result;
}

} // namespace

struct WindowsThumbnailProvider::State {
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
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(com);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
      return;
    }
    const std::wstring wideUrl = utf8ToWide(url);
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFAttributes> attributes;
    if (SUCCEEDED(MFCreateAttributes(&attributes, 2))) {
      attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }
    if (wideUrl.empty() ||
        FAILED(MFCreateSourceReaderFromURL(wideUrl.c_str(), attributes.Get(), &reader))) {
      MFShutdown();
      if (comInitialized) {
        CoUninitialize();
      }
      return;
    }
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    ComPtr<IMFMediaType> outputType;
    if (SUCCEEDED(MFCreateMediaType(&outputType))) {
      outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
      outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
      reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType.Get());
    }

    while (true) {
      int64_t positionUs = 0;
      Callback callback;
      uint64_t requestGeneration = 0;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return stop || pending != nullptr; });
        if (stop) {
          break;
        }
        positionUs = pendingPositionUs;
        callback = std::move(pending);
        pending = nullptr;
        requestGeneration = generation;
      }
      if (!callback) {
        continue;
      }
      auto thumbnail = generate(reader.Get(), positionUs);
      {
        const std::lock_guard<std::mutex> lock(mutex);
        if (stop || requestGeneration != generation) {
          continue;
        }
      }
      callback(std::move(thumbnail));
    }

    MFShutdown();
    if (comInitialized) {
      CoUninitialize();
    }
  }

  Thumbnail generate(IMFSourceReader* reader, const int64_t positionUs) {
    Thumbnail thumbnail;
    thumbnail.positionUs = positionUs;
    if (reader == nullptr) {
      return thumbnail;
    }
    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = positionUs * 10;
    const HRESULT seek = reader->SetCurrentPosition(GUID_NULL, position);
    PropVariantClear(&position);
    if (FAILED(seek)) {
      return thumbnail;
    }

    ComPtr<IMFSample> sample;
    for (int attempt = 0; attempt < 120; ++attempt) {
      DWORD streamFlags = 0;
      LONGLONG timestamp = 0;
      sample.Reset();
      const HRESULT read = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr,
        &streamFlags, &timestamp, &sample);
      if (FAILED(read) || (streamFlags & MF_SOURCE_READERF_ERROR) != 0 ||
          (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
        return thumbnail;
      }
      if (sample != nullptr) {
        break;
      }
    }
    if (sample == nullptr) {
      return thumbnail;
    }

    ComPtr<IMFMediaType> currentType;
    UINT32 width = 0;
    UINT32 height = 0;
    if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType)) ||
        FAILED(MFGetAttributeSize(currentType.Get(), MF_MT_FRAME_SIZE, &width, &height)) ||
        width == 0 || height == 0) {
      return thumbnail;
    }
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || buffer == nullptr) {
      return thumbnail;
    }
    BYTE* data = nullptr;
    DWORD length = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &length)) || data == nullptr) {
      return thumbnail;
    }
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    const size_t required = rowBytes * height;
    if (length < required) {
      buffer->Unlock();
      return thumbnail;
    }
    const double scale = std::min(1.0, std::min(
      static_cast<double>(maxWidth) / static_cast<double>(width),
      static_cast<double>(maxHeight) / static_cast<double>(height)));
    const UINT32 targetWidth = std::max<UINT32>(1, static_cast<UINT32>(width * scale));
    const UINT32 targetHeight = std::max<UINT32>(1, static_cast<UINT32>(height * scale));
    std::vector<uint8_t> pixels(static_cast<size_t>(targetWidth) * targetHeight * 4);
    if (targetWidth == width && targetHeight == height) {
      // RGB32 from the source reader is a bottom-up DIB; flip it into the top-down layout a UI
      // thumbnail expects.
      for (UINT32 y = 0; y < height; ++y) {
        const BYTE* source = data + static_cast<size_t>(height - 1 - y) * rowBytes;
        std::copy(source, source + rowBytes, pixels.data() + static_cast<size_t>(y) * rowBytes);
      }
    } else {
      for (UINT32 y = 0; y < targetHeight; ++y) {
        const UINT32 sourceY = height - 1 - std::min(height - 1, y * height / targetHeight);
        for (UINT32 x = 0; x < targetWidth; ++x) {
          const UINT32 sourceX = std::min(width - 1, x * width / targetWidth);
          std::copy(data + static_cast<size_t>(sourceY) * rowBytes + sourceX * 4,
            data + static_cast<size_t>(sourceY) * rowBytes + sourceX * 4 + 4,
            pixels.data() + (static_cast<size_t>(y) * targetWidth + x) * 4);
        }
      }
    }
    buffer->Unlock();
    thumbnail.frame = PixelBuffer::create(std::make_shared<BgraPixelBufferStorage>(
      targetWidth, targetHeight, std::move(pixels), positionUs));
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

WindowsThumbnailProvider::WindowsThumbnailProvider(std::string url, const uint32_t maxWidth,
                                                   const uint32_t maxHeight)
    : mState(std::make_shared<State>(std::move(url), maxWidth, maxHeight)) {
  mState->worker = std::thread([state = mState] { state->run(); });
}

std::shared_ptr<WindowsThumbnailProvider> WindowsThumbnailProvider::create(
    std::string url, const uint32_t maxWidth, const uint32_t maxHeight) {
  if (url.empty() || maxWidth == 0 || maxHeight == 0) {
    return nullptr;
  }
  return std::shared_ptr<WindowsThumbnailProvider>(
      new WindowsThumbnailProvider(std::move(url), maxWidth, maxHeight));
}

WindowsThumbnailProvider::~WindowsThumbnailProvider() = default;

void WindowsThumbnailProvider::requestThumbnail(const int64_t positionUs, Callback callback) {
  {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    mState->pending = std::move(callback);
    mState->pendingPositionUs = positionUs;
    ++mState->generation;
  }
  mState->condition.notify_one();
}

void WindowsThumbnailProvider::cancel() {
  {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    mState->pending = nullptr;
    ++mState->generation;
  }
  mState->condition.notify_one();
}

} // namespace coral

#endif // defined(_WIN32)
