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

#include "VideoFrameConverter.h"

#include <media/ColorInfo.h>
#include <media/PixelBuffer.h>

#include <libyuv/convert.h>

#include <algorithm>
#include <utility>

namespace coral {
namespace {

void applyColorInfo(const std::shared_ptr<AVPixelBuffer>& buffer, VideoFrameDesc* frame) {
  const ColorInfo& color = buffer->getColorInfo();
  frame->bt709 = color.colorSpace == ColorInfo::SpaceType::kSpaceBT709;
  frame->fullRange = color.colorRange == ColorInfo::RangeType::kRangeFull;
  frame->bt2020 = color.colorSpace == ColorInfo::SpaceType::kSpaceBT2020;
  frame->hdr = frame->bt2020 &&
               (color.colorTransfer == ColorInfo::TransferType::kTransferST2084 ||
                color.colorTransfer == ColorInfo::TransferType::kTransferHLG);
  frame->hdrTransfer = color.colorTransfer == ColorInfo::TransferType::kTransferHLG
                           ? EffectHdrTransfer::HLG
                           : EffectHdrTransfer::PQ;
}

} // namespace anonymous

VideoFrameConverter::VideoFrameConverter() = default;

VideoFrameConverter::~VideoFrameConverter() = default;

bool VideoFrameConverter::convert(const std::shared_ptr<AVPixelBuffer>& buffer,
                                  VideoFrameDesc* frame, const int displayWidth,
                                  const int displayHeight) {
  if (buffer == nullptr || frame == nullptr) {
    return false;
  }

  const int bufferWidth = buffer->width();
  const int bufferHeight = buffer->height();
  const int width = displayWidth > 0 ? std::min(displayWidth, bufferWidth) : bufferWidth;
  const int height = displayHeight > 0 ? std::min(displayHeight, bufferHeight) : bufferHeight;
  if (width <= 0 || height <= 0) {
    return false;
  }

  *frame = VideoFrameDesc{};
  frame->width = static_cast<uint32_t>(width);
  frame->height = static_cast<uint32_t>(height);
  frame->format = EffectPixelFormat::YUV420;
  frame->colorSpace = EffectColorSpace::SRGB;
  frame->source = EffectFrameSource::Planes;
  frame->timestamp = RenderTime::fromTicks(buffer->timeUs(), 1'000'000);
  applyColorInfo(buffer, frame);

  if (const auto i420 = std::dynamic_pointer_cast<I420PixelBuffer>(buffer);
      i420 != nullptr) {
    frame->planeCount = 3;
    frame->planes[0] = { i420->dataY(), static_cast<uint32_t>(i420->strideY()) };
    frame->planes[1] = { i420->dataU(), static_cast<uint32_t>(i420->strideU()) };
    frame->planes[2] = { i420->dataV(), static_cast<uint32_t>(i420->strideV()) };
    return true;
  }

  if (const auto nv12 = std::dynamic_pointer_cast<NV12PixelBuffer>(buffer); nv12 != nullptr) {
    // The scratch I420 is sized to the visible rectangle; the source pitch stays the coded one, so libyuv
    // crops the decoder's right/bottom padding instead of converting it into the picture.
    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;
    mNv12ToI420.resize(static_cast<size_t>(width) * height +
                       static_cast<size_t>(chromaWidth) * chromaHeight * 2);
    uint8_t* const y = mNv12ToI420.data();
    uint8_t* const u = y + static_cast<size_t>(width) * height;
    uint8_t* const v = u + static_cast<size_t>(chromaWidth) * chromaHeight;
    if (libyuv::NV12ToI420(nv12->dataY(), nv12->strideY(), nv12->dataUV(),
                           nv12->strideUV(), y, width, u, chromaWidth, v, chromaWidth, width,
                           height) != 0) {
      return false;
    }
    frame->planeCount = 3;
    frame->planes[0] = { y, static_cast<uint32_t>(width) };
    frame->planes[1] = { u, static_cast<uint32_t>(chromaWidth) };
    frame->planes[2] = { v, static_cast<uint32_t>(chromaWidth) };
    return true;
  }

  return false;
}

} // namespace coral
