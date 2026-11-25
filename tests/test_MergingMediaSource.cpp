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

#include <coral/selection/FixedTrackSelection.h>
#include <coral/source/FormatHolder.h>
#include <coral/source/MediaItem.h>
#include <coral/source/MediaPeriod.h>
#include <coral/source/MergingMediaSource.h>
#include <coral/source/MergingMediaSourceFactory.h>
#include <coral/source/ProgressiveMediaSource.h>
#include <coral/source/SampleStream.h>
#include <coral/upstream/DefaultAllocator.h>

#include <media/PacketBuffer.h>
#include <media/Types.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace coral {
namespace {

class RecordingCaller final : public MediaSourceCaller {
public:
  void onSourceInfoRefreshed(const std::shared_ptr<MediaSource>&,
      const std::shared_ptr<Timeline>&) override {
    std::lock_guard<std::mutex> lock(mutex);
    refreshed = true;
    condition.notify_all();
  }

  bool wait() {
    std::unique_lock<std::mutex> lock(mutex);
    return condition.wait_for(lock, std::chrono::seconds(10), [this] { return refreshed; });
  }

private:
  std::mutex mutex;
  std::condition_variable condition;
  bool refreshed = false;
};

class PeriodCallback final : public MediaPeriod::Callback {
public:
  void onPrepared(const std::shared_ptr<MediaPeriod>&) override {
    std::lock_guard<std::mutex> lock(mutex);
    prepared = true;
    condition.notify_all();
  }

  void onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>&) override {}

  bool wait() {
    std::unique_lock<std::mutex> lock(mutex);
    return condition.wait_for(lock, std::chrono::seconds(10), [this] { return prepared; });
  }

private:
  std::mutex mutex;
  std::condition_variable condition;
  bool prepared = false;
};

std::shared_ptr<MergingMediaSource> makeSource(std::vector<SubtitleMetadata> metadata = {}) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  const std::string subtitle = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample.srt";
  auto videoSource = std::make_shared<ProgressiveMediaSource>(MediaItem::fromUrl(video));
  auto subtitleSource = std::make_shared<ProgressiveMediaSource>(MediaItem::fromUrl(subtitle));
  if (metadata.empty()) {
    metadata = {SubtitleMetadata{}, SubtitleMetadata{}};
  }
  return std::make_shared<MergingMediaSource>(
      std::vector<std::shared_ptr<MediaSource>>{videoSource, subtitleSource}, std::move(metadata));
}

TEST(MergingMediaSource, ExposesTheSubtitleTrackOfASideLoadedFile) {
  SubtitleMetadata metadata;
  metadata.present = true;
  metadata.mimeType = "application/x-subrip";
  metadata.language = "eng";
  metadata.label = "English";
  metadata.id = "42";
  auto source = makeSource({SubtitleMetadata{}, metadata});
  auto caller = std::make_shared<RecordingCaller>();
  source->prepareSource(caller);
  ASSERT_TRUE(caller->wait());
  source->enable(caller);

  auto allocator = std::make_shared<DefaultAllocator>(true, 4 * 1024 * 1024);
  auto period = source->createPeriod(MediaPeriodId(PeriodUid("merged")), allocator, 0);
  auto callback = std::make_shared<PeriodCallback>();
  period->prepare(callback.get(), 0);
  ASSERT_TRUE(callback->wait());
  ASSERT_NE(nullptr, period->getTrackGroups());

  bool hasSubtitle = false;
  for (int i = 0; i < period->getTrackGroups()->length(); ++i) {
    const auto& group = period->getTrackGroups()->get(i);
    if (group == nullptr || group->mediaType() != MediaType::kMediaTypeSubtitle) {
      continue;
    }
    for (int j = 0; j < group->length(); ++j) {
      const auto& format = group->getFormat(j);
      if (format->getMimeType() == "application/x-subrip") {
        hasSubtitle = true;
        EXPECT_EQ("eng", format->getLanguage());
        EXPECT_EQ("English", format->getLabel());
        EXPECT_EQ(42, format->getId());
      }
    }
  }
  EXPECT_TRUE(hasSubtitle) << "the merged period should expose the side-loaded subtitle group";
}

TEST(MergingMediaSource, ReadsTheSubtitleSampleStream) {
  auto source = makeSource();
  auto caller = std::make_shared<RecordingCaller>();
  source->prepareSource(caller);
  ASSERT_TRUE(caller->wait());
  source->enable(caller);

  auto allocator = std::make_shared<DefaultAllocator>(true, 4 * 1024 * 1024);
  auto period = source->createPeriod(MediaPeriodId(PeriodUid("read")), allocator, 0);
  auto callback = std::make_shared<PeriodCallback>();
  period->prepare(callback.get(), 0);
  ASSERT_TRUE(callback->wait());

  std::shared_ptr<TrackGroup> subtitleGroup;
  for (int i = 0; i < period->getTrackGroups()->length(); ++i) {
    const auto& group = period->getTrackGroups()->get(i);
    if (group->mediaType() == MediaType::kMediaTypeSubtitle) subtitleGroup = group;
  }
  ASSERT_NE(nullptr, subtitleGroup);

  auto selection = std::make_shared<FixedTrackSelection>(subtitleGroup, 0);
  std::vector<std::shared_ptr<TrackSelection>> selections{selection};
  std::vector<bool> retain{false};
  std::vector<std::shared_ptr<SampleStream>> streams{nullptr};
  std::vector<bool> reset{false};
  period->selectTracks(selections, retain, streams, reset, 0);
  ASSERT_NE(nullptr, streams[0]);

  bool sawData = false;
  for (int i = 0; i < 100 && !sawData; ++i) {
    period->continueLoading(0);
    FormatHolder format;
    auto packet = std::make_shared<AVPacketBuffer>(MediaType::kMediaTypeSubtitle);
    using ReadResult = decltype(streams[0]->readData(format, packet.get(), false));
    const auto result = streams[0]->readData(format, packet.get(), false);
    if (result == ReadResult::kResultBufferRead) sawData = packet->size() > 0;
    packet->release();
  }
  EXPECT_TRUE(sawData) << "the subtitle sample stream produced no data";
}

} // namespace
} // namespace coral

namespace coral {
namespace {

TEST(MergingMediaSourceFactory, CreatesTheMergedSource) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  const std::string subtitle = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample.srt";
  std::vector<MediaItem::SubtitleConfiguration> subtitles;
  subtitles.emplace_back(subtitle, "application/x-subrip", "eng", "English", "7");
  auto item = MediaItem::fromUrl(video, std::move(subtitles));
  MergingMediaSourceFactory factory;
  auto source = factory.createMediaSource(item);
  ASSERT_NE(nullptr, source);
}
} // namespace
} // namespace coral
