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

#include "details/MetadataRenderer.h"

#include <coral/selection/Configuration.h>
#include <coral/selection/FixedTrackSelection.h>
#include <coral/selection/SelectionCapabilities.h>
#include <coral/selection/TrackGroup.h>
#include <coral/source/FormatHolder.h>
#include <coral/source/SampleStream.h>

#include <media/Buffer.h>
#include <media/Format.h>
#include <media/Metadata.h>
#include <media/MetadataEntries.h>
#include <media/PacketBuffer.h>
#include <media/Types.h>

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using coral::AVPacketBuffer;
using coral::Configuration;
using coral::EmsgMetadataEntry;
using coral::FixedTrackSelection;
using coral::FormatHolder;
using coral::MediaFormat;
using coral::MediaType;
using coral::Metadata;
using coral::MetadataRenderer;
using coral::MimeTypes;
using coral::SampleStream;
using coral::TrackGroup;

namespace {

void appendUInt32(std::vector<uint8_t>& out, const uint32_t value) {
  out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendString(std::vector<uint8_t>& out, const std::string& value) {
  out.insert(out.end(), value.begin(), value.end());
  out.push_back(0);
}

std::vector<uint8_t> emsgSample() {
  std::vector<uint8_t> payload = {0, 0, 0, 0};
  appendString(payload, "urn:example:event");
  appendString(payload, "value");
  appendUInt32(payload, 1000);
  appendUInt32(payload, 0);
  appendUInt32(payload, 500);
  appendUInt32(payload, 7);
  payload.push_back(1);
  return payload;
}

class FakeMetadataStream final : public SampleStream {
public:
  FakeMetadataStream(std::shared_ptr<MediaFormat> format, std::vector<uint8_t> sample,
                     int64_t timeUs)
    : mFormat(std::move(format)),
      mSample(std::move(sample)),
      mTimeUs(timeUs) {
  }

  MediaType getMediaType() const override { return MediaType::kMediaTypeMetadata; }

  bool isReady() const override { return true; }

  ReadDataResult readData(FormatHolder& formatHolder, AVPacketBuffer* buffer,
                          bool formatRequired) override {
    static_cast<void>(formatRequired);
    if (!mFormatSent) {
      formatHolder.format = mFormat;
      mFormatSent = true;
      return ReadDataResult::kResultFormatRead;
    }
    if (mSampleSent) {
      buffer->setFlags(coral::AVBuffer::kEndOfStream);
      return ReadDataResult::kResultBufferRead;
    }
    mSampleSent = true;
    buffer->ensureSpaceForWrite(mSample.size());
    std::memcpy(buffer->array(), mSample.data(), mSample.size());
    buffer->data()->limit(mSample.size());
    buffer->data()->position(0);
    buffer->timeUs(mTimeUs);
    return ReadDataResult::kResultBufferRead;
  }

  int skipData(int64_t positionUs) override {
    static_cast<void>(positionUs);
    return 0;
  }

private:
  std::shared_ptr<MediaFormat> mFormat;
  std::vector<uint8_t> mSample;
  int64_t mTimeUs;
  bool mFormatSent = false;
  bool mSampleSent = false;
};

} // namespace

TEST(MetadataRendererTest, DeliversMetadataWhenThePositionReachesIt) {
  auto format = MediaFormat::CreateSubtitleFormat(MimeTypes::APPLICATION_EMSG);
  auto stream = std::make_shared<FakeMetadataStream>(format, emsgSample(), 500000);
  auto group = std::make_shared<TrackGroup>(MediaType::kMediaTypeMetadata, format);
  auto selection = std::make_shared<FixedTrackSelection>(group, 0);

  MetadataRenderer renderer;
  std::vector<Metadata> delivered;
  renderer.setMetadataListener(
    [&delivered](const Metadata& metadata) { delivered.push_back(metadata); });
  renderer.enable(std::make_shared<Configuration>(), selection, stream, 0, false, false, 0, 0);
  renderer.start();

  renderer.render(0, 0);
  EXPECT_TRUE(delivered.empty());

  renderer.render(600000, 0);
  ASSERT_EQ(1u, delivered.size());
  ASSERT_EQ(1u, delivered.front().getEntries().size());
  const auto* event = dynamic_cast<EmsgMetadataEntry*>(delivered.front().getEntries()[0].get());
  ASSERT_NE(nullptr, event);
  EXPECT_EQ("urn:example:event", event->getSchemeIdUri());
  EXPECT_EQ("value", event->getValue());
  EXPECT_EQ(500000, delivered.front().getPresentationTimeUs());
}

TEST(MetadataRendererTest, AdvertisesTheFormatsItDecodes) {
  MetadataRenderer renderer;
  auto emsg = MediaFormat::CreateSubtitleFormat(MimeTypes::APPLICATION_EMSG);
  EXPECT_EQ(coral::SelectionCapabilities::getFormatSupport(renderer.supportsFormat(emsg)),
    coral::SelectionCapabilities::FORMAT_HANDLED);
  auto subtitle = MediaFormat::CreateSubtitleFormat(MimeTypes::APPLICATION_SUBRIP);
  EXPECT_EQ(coral::SelectionCapabilities::getFormatSupport(renderer.supportsFormat(subtitle)),
    coral::SelectionCapabilities::FORMAT_UNSUPPORTED_TYPE);
}
