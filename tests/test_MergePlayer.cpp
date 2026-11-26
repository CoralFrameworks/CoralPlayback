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

#include <coral/playback/LivePlayer.h>
#include <coral/playback/TextOutput.h>
#include <coral/source/MediaItem.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace coral {
namespace {

template <typename Predicate>
bool waitFor(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

/**
 * Creates a player for the video and subtitle paths.
 *
 * <p>These tests assert what the video renderer and the text renderer deliver; the audio renderer and
 * the host's output device are deliberately not part of them, so the suite runs the same on a machine
 * with no audio device (a build machine, for example).
 */
std::unique_ptr<LivePlayer> createPlayer(const std::shared_ptr<MediaItem>& item) {
  return LivePlayer::create(item, LivePlayer::Configuration{.audioEnabled = false});
}

class Sink final : public TextOutput {
public:
  void onCues(const AVCueGroup& group) override {
    std::lock_guard<std::mutex> lock(mutex);
    lastCount = static_cast<int>(group.getCues().size());
    for (const auto& cue: group.getCues()) {
      if (cue.isBitmap()) {
        bitmapWidth = cue.bitmap.width;
        bitmapHeight = cue.bitmap.height;
      }
      if (!cue.text.empty()) text = cue.text;
    }
    condition.notify_all();
  }

  bool waitForText(const std::string& expected, std::string& out) {
    std::unique_lock<std::mutex> lock(mutex);
    const bool ok = condition.wait_for(lock, std::chrono::seconds(8), [this, &expected] {
      return text.find(expected) != std::string::npos;
    });
    out = text;
    return ok;
  }

  int lastCount = 0;
  int bitmapWidth = 0;
  int bitmapHeight = 0;

  bool hasText() {
    std::lock_guard<std::mutex> lock(mutex);
    return !text.empty();
  }

private:
  std::mutex mutex;
  std::condition_variable condition;
  std::string text;
};

TEST(MergePlayer, SelectsTheSideLoadedSubtitleByLanguage) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  const std::string subtitle = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample.srt";
  std::vector<MediaItem::SubtitleConfiguration> subtitles;
  subtitles.emplace_back(subtitle, "application/x-subrip", "eng", "English", "7");
  auto player = createPlayer(MediaItem::fromUrl(video, std::move(subtitles)));
  ASSERT_NE(nullptr, player);
  auto sink = std::make_shared<Sink>();
  player->setPreferredTextLanguage("eng");
  player->setTextOutput(sink);
  player->prepare();
  ASSERT_TRUE(waitFor([&player] { return player->getBufferedPositionUs() >= 1600000; }))
      << "the media was not buffered past the first cue";
  player->play();
  std::string text;
  EXPECT_TRUE(sink->waitForText("Hello Coral", text)) << "cue not delivered: " << text;
  EXPECT_EQ(1, sink->lastCount) << "the subtitle cue was delivered more than once";
  player->pause();
  player->release();
}

TEST(MergePlayer, SelectsTheRequestedLanguageAmongSeveralSubtitles) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  const std::string english = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample.srt";
  const std::string chinese = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample_zho.srt";
  std::vector<MediaItem::SubtitleConfiguration> subtitles;
  subtitles.emplace_back(english, "application/x-subrip", "eng", "English", "eng");
  subtitles.emplace_back(chinese, "application/x-subrip", "zho", "Chinese", "zho");
  auto player = createPlayer(MediaItem::fromUrl(video, std::move(subtitles)));
  ASSERT_NE(nullptr, player);
  auto sink = std::make_shared<Sink>();
  player->setPreferredTextLanguage("zho");
  player->setTextOutput(sink);
  player->prepare();
  ASSERT_TRUE(waitFor([&player] { return player->getBufferedPositionUs() >= 1600000; }))
      << "the media was not buffered past the first cue";
  player->play();
  std::string text;
  EXPECT_TRUE(sink->waitForText("Zho Coral", text)) << "cue not delivered: " << text;
  EXPECT_EQ(1, sink->lastCount) << "the subtitle cue was delivered more than once";
  player->pause();
  player->release();
}

} // namespace
} // namespace coral
namespace coral {
namespace {

TEST(MergePlayer, PlaysTheEmbeddedSubtitleOfAMatroska) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/subtitle/srt.mkv";
  if (!std::filesystem::exists(video)) {
    GTEST_SKIP() << "the fixture is missing: " << video;
  }
  auto player = createPlayer(MediaItem::fromUrl(video));
  ASSERT_NE(nullptr, player);
  auto sink = std::make_shared<Sink>();
  player->setPreferredTextLanguage("eng");
  player->setTextOutput(sink);
  player->prepare();
  ASSERT_TRUE(waitFor([&player] { return player->getBufferedPositionUs() >= 1600000; }))
      << "the media was not buffered past the first cue";
  player->play();
  std::string text;
  EXPECT_TRUE(sink->waitForText("Hello Coral", text)) << "embedded cue not delivered: " << text;
  player->pause();
  player->release();
}

} // namespace
} // namespace coral
namespace coral {
namespace {

TEST(MergePlayer, PlaysTheEmbeddedAssSubtitleOfAMatroska) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/subtitle/ass.mkv";
  if (!std::filesystem::exists(video)) {
    GTEST_SKIP() << "the fixture is missing: " << video;
  }
  auto player = createPlayer(MediaItem::fromUrl(video));
  ASSERT_NE(nullptr, player);
  auto sink = std::make_shared<Sink>();
  player->setPreferredTextLanguage("eng");
  player->setTextOutput(sink);
  player->prepare();
  ASSERT_TRUE(waitFor([&player] { return player->getBufferedPositionUs() >= 1600000; }))
      << "the media was not buffered past the first cue";
  player->play();
  std::string text;
  EXPECT_TRUE(sink->waitForText("Hello Coral", text)) << "embedded ASS cue not delivered: " << text;
  player->pause();
  player->release();
}

} // namespace
} // namespace coral
namespace coral {
namespace {

TEST(MergePlayer, TogglesSubtitleRenderingAtRuntime) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  const std::string subtitle = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample.srt";
  std::vector<MediaItem::SubtitleConfiguration> subtitles;
  subtitles.emplace_back(subtitle, "application/x-subrip", "eng", "English", "toggle");
  auto player = createPlayer(MediaItem::fromUrl(video, std::move(subtitles)));
  ASSERT_NE(nullptr, player);
  auto sink = std::make_shared<Sink>();
  player->setPreferredTextLanguage("eng");
  player->setSubtitleEnabled(false);
  player->setTextOutput(sink);
  player->prepare();
  player->play();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_FALSE(sink->hasText()) << "a disabled subtitle renderer delivered a cue";

  player->seekTo(0);
  player->setSubtitleEnabled(true);
  std::string text;
  EXPECT_TRUE(sink->waitForText("Hello Coral", text)) << "cue not delivered after enabling: " << text;
  player->pause();
  player->release();
}

} // namespace
} // namespace coral
namespace coral {
namespace {

TEST(MergePlayer, SwitchesTheSubtitleLanguageAtRuntime) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  const std::string english = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample.srt";
  const std::string chinese = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample_zho.srt";
  std::vector<MediaItem::SubtitleConfiguration> subtitles;
  subtitles.emplace_back(english, "application/x-subrip", "eng", "English", "eng");
  subtitles.emplace_back(chinese, "application/x-subrip", "zho", "Chinese", "zho");
  auto player = createPlayer(MediaItem::fromUrl(video, std::move(subtitles)));
  ASSERT_NE(nullptr, player);
  auto sink = std::make_shared<Sink>();
  player->setPreferredTextLanguage("eng");
  player->setTextOutput(sink);
  player->prepare();
  ASSERT_TRUE(waitFor([&player] { return player->getBufferedPositionUs() >= 1600000; }))
      << "the media was not buffered past the first cue";
  player->play();
  std::string text;
  ASSERT_TRUE(sink->waitForText("Hello Coral", text)) << "English cue not delivered: " << text;

  player->setPreferredTextLanguage("zho");
  player->seekTo(0);
  if (!sink->waitForText("Zho Coral", text)) {
    // The seek can land before the new track has been selected, so the switch is verified once
    // more from the settled state.
    player->seekTo(0);
    EXPECT_TRUE(sink->waitForText("Zho Coral", text)) << "Chinese cue not delivered: " << text;
  }
  player->pause();
  player->release();
}

} // namespace
} // namespace coral
namespace coral {
namespace {

TEST(MergePlayer, PlaysTheEmbeddedPgsSubtitleAsABitmap) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/subtitle/pgs.mkv";
  if (!std::filesystem::exists(video)) {
    GTEST_SKIP() << "the fixture is missing: " << video;
  }
  auto player = createPlayer(MediaItem::fromUrl(video));
  ASSERT_NE(nullptr, player);
  auto sink = std::make_shared<Sink>();
  player->setPreferredTextLanguage("eng");
  player->setTextOutput(sink);
  player->prepare();
  ASSERT_TRUE(waitFor([&player] { return player->getBufferedPositionUs() >= 1600000; }))
      << "the media was not buffered past the first cue";
  player->play();
  ASSERT_TRUE(waitFor([&sink] { return sink->bitmapWidth > 0; }))
      << "the PGS bitmap cue was not delivered";
  EXPECT_EQ(320, sink->bitmapWidth);
  EXPECT_EQ(180, sink->bitmapHeight);
  player->pause();
  player->release();
}

} // namespace
} // namespace coral