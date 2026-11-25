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

#include "TestMedia.h"

#include <coral/selection/FixedTrackSelection.h>
#include <coral/source/MediaItem.h>
#include <coral/source/MediaSource.h>
#include <coral/source/ProgressiveMediaSource.h>
#include <coral/source/SilenceMediaPeriod.h>
#include <coral/source/SilenceMediaSource.h>
#include <coral/upstream/DefaultAllocator.h>

#include <datasource/DataSource.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace coral {
namespace {

using namespace std::chrono_literals;

constexpr int64_t kSilenceDurationUs = 2000000;
constexpr int kSampleRate = 8000;
constexpr int kChannelCount = 1;
constexpr int kSampleCount = 20000;
constexpr auto kTimeout = 10000ms;

/**
 * Records the timelines a source reports.
 */
class RecordingSourceCaller final : public MediaSourceCaller {
public:
  void onSourceInfoRefreshed(const std::shared_ptr<MediaSource>& source,
      const std::shared_ptr<Timeline>& timeline) override {
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(source);
    ++refreshCount_;
    timeline_ = timeline;
    cv_.notify_all();
  }

  bool waitForRefresh(const int count, const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this, count] { return refreshCount_ >= count; });
  }

  std::shared_ptr<Timeline> timeline() {
    std::lock_guard<std::mutex> lock(mutex_);
    return timeline_;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::shared_ptr<Timeline> timeline_;
  int refreshCount_ = 0;
};

/**
 * Records the events of a media period.
 */
class RecordingPeriodCallback final : public MediaPeriod::Callback {
public:
  void onPrepared(const std::shared_ptr<MediaPeriod>& mediaPeriod) override {
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(mediaPeriod);
    prepared_ = true;
    cv_.notify_all();
  }

  void onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& mediaPeriod) override {
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(mediaPeriod);
    cv_.notify_all();
  }

  bool waitForPrepared(const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return prepared_; });
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool prepared_ = false;
};

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

// ==================================================================================================

TEST(SilenceMediaSourceTest, KnowsItsTimelineBeforeItIsPrepared) {
  auto source = std::make_shared<SilenceMediaSource>(kSilenceDurationUs);

  // The duration and the seekability are known from the moment the source exists, so a player can
  // report the window without preparing anything.
  const auto& initialTimeline = source->getInitialTimeline();
  ASSERT_NE(initialTimeline, nullptr);
  EXPECT_EQ(initialTimeline->getWindowCount(), 1);
  EXPECT_EQ(initialTimeline->getPeriodCount(), 1);
  const auto window = initialTimeline->getWindow(0, std::make_shared<Timeline::Window>(), 0);
  ASSERT_NE(window, nullptr);
  EXPECT_EQ(window->durationUs, kSilenceDurationUs);
  EXPECT_TRUE(window->isSeekable);
  EXPECT_FALSE(window->isPlaceholder);

  // A silence source is never prepared lazily, like ExoPlayer's.
  EXPECT_FALSE(source->isSingleWindow());

  auto caller = std::make_shared<RecordingSourceCaller>();
  source->prepareSource(caller);
  ASSERT_TRUE(caller->waitForRefresh(1, kTimeout));
  // The timeline that is reported once prepared is the same one.
  EXPECT_EQ(caller->timeline(), initialTimeline);
}

TEST(SilenceMediaSourceTest, CreatesAPeriodThatDeliversSilence) {
  auto source = std::make_shared<SilenceMediaSource>(kSilenceDurationUs);
  auto caller = std::make_shared<RecordingSourceCaller>();
  source->prepareSource(caller);
  source->enable(caller);

  const auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, 64 * 1024);
  auto period = source->createPeriod(MediaPeriodId(PeriodUid("silence-period")), allocator, 0);
  ASSERT_NE(period, nullptr);
  EXPECT_NE(std::dynamic_pointer_cast<SilenceMediaPeriod>(period), nullptr);

  RecordingPeriodCallback callback;
  period->prepare(&callback, 0);
  // Silence needs nothing to be loaded, so it is prepared synchronously.
  EXPECT_TRUE(callback.waitForPrepared(kTimeout));
  EXPECT_EQ(period->getBufferedPositionUs(), Definitions::kTimeEndOfSource);
  EXPECT_FALSE(period->isLoading());

  // The seek position is constrained to the duration of the silence.
  EXPECT_EQ(period->seekToUs(kSilenceDurationUs * 2), kSilenceDurationUs);

  source->releasePeriod(period);
  source->releaseSource(caller);
}

TEST(ProgressiveMediaSourceTest, ProvidesAPlaceholderInitialTimeline) {
  const auto item = MediaItem::fromUrl("memory://test.wav");
  auto source = std::make_shared<ProgressiveMediaSource>(
      item, InMemoryDataSourceFactory(test::buildWav(kSampleRate, kChannelCount, kSampleCount)));

  // The source knows the shape of its timeline before it is prepared: one window, one period, whose
  // duration is not known yet. A player can report the window right away, and the masking source
  // does not have to rename the period.
  const auto& initialTimeline = source->getInitialTimeline();
  ASSERT_NE(initialTimeline, nullptr);
  EXPECT_EQ(initialTimeline->getWindowCount(), 1);
  EXPECT_EQ(initialTimeline->getPeriodCount(), 1);
  const auto window = initialTimeline->getWindow(0, std::make_shared<Timeline::Window>(), 0);
  ASSERT_NE(window, nullptr);
  EXPECT_TRUE(window->isPlaceholder);
  EXPECT_EQ(window->durationUs, Definitions::kTimeUnset);
}

} // namespace anonymous
} // namespace coral
