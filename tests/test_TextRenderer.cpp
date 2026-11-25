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

#include "details/TextRenderer.h"

#include <coral/selection/Configuration.h>
#include <coral/selection/SelectionCapabilities.h>
#include <coral/selection/FixedTrackSelection.h>
#include <coral/selection/TrackGroup.h>
#include <coral/source/FormatHolder.h>
#include <coral/source/SampleStream.h>

#include <extractor/text/Cue.h>
#include <extractor/text/CueGroup.h>

#include <media/Definitions.h>
#include <media/Format.h>
#include <media/PacketBuffer.h>
#include <media/Buffer.h>
#include <media/Types.h>

#include <coral/playback/LivePlayer.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using coral::AVCueGroup;
using coral::AVPacketBuffer;
using coral::Configuration;
using coral::Definitions;
using coral::FixedTrackSelection;
using coral::FormatHolder;
using coral::MediaFormat;
using coral::MediaType;
using coral::MimeTypes;
using coral::SampleStream;
using coral::TextRenderer;
using coral::TrackGroup;

namespace {

/** A stream of subtitle samples that answers from memory, with the format first. */
class FakeSubtitleStream final : public SampleStream {
public:
  FakeSubtitleStream(std::shared_ptr<MediaFormat> format, std::vector<std::vector<uint8_t>> samples)
    : mFormat(std::move(format)),
      mSamples(std::move(samples)) {
  }

  MediaType getMediaType() const override { return MediaType::kMediaTypeSubtitle; }

  bool isReady() const override { return mFormat != nullptr || mIndex < mSamples.size(); }

  ReadDataResult readData(FormatHolder& formatHolder, AVPacketBuffer* buffer,
                          bool formatRequired) override {
    static_cast<void>(formatRequired);
    if (!mFormatSent) {
      formatHolder.format = mFormat;
      mFormatSent = true;
      return ReadDataResult::kResultFormatRead;
    }
    if (mIndex >= mSamples.size()) {
      buffer->setFlags(coral::AVBuffer::kEndOfStream);
      return ReadDataResult::kResultBufferRead;
    }
    const auto& sample = mSamples[mIndex++];
    buffer->ensureSpaceForWrite(sample.size());
    std::memcpy(buffer->array(), sample.data(), sample.size());
    buffer->data()->limit(sample.size());
    buffer->data()->position(0);
    buffer->timeUs(0);
    return ReadDataResult::kResultBufferRead;
  }

  int skipData(int64_t positionUs) override {
    static_cast<void>(positionUs);
    return 0;
  }

private:
  std::shared_ptr<MediaFormat> mFormat;
  std::vector<std::vector<uint8_t>> mSamples;
  size_t mIndex = 0;
  bool mFormatSent = false;
};

std::shared_ptr<MediaFormat> makeSrtFormat() {
  auto format = MediaFormat::CreateSubtitleFormat(MimeTypes::APPLICATION_SUBRIP);
  format->setSubsampleOffsetUs(0);
  format->setCueReplacementBehavior(coral::CueReplacementBehavior::kMerge);
  return format;
}

std::vector<uint8_t> srtDocument() {
  const std::string text = "1\n00:00:00,200 --> 00:00:01,500\nHello\n";
  return std::vector<uint8_t>(text.begin(), text.end());
}

void enableRenderer(TextRenderer& renderer, const std::shared_ptr<SampleStream>& stream,
                    const std::shared_ptr<MediaFormat>& format) {
  auto group = std::make_shared<TrackGroup>(MediaType::kMediaTypeSubtitle, format);
  auto selection = std::make_shared<FixedTrackSelection>(group, 0);
  renderer.enable(std::make_shared<Configuration>(), selection, stream, 0, false, false, 0, 0);
  renderer.start();
}

std::string textOf(const AVCueGroup& group) {
  std::string text;
  for (const auto& cue : group.getCues()) {
    text += cue.text;
  }
  return text;
}

} // namespace

namespace {

template<class Predicate>
bool waitUntil(Predicate predicate,
    const std::chrono::milliseconds timeout = std::chrono::milliseconds(8000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return true;
}

class CollectingTextOutput final : public coral::TextOutput {
public:
  void onCues(const AVCueGroup& cueGroup) override {
    std::lock_guard<std::mutex> lock(mMutex);
    mGroups.push_back(cueGroup);
  }

  [[nodiscard]] bool sawText(const std::string& text) const {
    std::lock_guard<std::mutex> lock(mMutex);
    for (const auto& group : mGroups) {
      for (const auto& cue : group.getCues()) {
        if (cue.text == text) {
          return true;
        }
      }
    }
    return false;
  }

  [[nodiscard]] bool sawEmpty() const {
    std::lock_guard<std::mutex> lock(mMutex);
    for (const auto& group : mGroups) {
      if (group.isEmpty()) {
        return true;
      }
    }
    return false;
  }

private:
  mutable std::mutex mMutex;
  std::vector<AVCueGroup> mGroups;
};

class CollectingListener final : public coral::Player::EventListener {
public:
  void onCues(const AVCueGroup& cueGroup) override {
    std::lock_guard<std::mutex> lock(mMutex);
    for (const auto& cue : cueGroup.getCues()) {
      if (cue.text == "Hello Coral") {
        sawFirst.store(true);
      }
    }
  }

  std::atomic_bool sawFirst{ false };

private:
  mutable std::mutex mMutex;
};

} // namespace

TEST(TextRendererTest, LivePlayerDeliversTheCuesToTheOutputAndTheListeners) {
  const auto path = std::string(CORAL_ASSETS_DIR) + "/subtitle/srt.mkv";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "the subtitle fixture is missing: " << path;
  }
  auto player = coral::LivePlayer::create(path);
  ASSERT_NE(nullptr, player);
  auto output = std::make_shared<CollectingTextOutput>();
  auto listener = std::make_shared<CollectingListener>();
  player->setTextOutput(output);
  player->addListener(listener);

  player->prepare();
  ASSERT_TRUE(waitUntil([&player] {
    return player->getPlaybackState() == coral::Player::State::STATE_READY;
  })) << "the player did not become ready";
  player->play();
  ASSERT_TRUE(waitUntil([&output] { return output->sawText("Hello Coral"); }))
      << "the text output never received the first cue";
  ASSERT_TRUE(waitUntil([&listener] { return listener->sawFirst.load(); }))
      << "the event listener never received the first cue";

  player->seekTo(2400000);
  ASSERT_TRUE(waitUntil([&output] { return output->sawText("Second cue"); }))
      << "the seek did not deliver the cue at the new position";
  EXPECT_TRUE(output->sawEmpty()) << "a seek must clear the screen first";
  player->pause();
  player->release();
}
TEST(TextRendererTest, DeliversTheCuesOfTheSamplesItReads) {
  const auto format = makeSrtFormat();
  auto stream = std::make_shared<FakeSubtitleStream>(format, std::vector<std::vector<uint8_t>>{
    srtDocument() });
  TextRenderer renderer;
  std::vector<AVCueGroup> groups;
  renderer.setCueListener([&groups](const AVCueGroup& group) { groups.push_back(group); });
  enableRenderer(renderer, stream, format);

  renderer.render(0, 0);
  EXPECT_TRUE(groups.empty() || textOf(groups.back()).empty());
  renderer.render(300000, 0);
  ASSERT_FALSE(groups.empty());
  EXPECT_EQ("Hello", textOf(groups.back()));

  const size_t count = groups.size();
  renderer.render(1600000, 0);
  ASSERT_GT(groups.size(), count);
  EXPECT_TRUE(groups.back().isEmpty());
}

TEST(TextRendererTest, DeliversTheEmptyGroupOnAPositionReset) {
  const auto format = makeSrtFormat();
  auto stream = std::make_shared<FakeSubtitleStream>(format, std::vector<std::vector<uint8_t>>{
    srtDocument() });
  TextRenderer renderer;
  std::vector<AVCueGroup> groups;
  renderer.setCueListener([&groups](const AVCueGroup& group) { groups.push_back(group); });
  enableRenderer(renderer, stream, format);
  renderer.render(300000, 0);
  ASSERT_FALSE(groups.empty());

  const size_t count = groups.size();
  renderer.resetPosition(0);
  ASSERT_GT(groups.size(), count);
  EXPECT_TRUE(groups.back().isEmpty());
}

TEST(TextRendererTest, AdvertisesOnlyTheFormatsItParses) {
  TextRenderer renderer;
  EXPECT_EQ(coral::SelectionCapabilities::getFormatSupport(renderer.supportsFormat(makeSrtFormat())),
    coral::SelectionCapabilities::FORMAT_HANDLED);
  auto video = MediaFormat::CreateVideoFormat(MimeTypes::VIDEO_H264, 640, 360);
  EXPECT_EQ(coral::SelectionCapabilities::getFormatSupport(renderer.supportsFormat(video)),
    coral::SelectionCapabilities::FORMAT_UNSUPPORTED_TYPE);
}
