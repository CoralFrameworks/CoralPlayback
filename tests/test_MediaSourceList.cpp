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

#include <TestMedia.h>

#include <coral/selection/FixedTrackSelection.h>
#include <coral/source/MediaItem.h>
#include <coral/source/MediaPeriod.h>
#include <coral/source/ProgressiveMediaSource.h>
#include <coral/source/ShuffleOrder.h>
#include <coral/upstream/DefaultAllocator.h>

// The playlist the player drives is an internal type of the library, see the CMakeLists of this
// target for the include path that exposes it.
#include "details/MediaSourceList.h"

#include <datasource/DataSource.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace coral {
namespace {

using namespace std::chrono_literals;

constexpr int kSampleRate = 8000;
constexpr int kChannelCount = 1;
constexpr int kSampleCount = 20000;
constexpr auto kTimeout = 10000ms;
/** The number of channels the playlist is supposed to report for the events of a child source. */
constexpr int kChildWindowIndex = 0;

/**
 * Reads the media from memory, so that the test needs no file.
 */
class InMemoryDataSourceFactory {
public:
  explicit InMemoryDataSourceFactory(std::vector<uint8_t> media) noexcept
      : media_(std::move(media)) {
  }

  std::shared_ptr<AVDataSource> operator()(const std::string& url) const {
    static_cast<void>(url);
    return std::make_shared<AVByteArrayDataSource>(media_);
  }

private:
  std::vector<uint8_t> media_;
};

/**
 * Records the load events that reach the playlist.
 */
class RecordingEventListener final : public MediaSourceEventListener {
public:
  void onLoadStarted(const int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++loadStartedCount_;
    lastWindowIndex_ = windowIndex;
    lastMediaPeriodId_ = mediaPeriodId;
    lastUrl_ = loadEventInfo.url;
    static_cast<void>(mediaLoadData);
    cv_.notify_all();
  }

  void onLoadCompleted(const int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++loadCompletedCount_;
    lastWindowIndex_ = windowIndex;
    lastMediaPeriodId_ = mediaPeriodId;
    static_cast<void>(loadEventInfo);
    static_cast<void>(mediaLoadData);
    cv_.notify_all();
  }

  void onMediaPeriodCreated(const int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++mediaPeriodCreatedCount_;
    lastMediaPeriodCreatedWindowIndex_ = windowIndex;
    lastCreatedMediaPeriodId_ = mediaPeriodId;
    cv_.notify_all();
  }

  void onMediaPeriodReleased(const int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++mediaPeriodReleasedCount_;
    lastMediaPeriodReleasedWindowIndex_ = windowIndex;
    lastReleasedMediaPeriodId_ = mediaPeriodId;
    cv_.notify_all();
  }

  void onDownstreamFormatChanged(const int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const MediaLoadData& mediaLoadData) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++downstreamFormatCount_;
    lastWindowIndex_ = windowIndex;
    lastMediaPeriodId_ = mediaPeriodId;
    lastTrackFormat_ = mediaLoadData.trackFormat;
    cv_.notify_all();
  }

  bool waitFor(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, predicate);
  }

  [[nodiscard]] int loadStartedCount() const { return loadStartedCount_.load(); }

  [[nodiscard]] int loadCompletedCount() const { return loadCompletedCount_.load(); }

  [[nodiscard]] int downstreamFormatCount() const { return downstreamFormatCount_.load(); }

  [[nodiscard]] int mediaPeriodCreatedCount() const { return mediaPeriodCreatedCount_.load(); }

  [[nodiscard]] int mediaPeriodReleasedCount() const { return mediaPeriodReleasedCount_.load(); }

  [[nodiscard]] int lastMediaPeriodCreatedWindowIndex() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastMediaPeriodCreatedWindowIndex_;
  }

  [[nodiscard]] int lastMediaPeriodReleasedWindowIndex() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastMediaPeriodReleasedWindowIndex_;
  }

  [[nodiscard]] std::optional<MediaPeriodId> lastCreatedMediaPeriodId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastCreatedMediaPeriodId_;
  }

  [[nodiscard]] std::optional<MediaPeriodId> lastReleasedMediaPeriodId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastReleasedMediaPeriodId_;
  }

  [[nodiscard]] int lastWindowIndex() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastWindowIndex_;
  }

  [[nodiscard]] std::optional<MediaPeriodId> lastMediaPeriodId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastMediaPeriodId_;
  }

  [[nodiscard]] std::string lastUrl() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastUrl_;
  }

  [[nodiscard]] std::shared_ptr<MediaFormat> lastTrackFormat() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastTrackFormat_;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<int> loadStartedCount_{ 0 };
  std::atomic<int> loadCompletedCount_{ 0 };
  std::atomic<int> downstreamFormatCount_{ 0 };
  std::atomic<int> mediaPeriodCreatedCount_{ 0 };
  std::atomic<int> mediaPeriodReleasedCount_{ 0 };
  int lastMediaPeriodCreatedWindowIndex_ = Definitions::kIndexUnset;
  int lastMediaPeriodReleasedWindowIndex_ = Definitions::kIndexUnset;
  std::optional<MediaPeriodId> lastCreatedMediaPeriodId_;
  std::optional<MediaPeriodId> lastReleasedMediaPeriodId_;
  int lastWindowIndex_ = Definitions::kIndexUnset;
  std::optional<MediaPeriodId> lastMediaPeriodId_;
  std::string lastUrl_;
  std::shared_ptr<MediaFormat> lastTrackFormat_;
};

/**
 * Records the events of a media period.
 */
class RecordingPeriodCallback final : public MediaPeriod::Callback {
public:
  void onPrepared(const std::shared_ptr<MediaPeriod>& mediaPeriod) override {
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(mediaPeriod);
    prepared = true;
    cv_.notify_all();
  }

  void onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& mediaPeriod) override {
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(mediaPeriod);
    ++continueLoadingRequestCount_;
    cv_.notify_all();
  }

  bool waitForPrepared(const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return prepared; });
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool prepared = false;
  int continueLoadingRequestCount_ = 0;
};

/**
 * Records the timeline updates the playlist asks for.
 */
class RecordingInfoRefreshListener final : public MediaSourceListInfoRefreshListener {
public:
  void onPlaylistUpdateRequested() override {
    ++updateCount;
  }

  std::atomic<int> updateCount{ 0 };
};

/**
 * Selects the only track of a period, which is what the player does before it loads the media: a
 * period that has no enabled track does not load anything, so the events of its load only arrive
 * once a track was selected.
 */
void selectOnlyTrack(MediaPeriod& period) {
  const auto& groups = period.getTrackGroups();
  std::vector<std::shared_ptr<TrackSelection>> selections(1);
  if (groups->length() > 0) {
    selections[0] = std::make_shared<FixedTrackSelection>(groups->get(0), 0);
  }
  std::vector<bool> mayRetainStreamFlags(1, false);
  std::vector<std::shared_ptr<SampleStream>> streams(1);
  std::vector<bool> streamResetFlags(1);
  period.selectTracks(selections, mayRetainStreamFlags, streams, streamResetFlags, 0);
}

// ==================================================================================================

TEST(MediaSourceListTest, RemapsTheEventsOfAChildSourceToThePlaylist) {
  RecordingInfoRefreshListener infoListener;
  MediaSourceList list(&infoListener);

  const auto item = MediaItem::fromUrl("memory://test.wav");
  auto source = std::make_shared<ProgressiveMediaSource>(
      item, InMemoryDataSourceFactory(test::buildWav(kSampleRate, kChannelCount, kSampleCount)));
  std::vector<std::shared_ptr<MediaSourceHolder>> holders{
    std::make_shared<MediaSourceHolder>(source, /* useLazyPreparation= */ false)
  };
  const auto shuffleOrder = std::make_shared<DefaultShuffleOrder>(std::vector<int>{ 0 });
  list.setMediaSources(holders, shuffleOrder);
  list.prepare();

  // The listener is registered once the child source is already prepared, which is what an
  // application does when it registers on a player that is playing.
  auto listener = std::make_shared<RecordingEventListener>();
  list.addEventListener(nullptr, listener);

  // The period is created with the identifier the playlist timeline reports, which is the one the
  // events have to carry.
  const auto playlistTimeline = list.createTimeline();
  ASSERT_EQ(playlistTimeline->getPeriodCount(), 1);
  const auto periodUid = playlistTimeline->getUidOfPeriod(0);
  ASSERT_TRUE(periodUid.has_value());

  const auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, 256 * 1024);
  auto period = list.createPeriod(MediaPeriodId(periodUid.value()), allocator, 0);
  ASSERT_NE(period, nullptr);

  RecordingPeriodCallback callback;
  period->prepare(&callback, 0);
  ASSERT_TRUE(callback.waitForPrepared(kTimeout)) << "the period of the playlist was not prepared";
  selectOnlyTrack(*period);

  ASSERT_TRUE(listener->waitFor([&] { return listener->loadStartedCount() > 0; }, kTimeout))
      << "the load started event did not reach the playlist";
  // The child source reports an unset window index, which the playlist resolves to the index of the
  // first window the child owns.
  EXPECT_EQ(listener->lastWindowIndex(), kChildWindowIndex);
  EXPECT_EQ(listener->lastUrl(), "memory://test.wav");

  // The period identifier is the one of the playlist timeline, not the one of the child source.
  ASSERT_TRUE(listener->lastMediaPeriodId().has_value());
  EXPECT_EQ(listener->lastMediaPeriodId()->periodUid(), periodUid.value());
  EXPECT_NE(playlistTimeline->getIndexOfPeriod(listener->lastMediaPeriodId()->periodUid()),
      Definitions::kIndexUnset);

  // Drive the load to its end, so that the completion of the load is reported too.
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (period->getBufferedPositionUs() == Definitions::kTimeEndOfSource) {
      break;
    }
    period->continueLoading(0);
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_EQ(period->getBufferedPositionUs(), Definitions::kTimeEndOfSource)
      << "the period of the playlist did not finish loading";
  ASSERT_TRUE(listener->waitFor([&] { return listener->loadCompletedCount() > 0; }, kTimeout))
      << "the load completed event did not reach the playlist";

  list.releasePeriod(period);
  list.release();
}

TEST(MediaSourceListTest, ReportsTheEventsOfEveryWindowOfThePlaylist) {
  RecordingInfoRefreshListener infoListener;
  MediaSourceList list(&infoListener);

  const auto media = test::buildWav(kSampleRate, kChannelCount, kSampleCount);
  std::vector<std::shared_ptr<MediaSourceHolder>> holders{
    std::make_shared<MediaSourceHolder>(
        std::make_shared<ProgressiveMediaSource>(
            MediaItem::fromUrl("memory://first.wav"), InMemoryDataSourceFactory(media)),
        /* useLazyPreparation= */ false),
    std::make_shared<MediaSourceHolder>(
        std::make_shared<ProgressiveMediaSource>(
            MediaItem::fromUrl("memory://second.wav"), InMemoryDataSourceFactory(media)),
        /* useLazyPreparation= */ false)
  };
  // An order that was not shuffled keeps the order of the list, so the second source owns the second
  // window of the playlist, which is what the events have to report.
  const auto shuffleOrder = std::make_shared<DefaultShuffleOrder>(std::vector<int>{ 0, 1 });
  auto timeline = list.setMediaSources(holders, shuffleOrder);
  ASSERT_EQ(timeline->getWindowCount(), 2);
  ASSERT_EQ(timeline->getPeriodCount(), 2);
  list.prepare();

  auto listener = std::make_shared<RecordingEventListener>();
  list.addEventListener(nullptr, listener);

  // Only the second window is played, so its events are the ones that have to arrive.
  const auto periodUid = timeline->getUidOfPeriod(1);
  ASSERT_TRUE(periodUid.has_value());
  const auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, 256 * 1024);
  auto period = list.createPeriod(MediaPeriodId(periodUid.value()), allocator, 0);
  ASSERT_NE(period, nullptr);

  RecordingPeriodCallback callback;
  period->prepare(&callback, 0);
  ASSERT_TRUE(callback.waitForPrepared(kTimeout));
  selectOnlyTrack(*period);

  ASSERT_TRUE(listener->waitFor([&] { return listener->loadStartedCount() > 0; }, kTimeout));
  // The second child source owns the second window of the playlist.
  EXPECT_EQ(listener->lastWindowIndex(), 1);
  EXPECT_EQ(listener->lastUrl(), "memory://second.wav");

  list.releasePeriod(period);
  list.release();
}

/**
 * Builds holders that each own one window, so that the playlist has one window per holder.
 */
std::vector<std::shared_ptr<MediaSourceHolder>> buildHolders(
    const std::vector<std::string>& urls, const std::vector<uint8_t>& media) {
  auto holders = std::vector<std::shared_ptr<MediaSourceHolder>>();
  for (const auto& url : urls) {
    holders.push_back(std::make_shared<MediaSourceHolder>(
        std::make_shared<ProgressiveMediaSource>(MediaItem::fromUrl(url),
            InMemoryDataSourceFactory(media)),
        /* useLazyPreparation= */ false));
  }
  return holders;
}

/** Returns the url of the media item of every window of a timeline, in playlist order. */
std::vector<std::string> windowUrls(const std::shared_ptr<Timeline>& timeline) {
  auto urls = std::vector<std::string>();
  for (auto i = 0; i < timeline->getWindowCount(); ++i) {
    const auto window = timeline->getWindow(i, std::make_shared<Timeline::Window>(), 0);
    urls.push_back(window->mediaItem != nullptr ? window->mediaItem->getUrl() : std::string());
  }
  return urls;
}

TEST(MediaSourceListTest, AddsMediaSourcesInTheMiddleOfThePlaylist) {
  RecordingInfoRefreshListener infoListener;
  MediaSourceList list(&infoListener);
  const auto media = test::buildWav(kSampleRate, kChannelCount, kSampleCount);

  const auto twoHolders = buildHolders({"memory://first.wav", "memory://third.wav"}, media);
  auto timeline = list.setMediaSources(
      twoHolders, std::make_shared<DefaultShuffleOrder>(std::vector<int>{ 0, 1 }));
  ASSERT_EQ(windowUrls(timeline), (std::vector<std::string>{"memory://first.wav",
    "memory://third.wav"}));

  // Inserting in the middle keeps the order of the playlist and shifts the windows before which the
  // new source owns its window.
  const auto added = buildHolders({"memory://second.wav"}, media);
  auto order = std::make_shared<DefaultShuffleOrder>(std::vector<int>{ 0, 1 })->cloneAndInsert(1, 1);
  timeline = list.addMediaSources(1, added, order);
  EXPECT_EQ(windowUrls(timeline), (std::vector<std::string>{"memory://first.wav",
    "memory://second.wav", "memory://third.wav"}));
  EXPECT_EQ(timeline->getWindowCount(), 3);
  EXPECT_EQ(timeline->getPeriodCount(), 3);
  list.release();
}

TEST(MediaSourceListTest, RemovesMediaSourcesAndKeepsTheRestContiguous) {
  RecordingInfoRefreshListener infoListener;
  MediaSourceList list(&infoListener);
  const auto media = test::buildWav(kSampleRate, kChannelCount, kSampleCount);

  const auto holders = buildHolders(
      {"memory://first.wav", "memory://second.wav", "memory://third.wav"}, media);
  std::shared_ptr<ShuffleOrder> order =
      std::make_shared<DefaultShuffleOrder>(std::vector<int>{ 0, 1, 2 });
  auto timeline = list.setMediaSources(holders, order);
  ASSERT_EQ(timeline->getWindowCount(), 3);

  order = order->cloneAndRemove(1, 2);
  timeline = list.removeMediaSources(1, 2, order);
  EXPECT_EQ(windowUrls(timeline), (std::vector<std::string>{"memory://first.wav",
    "memory://third.wav"}));
  EXPECT_EQ(timeline->getWindowCount(), 2);
  list.release();
}

TEST(MediaSourceListTest, MovesMediaSourcesAndKeepsTheWindowsConsistent) {
  RecordingInfoRefreshListener infoListener;
  MediaSourceList list(&infoListener);
  const auto media = test::buildWav(kSampleRate, kChannelCount, kSampleCount);

  const auto holders = buildHolders(
      {"memory://first.wav", "memory://second.wav", "memory://third.wav"}, media);
  // An order that was not shuffled keeps the order of the list.
  auto timeline = list.setMediaSources(
      holders, std::make_shared<DefaultShuffleOrder>(std::vector<int>{ 0, 1, 2 }));
  ASSERT_EQ(timeline->getWindowCount(), 3);

  // Move the third source to the front: the windows of the two others shift by one.
  timeline = list.moveMediaSources(2, 3, 0, nullptr);
  EXPECT_EQ(windowUrls(timeline), (std::vector<std::string>{"memory://third.wav",
    "memory://first.wav", "memory://second.wav"}));
  EXPECT_EQ(timeline->getWindowCount(), 3);
  // Every source owns the window the playlist reports for it, which is what the move has to keep
  // true: `windowUrls` reads the windows through the first window index of each child source.
  EXPECT_EQ(timeline->getUidOfPeriod(0).has_value(), true);
  list.release();
}

TEST(MediaSourceListTest, ClearsEveryMediaSourceOfThePlaylist) {
  RecordingInfoRefreshListener infoListener;
  MediaSourceList list(&infoListener);
  const auto media = test::buildWav(kSampleRate, kChannelCount, kSampleCount);

  const auto holders = buildHolders({"memory://first.wav", "memory://second.wav"}, media);
  auto order = std::make_shared<DefaultShuffleOrder>(std::vector<int>{ 0, 1 });
  auto timeline = list.setMediaSources(holders, order);
  ASSERT_EQ(timeline->getWindowCount(), 2);

  timeline = list.clear(order->cloneAndClear());
  EXPECT_EQ(timeline->getWindowCount(), 0);
  EXPECT_EQ(timeline->getPeriodCount(), 0);
  EXPECT_EQ(list.getSize(), 0);
  list.release();
}

TEST(MediaSourceListTest, ReportsTheCreationAndTheReleaseOfAPeriod) {
  RecordingInfoRefreshListener infoListener;
  MediaSourceList list(&infoListener);
  const auto media = test::buildWav(kSampleRate, kChannelCount, kSampleCount);

  // The playlist owns two windows, so the period that is created for the second one has to be
  // reported with the second window index and the playlist identifier of the period.
  std::vector<std::shared_ptr<MediaSourceHolder>> holders{
    std::make_shared<MediaSourceHolder>(
        std::make_shared<ProgressiveMediaSource>(
            MediaItem::fromUrl("memory://first.wav"), InMemoryDataSourceFactory(media)),
        /* useLazyPreparation= */ false),
    std::make_shared<MediaSourceHolder>(
        std::make_shared<ProgressiveMediaSource>(
            MediaItem::fromUrl("memory://second.wav"), InMemoryDataSourceFactory(media)),
        /* useLazyPreparation= */ false)
  };
  auto timeline = list.setMediaSources(holders,
      std::make_shared<DefaultShuffleOrder>(std::vector<int>{ 0, 1 }));
  ASSERT_EQ(timeline->getWindowCount(), 2);
  list.prepare();

  auto listener = std::make_shared<RecordingEventListener>();
  list.addEventListener(nullptr, listener);

  const auto periodUid = timeline->getUidOfPeriod(1);
  ASSERT_TRUE(periodUid.has_value());
  const auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, 256 * 1024);
  auto period = list.createPeriod(MediaPeriodId(periodUid.value()), allocator, 0);
  ASSERT_NE(period, nullptr);

  ASSERT_TRUE(listener->waitFor([&] { return listener->mediaPeriodCreatedCount() > 0; }, kTimeout))
      << "the creation of the period was not reported";
  EXPECT_EQ(listener->lastMediaPeriodCreatedWindowIndex(), 1);
  ASSERT_TRUE(listener->lastCreatedMediaPeriodId().has_value());
  EXPECT_EQ(listener->lastCreatedMediaPeriodId()->periodUid(), periodUid.value());

  list.releasePeriod(period);
  ASSERT_TRUE(listener->waitFor([&] { return listener->mediaPeriodReleasedCount() > 0; }, kTimeout))
      << "the release of the period was not reported";
  // The release carries the same identifier and the same window as the creation.
  EXPECT_EQ(listener->lastMediaPeriodReleasedWindowIndex(), 1);
  ASSERT_TRUE(listener->lastReleasedMediaPeriodId().has_value());
  EXPECT_EQ(listener->lastReleasedMediaPeriodId()->periodUid(), periodUid.value());

  list.release();
}

} // namespace anonymous
} // namespace coral
