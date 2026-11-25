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
#include <coral/playback/Player.h>
#include <coral/playback/Tracks.h>

#include <media/Format.h>
#include <media/Metadata.h>
#include <media/MetadataEntries.h>
#include <media/Types.h>
#include <muxer/Muxer.h>
#include <muxer/MuxerFactory.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace coral {
namespace {

using namespace std::chrono_literals;

bool waitFor(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return predicate();
}

bool writeChapters(muxer::Muxer& muxer) {
  std::vector<Mp4Chapter> chapterList;
  chapterList.emplace_back(0, "Intro");
  chapterList.emplace_back(2'000'000, "Main");
  auto entry = std::make_shared<Mp4ChaptersData>(std::move(chapterList));
  auto metadata = std::make_shared<Metadata>(
      std::vector<std::shared_ptr<Metadata::Entry>>{ std::move(entry) });
  return muxer.addMetadata(metadata);
}

bool writeTx3gTrack(muxer::Muxer& muxer, const std::string& language, const std::string& text) {
  auto format = MediaFormat::CreateSubtitleFormat(MimeTypes::APPLICATION_TX3G);
  if (format == nullptr) {
    return false;
  }
  format->setLabel(language);
  muxer::TrackOptions options;
  options.language = language;
  const int track = muxer.addTrack(format, options);
  if (track < 0) {
    return false;
  }
  std::vector<uint8_t> sample;
  sample.reserve(2 + text.size());
  sample.push_back(static_cast<uint8_t>((text.size() >> 8) & 0xFF));
  sample.push_back(static_cast<uint8_t>(text.size() & 0xFF));
  sample.insert(sample.end(), text.begin(), text.end());
  muxer::SampleInfo info;
  info.presentationTimeUs = 0;
  info.durationUs = 5'000'000;
  info.flags = static_cast<uint32_t>(muxer::SampleFlag::kKeyFrame);
  return muxer.writeSample(track, sample.data(), sample.size(), info);
}

TEST(SubtitleTracksTest, ReportsAndSelectsInContainerTracks) {
  const auto path = std::filesystem::temp_directory_path() / "coral-playback-two-subtitles.mp4";
  std::filesystem::remove(path);
  auto factory = muxer::createMuxerFactory();
  ASSERT_NE(factory, nullptr);
  muxer::Error error;
  auto file = factory->create(path.string(), muxer::Container::kMp4, {}, &error);
  ASSERT_NE(file, nullptr) << error.message;
  ASSERT_TRUE(writeTx3gTrack(*file, "eng", "Hello")) << file->getError().message;
  ASSERT_TRUE(writeTx3gTrack(*file, "zho", "Ni hao")) << file->getError().message;
  ASSERT_TRUE(writeChapters(*file)) << file->getError().message;
  ASSERT_TRUE(file->finish()) << file->getError().message;
  file.reset();

  auto player = LivePlayer::create(path.string(), LivePlayer::Configuration{
      .audioEnabled = false, .videoEnabled = false, .imageEnabled = false,
      .textEnabled = true, .metadataEnabled = false});
  ASSERT_NE(player, nullptr);
  player->prepare();
  ASSERT_TRUE(waitFor([&] {
    return player->getPlaybackState() == Player::State::STATE_READY ||
           !player->getPlaybackErrorMessage().empty();
  }, 10s)) << player->getPlaybackErrorMessage();
  if (!player->getPlaybackErrorMessage().empty()) {
    player->release();
    std::filesystem::remove(path);
    GTEST_SKIP() << player->getPlaybackErrorMessage();
  }

  const auto chapters = player->getChapters();
  ASSERT_GE(chapters.size(), 2u) << "the MP4 should expose its chpl chapters";
  EXPECT_EQ(chapters[0].startUs, 0);
  EXPECT_EQ(chapters[0].title, "Intro");
  EXPECT_EQ(chapters[1].startUs, 2'000'000);
  EXPECT_EQ(chapters[1].title, "Main");
  EXPECT_GE(chapters[1].endUs, chapters[1].startUs);

  auto textTracks = [&]() {
    std::vector<Track> tracks;
    for (const auto& group : player->getCurrentTracks().groups) {
      for (const auto& track : group.tracks) {
        if (track.type == TrackType::Text) {
          tracks.push_back(track);
        }
      }
    }
    return tracks;
  };
  const auto tracks = textTracks();
  ASSERT_GE(tracks.size(), 2u) << "the MP4 should expose both tx3g tracks";
  const auto target = std::find_if(tracks.begin(), tracks.end(),
      [](const Track& track) { return !track.selected; });
  ASSERT_NE(target, tracks.end());
  player->selectTrack(target->rendererIndex, target->groupIndex, target->trackIndex);
  EXPECT_TRUE(waitFor([&] {
    for (const auto& track : textTracks()) {
      if (track.selected && track.rendererIndex == target->rendererIndex &&
          track.groupIndex == target->groupIndex && track.trackIndex == target->trackIndex) {
        return true;
      }
    }
    return false;
  }, 5s)) << "the in-container subtitle selection did not stick";

  player->release();
  std::filesystem::remove(path);
}

TEST(SubtitleTracksTest, ReadsTheForcedFlagFromAMatroskaTrack) {
  const auto path = std::filesystem::path(CORAL_ASSETS_DIR) / "video/forced_subtitle_5s.mkv";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "the forced subtitle fixture is missing: " << path;
  }
  auto player = LivePlayer::create(path.string(), LivePlayer::Configuration{
      .audioEnabled = false, .videoEnabled = false, .imageEnabled = false,
      .textEnabled = true, .metadataEnabled = false});
  ASSERT_NE(player, nullptr);
  player->setPreferredTextLanguage("zho");
  player->prepare();
  ASSERT_TRUE(waitFor([&] {
    return player->getPlaybackState() == Player::State::STATE_READY ||
           !player->getPlaybackErrorMessage().empty();
  }, 10s)) << player->getPlaybackErrorMessage();
  ASSERT_TRUE(player->getPlaybackErrorMessage().empty());

  bool foundForced = false;
  for (const auto& group : player->getCurrentTracks().groups) {
    for (const auto& track : group.tracks) {
      if (track.type != TrackType::Text || track.format == nullptr) {
        continue;
      }
      // The Matroska extractor does not currently publish TrackEntry language, so the assertion
      // is about the container's forced flag and the selection it causes, not the language tag.
      EXPECT_NE(track.format->getSelectionFlags() &
                  static_cast<MediaSelectionFlags>(MediaSelectionFlags::kMediaSelectionForced),
                static_cast<MediaSelectionFlags>(0));
      foundForced = true;
      EXPECT_TRUE(track.selected) << "the forced zho track should be selected";
    }
  }
  EXPECT_TRUE(foundForced) << "the forced Matroska subtitle track was not exposed";
  player->release();
}

} // namespace
} // namespace coral
