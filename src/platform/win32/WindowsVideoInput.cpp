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

#include <coral/playback/win32/WindowsVideoInput.h>

#if defined(_WIN32)

#include <coral/playback/win32/WindowsHardwareVideoSurface.h>
#include <coral/playback/win32/WindowsVideoFrameSink.h>

#include "details/CpuVideoFrameSink.h"

#include <effect/VideoFrameInputNode.h>
#include <effect/platform/win/WindowsD3D11InputNode.h>

#include <minilog/log.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace coral {
namespace {

/**
 * Creates the input node's initial texture.
 *
 * <p>`WindowsD3D11InputNode` wants a texture at creation, and the graph renders before the decoder's
 * first frame arrives. A small black shared texture makes those warm-up frames black instead of
 * whatever the driver left in an uninitialised allocation; the first decoded frame replaces it. The
 * texture is shared in the same way the decoder's are, so the node's import path works for it too.
 */
ComPtr<ID3D11Texture2D> createWarmUpTexture(ID3D11Device* device, const uint32_t width,
                                            const uint32_t height) {
  D3D11_TEXTURE2D_DESC description{};
  description.Width = width;
  description.Height = height;
  description.MipLevels = 1;
  description.ArraySize = 1;
  description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  description.MiscFlags =
      D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

  const std::vector<uint8_t> black(static_cast<size_t>(width) * height * 4, 0);
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = black.data();
  data.SysMemPitch = width * 4;
  ComPtr<ID3D11Texture2D> texture;
  if (FAILED(device->CreateTexture2D(&description, &data, &texture))) {
    return nullptr;
  }
  return texture;
}

} // namespace

VideoEffectInput WindowsVideoInput::createZeroCopyInput(void* d3d11Device, const uint32_t width,
                                                         const uint32_t height,
                                                         const uint32_t queueDepth) {
  VideoEffectInput input;
  auto* const device = static_cast<ID3D11Device*>(d3d11Device);
  if (device == nullptr) {
    return input;
  }
  // The surface also turns on the device's multi-thread protection, which the decoder and the render
  // thread both need before either touches the device.
  const auto surface = WindowsHardwareVideoSurface::create(device);
  if (surface == nullptr) {
    return input;
  }

  const uint32_t initialWidth = width == 0 ? 2 : width;
  const uint32_t initialHeight = height == 0 ? 2 : height;
  const auto warmUp = createWarmUpTexture(device, initialWidth, initialHeight);
  if (warmUp == nullptr) {
    MINILOG_WARN("the Windows zero-copy input could not create its warm-up texture");
    return input;
  }
  const auto node = WindowsD3D11InputNode::create(warmUp.Get(), initialWidth, initialHeight,
                                                  EffectPixelFormat::BGRA8,
                                                  queueDepth == 0 ? 4 : queueDepth);
  if (node == nullptr) {
    MINILOG_WARN("the Windows zero-copy input could not create its D3D11 input node");
    return input;
  }
  const auto sink = WindowsVideoFrameSink::create(node, width, height);
  if (sink == nullptr) {
    MINILOG_WARN("the Windows zero-copy input could not create its frame sink");
    return input;
  }

  input.inputNode = node;
  input.decoderSurface = surface;
  input.frameSink = sink;
  return input;
}

VideoEffectInput WindowsVideoInput::createCpuInput(const uint32_t width, const uint32_t height,
                                                    const uint32_t queueDepth) {
  VideoEffectInput input;
  if (width == 0 || height == 0) {
    return input;
  }
  const auto node = VideoFrameInputNode::create(width, height, queueDepth == 0 ? 4 : queueDepth);
  if (node == nullptr) {
    return input;
  }
  input.inputNode = node;
  input.frameSink = std::make_shared<CpuVideoFrameSink>(node);
  return input;
}

} // namespace coral

#endif // _WIN32
