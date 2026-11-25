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

#include "ImageRenderer.h"

#include "VideoEffectGraph.h"

#include <effect/VideoFrameInputNode.h>

#include <graphics/Data.h>
#include <graphics/Image.h>
#include <graphics/Pixmap.h>

#include <media/PacketBuffer.h>

#include <minilog/log.h>

#include <utility>

namespace coral {

ImageRenderer::ImageRenderer()
  : BaseRenderer(MediaType::kMediaTypeImage) {
  mEncodedBuffer = std::make_shared<AVPacketBuffer>(MediaType::kMediaTypeImage);
}

ImageRenderer::~ImageRenderer() = default;

void ImageRenderer::setVideoEffectGraph(std::shared_ptr<VideoEffectGraph> graph) {
  mVideoEffectGraph = std::move(graph);
  resetDecodedFrame();
}

ImageRenderer::Capabilities ImageRenderer::supportsFormat(
    const std::shared_ptr<MediaFormat>& format) {
  if (!format->isImage()) {
    return SelectionCapabilities::create(FORMAT_UNSUPPORTED_TYPE);
  }
  return SelectionCapabilities::create(FORMAT_HANDLED, ADAPTIVE_NOT_SEAMLESS,
                                       TUNNELING_NOT_SUPPORTED);
}

void ImageRenderer::render(const int64_t positionUs, const int64_t elapsedRealtimeUs) {
  (void)elapsedRealtimeUs;
  if (mDecoded) {
    return;
  }
  if (mVideoEffectGraph == nullptr) {
    return;
  }

  FormatHolder& holder = getFormatHolder();
  mEncodedBuffer->clear();
  const bool formatRequired = mFormat == nullptr;
  const ReadDataResult result = readSource(holder, mEncodedBuffer.get(), formatRequired);
  if (result == ReadDataResult::kResultFormatRead) {
    mFormat = holder.format;
    return;
  }
  if (result != ReadDataResult::kResultBufferRead) {
    return;
  }
  if (mEncodedBuffer->size() == 0 || mEncodedBuffer->array() == nullptr) {
    return;
  }

  std::string error;
  const graphics::Image image = graphics::Image::decode(
      graphics::Data::makeCopy(mEncodedBuffer->array(), mEncodedBuffer->size()), &error);
  if (image.isEmpty()) {
    MINILOG_ERROR("the image could not be decoded: {}", error);
    return;
  }

  const graphics::Pixmap pixels = image.makePixmap();
  const auto* const data = static_cast<const uint8_t*>(pixels.addr(0, 0));
  if (data == nullptr) {
    return;
  }

  VideoFrameDesc frame;
  frame.width = static_cast<uint32_t>(image.width());
  frame.height = static_cast<uint32_t>(image.height());
  frame.format = EffectPixelFormat::RGBA8;
  frame.colorSpace = EffectColorSpace::SRGB;
  frame.source = EffectFrameSource::Planes;
  frame.planeCount = 1;
  frame.planes[0] = { data, static_cast<uint32_t>(pixels.rowBytes()) };
  frame.timestamp = RenderTime::fromTicks(positionUs, 1'000'000);
  if (mVideoEffectGraph->submit(frame) != EffectResult::Success) {
    MINILOG_WARN("the effect graph rejected the decoded image");
    return;
  }
  mDecoded = true;
}

void ImageRenderer::handleMessage(int type, const Message* msg) {
  (void)type;
  (void)msg;
}

bool ImageRenderer::isReady() {
  // An image track has one sample; there is nothing to buffer. Reporting ready from the start lets
  // the playback clock run, and the first render call performs the decode.
  return true;
}

bool ImageRenderer::isEnded() {
  // The single image has been decoded and submitted. The timeline duration decides when the item
  // itself ends; the renderer has no more output to produce.
  return mDecoded;
}

void ImageRenderer::onPositionReset(int64_t positionUs, bool joining) {
  (void)positionUs;
  (void)joining;
  resetDecodedFrame();
}

void ImageRenderer::onDisabled() {
  resetDecodedFrame();
}

void ImageRenderer::resetDecodedFrame() {
  mDecoded = false;
  mFormat.reset();
  if (mEncodedBuffer != nullptr) {
    mEncodedBuffer->clear();
  }
}

} // namespace coral
