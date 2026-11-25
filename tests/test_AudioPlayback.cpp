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

/**
 * @file test_AudioPlayback.cpp
 *
 * The end-to-end audio path of LivePlayer: the progressive source, the audio decoder and the audio
 * output sink together. The unit suites of the other renderers deliberately do not open an audio
 * device; this suite is registered separately (`test_playback_audio`) because it does.
 *
 * The asset is the same MP4 the video tests use. Its AAC track is five seconds long, and the Darwin
 * decoder returns the whole queued remainder of a track in one buffer. That buffer is larger than the
 * sink's ring, so a renderer that writes it whole rejects it forever and the player never leaves
 * BUFFERING. A position that reaches the end of the track proves the renderer split the buffer and
 * that the sink consumed it.
 */

#include <coral/playback/LivePlayer.h>
#include <coral/playback/Player.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

namespace coral {
namespace {

using namespace std::chrono_literals;

bool waitUntil(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(20ms);
  }
  return predicate();
}

TEST(AudioPlaybackTest, PlaysTheAudioTrackOfAProgressiveMp4) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "the media fixture is missing: " << path;
  }

  // Only the audio renderer: the test does not need a decoder, a surface or a GPU, and the media
  // clock is the one of the audio sink.
  auto player = LivePlayer::create(
      path, LivePlayer::Configuration{.audioEnabled = true,
                                      .videoEnabled = false,
                                      .imageEnabled = false,
                                      .textEnabled = false,
                                      .metadataEnabled = false});
  ASSERT_NE(player, nullptr);

  player->prepare();
  player->play();

  const bool advanced = waitUntil([&player]() {
    return player->getCurrentPositionUs() >= 4'500'000;
  }, 15s);

  EXPECT_TRUE(advanced)
      << "the audio clock did not reach the end of the track: state="
      << static_cast<int>(player->getPlaybackState())
      << " positionUs=" << player->getCurrentPositionUs()
      << " error='" << player->getPlaybackErrorMessage() << "'";

  player->release();
}

TEST(AudioPlaybackTest, SeeksInsideTheAudioTrack) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "the media fixture is missing: " << path;
  }

  auto player = LivePlayer::create(
      path, LivePlayer::Configuration{.audioEnabled = true,
                                      .videoEnabled = false,
                                      .imageEnabled = false,
                                      .textEnabled = false,
                                      .metadataEnabled = false});
  ASSERT_NE(player, nullptr);

  player->prepare();
  player->play();
  ASSERT_TRUE(waitUntil([&player]() { return player->getCurrentPositionUs() >= 500'000; }, 10s))
      << "the audio clock did not start: state=" << static_cast<int>(player->getPlaybackState());

  player->seekTo(2'000'000);
  EXPECT_TRUE(waitUntil([&player]() { return player->getCurrentPositionUs() >= 2'000'000; }, 5s))
      << "the audio clock did not move to the seek position: state="
      << static_cast<int>(player->getPlaybackState())
      << " positionUs=" << player->getCurrentPositionUs();

  player->pause();
  player->release();
}

} // namespace
} // namespace coral
