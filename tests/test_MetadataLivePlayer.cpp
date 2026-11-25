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

#include <coral/playback/MetadataOutput.h>
#include <coral/playback/Player.h>
#include <coral/selection/TrackGroup.h>
#include <coral/source/BaseMediaSource.h>
#include <coral/source/FormatHolder.h>
#include <coral/source/MediaItem.h>
#include <coral/source/MediaPeriod.h>
#include <coral/source/MediaSourceFactory.h>
#include <coral/source/ProgressiveMediaSource.h>
#include <coral/source/SampleStream.h>
#include <coral/source/SinglePeriodTimeline.h>

#include "TestMedia.h"

#include <media/Definitions.h>
#include <media/Format.h>
#include <media/Metadata.h>
#include <media/MetadataEntries.h>
#include <media/PacketBuffer.h>
#include <media/Types.h>

#include "details/AvLivePlayer.h"
#include "details/MetadataRenderer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace coral {
namespace {

using namespace std::chrono_literals;

constexpr int64_t kDurationUs = 2'000'000;
constexpr int64_t kMetadataTimeUs = 100'000;

void appendU32(std::vector<uint8_t>& out, const uint32_t value) {
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
  appendString(payload, "urn:example:metadata-live");
  appendString(payload, "live");
  appendU32(payload, 1000);
  appendU32(payload, 0);
  appendU32(payload, 500);
  appendU32(payload, 7);
  payload.push_back(1);
  return payload;
}

class MetadataSampleStream final : public SampleStream {
public:
  MetadataSampleStream(std::shared_ptr<MediaFormat> format, std::vector<uint8_t> sample)
      : mFormat(std::move(format)), mSample(std::move(sample)) {
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
      buffer->setFlags(AVBuffer::kEndOfStream);
      return ReadDataResult::kResultBufferRead;
    }
    mSampleSent = true;
    buffer->ensureSpaceForWrite(mSample.size());
    std::memcpy(buffer->array(), mSample.data(), mSample.size());
    buffer->data()->limit(mSample.size());
    buffer->data()->position(0);
    buffer->timeUs(kMetadataTimeUs);
    return ReadDataResult::kResultBufferRead;
  }

  int skipData(int64_t positionUs) override {
    static_cast<void>(positionUs);
    return 0;
  }

private:
  std::shared_ptr<MediaFormat> mFormat;
  std::vector<uint8_t> mSample;
  bool mFormatSent = false;
  bool mSampleSent = false;
};

class MetadataMediaPeriod final : public MediaPeriod,
                                  public std::enable_shared_from_this<MetadataMediaPeriod> {
public:
  MetadataMediaPeriod(std::shared_ptr<MediaFormat> format, std::vector<uint8_t> sample)
      : mFormat(std::move(format)), mSample(std::move(sample)) {
    mTracks = std::make_shared<TrackGroupArray>(std::vector<std::shared_ptr<TrackGroup>>{
      std::make_shared<TrackGroup>(MediaType::kMediaTypeMetadata, mFormat)
    });
  }

  void prepare(Callback* callback, int64_t positionUs) override {
    static_cast<void>(positionUs);
    if (callback != nullptr) {
      callback->onPrepared(shared_from_this());
    }
  }

  const std::shared_ptr<TrackGroupArray>& getTrackGroups() const override {
    return mTracks;
  }

  int64_t selectTracks(const std::vector<std::shared_ptr<TrackSelection>>& selections,
      const std::vector<bool>& mayRetainStreamFlags,
      std::vector<std::shared_ptr<SampleStream>>& streams,
      std::vector<bool>& streamResetFlags, int64_t positionUs) override {
    static_cast<void>(mayRetainStreamFlags);
    positionUs = std::max(0LL, std::min(positionUs, kDurationUs));
    for (size_t index = 0; index < selections.size(); ++index) {
      if (selections[index] == nullptr) {
        streams[index] = nullptr;
        continue;
      }
      if (streams[index] == nullptr) {
        streams[index] = std::make_shared<MetadataSampleStream>(mFormat, mSample);
        streamResetFlags[index] = true;
      }
    }
    return positionUs;
  }

  int64_t getAdjustedSeekPositionUs(int64_t positionUs,
      const SeekParameters& parameters) override {
    static_cast<void>(parameters);
    return std::max(0LL, std::min(positionUs, kDurationUs));
  }

  int64_t getBufferedPositionUs() override { return Definitions::kTimeEndOfSource; }
  int64_t getNextLoadPositionUs() override { return Definitions::kTimeEndOfSource; }
  bool continueLoading(int64_t positionUs) override {
    static_cast<void>(positionUs);
    return false;
  }
  bool isLoading() override { return false; }
  void discardBuffer(int64_t positionUs, bool toKeyframe) override {
    static_cast<void>(positionUs);
    static_cast<void>(toKeyframe);
  }
  int64_t readDiscontinuity() override { return Definitions::kTimeUnset; }
  int64_t seekToUs(int64_t positionUs) override {
    return std::max(0LL, std::min(positionUs, kDurationUs));
  }
  void reevaluateBuffer(int64_t positionUs) override { static_cast<void>(positionUs); }

private:
  std::shared_ptr<MediaFormat> mFormat;
  std::vector<uint8_t> mSample;
  std::shared_ptr<TrackGroupArray> mTracks;
};

class MetadataMediaSource final : public BaseMediaSource {
public:
  explicit MetadataMediaSource(std::shared_ptr<MediaItem> mediaItem)
      : mMediaItem(std::move(mediaItem)),
        mTimeline(std::make_shared<SinglePeriodTimeline>(
          kDurationUs, true, false, false, mMediaItem)) {
    mFormat = MediaFormat::CreateSubtitleFormat(MimeTypes::APPLICATION_EMSG);
    mSample = emsgSample();
  }

  const std::shared_ptr<MediaItem>& getMediaItem() const override { return mMediaItem; }

  const std::shared_ptr<Timeline>& getInitialTimeline() const override { return mTimeline; }

  std::shared_ptr<MediaPeriod> createPeriod(MediaPeriodId id,
      const std::shared_ptr<Allocator>& allocator, int64_t startPositionUs) override {
    static_cast<void>(id);
    static_cast<void>(allocator);
    static_cast<void>(startPositionUs);
    return std::make_shared<MetadataMediaPeriod>(mFormat, mSample);
  }

  void releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) override {
    static_cast<void>(mediaPeriod);
  }

protected:
  void prepareSourceInternal() override { refreshSourceInfo(mTimeline); }
  void releaseSourceInternal() override {}

private:
  std::shared_ptr<MediaItem> mMediaItem;
  std::shared_ptr<Timeline> mTimeline;
  std::shared_ptr<MediaFormat> mFormat;
  std::vector<uint8_t> mSample;
};

class MetadataSourceFactory final : public MediaSourceFactory {
public:
  std::shared_ptr<MediaSource> createMediaSource(
      const std::shared_ptr<MediaItem>& mediaItem) override {
    return std::make_shared<MetadataMediaSource>(mediaItem);
  }
};

class ProgressiveMetadataSourceFactory final : public MediaSourceFactory {
public:
  std::shared_ptr<MediaSource> createMediaSource(
      const std::shared_ptr<MediaItem>& mediaItem) override {
    return std::make_shared<ProgressiveMediaSource>(mediaItem);
  }
};

class CollectingMetadataOutput final : public MetadataOutput {
public:
  void onMetadata(const Metadata& metadata) override {
    std::lock_guard<std::mutex> lock(mMutex);
    mMetadata.push_back(metadata);
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mMetadata.size();
  }

  Metadata front() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mMetadata.front();
  }

private:
  mutable std::mutex mMutex;
  std::vector<Metadata> mMetadata;
};

bool waitFor(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return predicate();
}

} // namespace

TEST(MetadataLivePlayerTest, DeliversEmsgThroughTheLivePlayerMetadataRenderer) {
  auto renderer = std::make_shared<MetadataRenderer>();
  auto player = std::make_shared<AvLivePlayer>(
    std::vector<std::shared_ptr<Renderer>>{renderer});
  player->setMediaSourceFactory(std::make_shared<MetadataSourceFactory>());
  player->setMediaItem(MediaItem::fromUrl("metadata://emsg"));
  auto output = std::make_shared<CollectingMetadataOutput>();
  player->setMetadataOutput(output);
  player->prepare();

  ASSERT_TRUE(waitFor([&player] {
    return player->getPlaybackState() != Player::State::STATE_IDLE;
  }, 5s)) << "the metadata source was never prepared";
  player->play();

  ASSERT_TRUE(waitFor([&output] { return output->size() > 0; }, 5s))
    << "the metadata renderer never delivered a sample to MetadataOutput";
  const Metadata metadata = output->front();
  ASSERT_EQ(1u, metadata.getEntries().size());
  const auto* emsg = dynamic_cast<const EmsgMetadataEntry*>(metadata.getEntries()[0].get());
  ASSERT_NE(nullptr, emsg);
  EXPECT_EQ("urn:example:metadata-live", emsg->getSchemeIdUri());
  EXPECT_EQ("live", emsg->getValue());
  player->release();
}

TEST(MetadataLivePlayerTest, ReadsTheEmsgTrackOfAnMp4ThroughProgressiveMediaSource) {
  const auto path = std::filesystem::current_path() / "coral-metadata-emsg.mp4";
  const auto media = coral::test::buildMp4WithEmsg(emsgSample());
  {
    std::ofstream file(path.string(), std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.good()) << "cannot create " << path.string();
    file.write(reinterpret_cast<const char*>(media.data()),
      static_cast<std::streamsize>(media.size()));
    ASSERT_TRUE(file.good()) << "cannot write " << path.string();
  }

  auto renderer = std::make_shared<MetadataRenderer>();
  auto player = std::make_shared<AvLivePlayer>(
    std::vector<std::shared_ptr<Renderer>>{renderer});
  player->setMediaSourceFactory(std::make_shared<ProgressiveMetadataSourceFactory>());
  player->setMediaItem(MediaItem::fromUrl(path.string()));
  auto output = std::make_shared<CollectingMetadataOutput>();
  player->setMetadataOutput(output);
  player->prepare();

  ASSERT_TRUE(waitFor([&player] {
    return player->getPlaybackState() != Player::State::STATE_IDLE;
  }, 5s)) << "the progressive MP4 was never prepared";
  player->play();

  ASSERT_TRUE(waitFor([&output] { return output->size() > 0; }, 5s))
    << "the MP4 emsg track never reached MetadataOutput";
  const Metadata metadata = output->front();
  ASSERT_EQ(1u, metadata.getEntries().size());
  const auto* emsg = dynamic_cast<const EmsgMetadataEntry*>(metadata.getEntries()[0].get());
  ASSERT_NE(nullptr, emsg);
  EXPECT_EQ("urn:example:metadata-live", emsg->getSchemeIdUri());
  EXPECT_EQ("live", emsg->getValue());

  player->release();
  std::filesystem::remove(path);
}

TEST(MetadataLivePlayerTest, ReadsTheId3TrackOfAnMp3ThroughProgressiveMediaSource) {
  const auto path = std::filesystem::current_path() / "coral-metadata-id3.mp3";
  const auto media = coral::test::buildMp3(2, true);
  {
    std::ofstream file(path.string(), std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.good()) << "cannot create " << path.string();
    file.write(reinterpret_cast<const char*>(media.data()),
      static_cast<std::streamsize>(media.size()));
  }

  auto renderer = std::make_shared<MetadataRenderer>();
  auto player = std::make_shared<AvLivePlayer>(
    std::vector<std::shared_ptr<Renderer>>{renderer});
  player->setMediaSourceFactory(std::make_shared<ProgressiveMetadataSourceFactory>());
  player->setMediaItem(MediaItem::fromUrl(path.string()));
  auto output = std::make_shared<CollectingMetadataOutput>();
  player->setMetadataOutput(output);
  player->prepare();

  ASSERT_TRUE(waitFor([&player] {
    return player->getPlaybackState() != Player::State::STATE_IDLE;
  }, 5s)) << "the MP3 was never prepared";
  player->play();
  ASSERT_TRUE(waitFor([&output] { return output->size() > 0; }, 5s))
    << "the MP3 ID3 track never reached MetadataOutput";
  const Metadata metadata = output->front();
  bool foundTitle = false;
  for (const auto& entry : metadata.getEntries()) {
    if (const auto* frame = dynamic_cast<const Id3FrameMetadataEntry*>(entry.get())) {
      if (frame->getFrameId() == "TIT2" && frame->getText() == "Coral") {
        foundTitle = true;
      }
    }
  }
  EXPECT_TRUE(foundTitle);

  player->release();
  std::filesystem::remove(path);
}

TEST(MetadataLivePlayerTest, ReadsTheScte35TrackOfATsThroughProgressiveMediaSource) {
  const auto path = std::filesystem::current_path() / "coral-metadata-scte35.ts";
  const auto media = coral::test::buildTsWithScte35({0xFC, 0x30, 0x11, 0x00, 0x00, 0x00});
  {
    std::ofstream file(path.string(), std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.good()) << "cannot create " << path.string();
    file.write(reinterpret_cast<const char*>(media.data()),
      static_cast<std::streamsize>(media.size()));
  }

  auto renderer = std::make_shared<MetadataRenderer>();
  auto player = std::make_shared<AvLivePlayer>(
    std::vector<std::shared_ptr<Renderer>>{renderer});
  player->setMediaSourceFactory(std::make_shared<ProgressiveMetadataSourceFactory>());
  player->setMediaItem(MediaItem::fromUrl(path.string()));
  auto output = std::make_shared<CollectingMetadataOutput>();
  player->setMetadataOutput(output);
  player->prepare();

  ASSERT_TRUE(waitFor([&player] {
    return player->getPlaybackState() != Player::State::STATE_IDLE;
  }, 5s)) << "the TS was never prepared";
  player->play();
  ASSERT_TRUE(waitFor([&output] { return output->size() > 0; }, 5s))
    << "the SCTE-35 track never reached MetadataOutput";
  const Metadata metadata = output->front();
  ASSERT_EQ(1u, metadata.getEntries().size());
  const auto* scte = dynamic_cast<const Scte35MetadataEntry*>(metadata.getEntries()[0].get());
  ASSERT_NE(nullptr, scte);
  ASSERT_FALSE(scte->getSpliceInfo().empty());
  EXPECT_EQ(0xFC, scte->getSpliceInfo()[0]);

  player->release();
  std::filesystem::remove(path);
}

} // namespace coral
