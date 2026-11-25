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

#include "MetadataRenderer.h"

#include <media/PacketBuffer.h>

#include <utility>

namespace coral {

MetadataRenderer::MetadataRenderer()
  : BaseRenderer(MediaType::kMediaTypeMetadata),
    mFactory(createDefaultMetadataDecoderFactory()) {
}

MetadataRenderer::~MetadataRenderer() = default;

void MetadataRenderer::setMetadataListener(MetadataListener listener) {
  mMetadataListener = std::move(listener);
}

void MetadataRenderer::setMetadataOutput(const std::shared_ptr<MetadataOutput>& output) {
  mMetadataOutput = output;
}

SelectionCapabilities::Capabilities
MetadataRenderer::supportsFormat(const std::shared_ptr<MediaFormat>& format) {
  if (format == nullptr || MimeTypes::getMediaType(format->getMimeType()) !=
                             MediaType::kMediaTypeMetadata) {
    return SelectionCapabilities::create(FORMAT_UNSUPPORTED_TYPE);
  }
  if (mFactory == nullptr || !mFactory->supportsFormat(*format)) {
    return SelectionCapabilities::create(FORMAT_UNSUPPORTED_SUBTYPE);
  }
  return SelectionCapabilities::create(FORMAT_HANDLED, ADAPTIVE_NOT_SUPPORTED,
    TUNNELING_NOT_SUPPORTED);
}

void MetadataRenderer::onStreamChanged(const std::vector<std::shared_ptr<MediaFormat>>& formats,
                                       const int64_t startPositionUs, const int64_t offsetUs) {
  static_cast<void>(startPositionUs);
  static_cast<void>(offsetUs);
  clearQueue();
  mInputEnded = false;
  mDecoder.reset();
  mFormat.reset();
  if (!formats.empty() && formats.front() != nullptr) {
    onInputFormatChanged(formats.front());
  }
}

void MetadataRenderer::onInputFormatChanged(const std::shared_ptr<MediaFormat>& format) {
  mFormat = format;
  mDecoder.reset();
  if (format != nullptr && mFactory != nullptr && mFactory->supportsFormat(*format)) {
    mDecoder = mFactory->create(*format);
  }
}

void MetadataRenderer::onPositionReset(const int64_t positionUs, const bool joining) {
  static_cast<void>(positionUs);
  static_cast<void>(joining);
  clearQueue();
  mInputEnded = false;
  if (mDecoder != nullptr) {
    mDecoder->reset();
  }
}

void MetadataRenderer::onDisabled() {
  clearQueue();
}

void MetadataRenderer::onReset() {
  clearQueue();
  mDecoder.reset();
}

void MetadataRenderer::clearQueue() {
  mPending.clear();
  mInputEnded = true;
}

bool MetadataRenderer::readSamples() {
  if (mInputEnded) {
    return false;
  }
  if (mFormat == nullptr) {
    auto& formatHolder = getFormatHolder();
    AVPacketBuffer flagsOnly(MediaType::kMediaTypeMetadata);
    const auto result = readSource(formatHolder, &flagsOnly, true);
    if (result == ReadDataResult::kResultFormatRead) {
      onInputFormatChanged(formatHolder.format);
    } else if (result == ReadDataResult::kResultBufferRead) {
      mInputEnded = true;
      return false;
    } else {
      return true;
    }
  }
  if (mDecoder == nullptr) {
    mInputEnded = true;
    return false;
  }

  constexpr int kMaxSamplesPerRender = 100;
  for (int i = 0; i < kMaxSamplesPerRender; ++i) {
    auto packet = std::make_shared<AVPacketBuffer>(MediaType::kMediaTypeMetadata);
    auto& formatHolder = getFormatHolder();
    const auto result = readSource(formatHolder, packet.get(), false);
    if (result == ReadDataResult::kResultFormatRead) {
      onInputFormatChanged(formatHolder.format);
      packet->release();
      if (mDecoder == nullptr) {
        mInputEnded = true;
        return false;
      }
      continue;
    }
    if (result == ReadDataResult::kResultNothingRead) {
      packet->release();
      break;
    }
    if (packet->isEndOfStream()) {
      packet->release();
      mInputEnded = true;
      break;
    }
    auto metadata = std::make_shared<Metadata>(packet->timeUs(),
      std::vector<std::shared_ptr<Metadata::Entry>>{});
    mDecoder->decode(packet->array(), packet->size(), metadata);
    packet->release();
    if (metadata != nullptr && !metadata->getEntries().empty()) {
      mPending.push_back(*metadata);
    }
  }
  if (!mInputEnded && hasReadStreamToEnd()) {
    mInputEnded = true;
  }
  return !mInputEnded;
}

void MetadataRenderer::render(const int64_t positionUs, const int64_t elapsedRealtimeUs) {
  static_cast<void>(elapsedRealtimeUs);
  if (mInputEnded && mPending.empty()) {
    return;
  }
  readSamples();
  deliverReadyMetadata(positionUs);
}

void MetadataRenderer::deliverReadyMetadata(const int64_t positionUs) {

  while (!mPending.empty() && mPending.front().getPresentationTimeUs() <= positionUs) {
    const Metadata metadata = mPending.front();
    mPending.pop_front();
    if (mMetadataListener != nullptr) {
      mMetadataListener(metadata);
    }
    if (mMetadataOutput != nullptr) {
      mMetadataOutput->onMetadata(metadata);
    }
  }
}

bool MetadataRenderer::isReady() {
  return true;
}

bool MetadataRenderer::isEnded() {
  return mInputEnded && mPending.empty();
}

void MetadataRenderer::handleMessage(int type, const Message* msg) {
  static_cast<void>(type);
  static_cast<void>(msg);
}

} // namespace coral
