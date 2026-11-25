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

#include <coral/analytics/AnalyticsCollector.h>
#include <coral/source/MediaItem.h>
#include <coral/source/SinglePeriodTimeline.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace coral {
namespace {

constexpr int64_t kDurationUs = 60000000;

std::shared_ptr<Timeline> createTimeline(const std::string& url) {
  return std::make_shared<SinglePeriodTimeline>(kDurationUs,
      /* isSeekable= */ true,
      /* isDynamic= */ false,
      /* isLive= */ false,
      MediaItem::fromUrl(url));
}

/**
 * Records every event an analytics listener receives.
 */
class RecordingAnalyticsListener final : public AnalyticsListener {
public:
  void onPlaybackStateChanged(const EventTime& eventTime, Player::State state) override {
    ++playbackStateCount;
    lastEventTime = eventTime;
    lastPlaybackState = state;
  }

  void onIsLoadingChanged(const EventTime& eventTime, bool isLoading) override {
    ++isLoadingCount;
    lastEventTime = eventTime;
  }

  void onMediaPeriodCreated(const EventTime& eventTime) override {
    ++mediaPeriodCreatedCount;
    lastEventTime = eventTime;
  }

  void onMediaPeriodReleased(const EventTime& eventTime) override {
    ++mediaPeriodReleasedCount;
    lastEventTime = eventTime;
  }

  void onLoadStarted(const EventTime& eventTime, const MediaLoadData& mediaLoadData) override {
    ++loadStartedCount;
    lastEventTime = eventTime;
    lastMediaLoadData = mediaLoadData;
  }

  void onLoadCompleted(const EventTime& eventTime, const MediaLoadData& mediaLoadData) override {
    ++loadCompletedCount;
    lastEventTime = eventTime;
  }

  void onLoadError(const EventTime& eventTime, const MediaLoadData& mediaLoadData,
      const std::string& errorMessage, bool wasCanceled) override {
    ++loadErrorCount;
    lastEventTime = eventTime;
    lastErrorMessage = errorMessage;
    lastWasCanceled = wasCanceled;
  }

  void onVideoSizeChanged(const EventTime& eventTime, int width, int height) override {
    ++videoSizeCount;
    lastEventTime = eventTime;
    lastWidth = width;
    lastHeight = height;
  }

  int playbackStateCount = 0;
  int isLoadingCount = 0;
  int mediaPeriodCreatedCount = 0;
  int mediaPeriodReleasedCount = 0;
  int loadStartedCount = 0;
  int loadCompletedCount = 0;
  int loadErrorCount = 0;
  int videoSizeCount = 0;
  Player::State lastPlaybackState = Player::State::STATE_IDLE;
  EventTime lastEventTime;
  MediaLoadData lastMediaLoadData;
  std::string lastErrorMessage;
  bool lastWasCanceled = false;
  int lastWidth = 0;
  int lastHeight = 0;
};

TEST(AnalyticsCollectorTest, ForwardsThePlayerEventsWithTheStateOfThePlayer) {
  auto collector = std::make_shared<AnalyticsCollector>();
  auto listener = std::make_shared<RecordingAnalyticsListener>();
  collector->addListener(listener);
  EXPECT_EQ(collector->getListenerCount(), 1);

  const auto timeline = createTimeline("memory://test.wav");
  const auto periodId = MediaPeriodId(PeriodUid("period"), /* windowSequenceNumber= */ 3);
  collector->setState(timeline, /* windowIndex= */ 0, periodId, /* positionMs= */ 1500,
      /* bufferedDurationMs= */ 2500);

  collector->onPlaybackStateChanged(Player::State::STATE_READY);
  EXPECT_EQ(listener->playbackStateCount, 1);
  EXPECT_EQ(listener->lastPlaybackState, Player::State::STATE_READY);

  // Every event carries the context of the player: the timeline, the window the event belongs to, the
  // period, the playback position and the buffered duration.
  const auto& eventTime = listener->lastEventTime;
  EXPECT_EQ(eventTime.timeline, timeline);
  EXPECT_EQ(eventTime.windowIndex, 0);
  ASSERT_TRUE(eventTime.mediaPeriodId.has_value());
  EXPECT_EQ(eventTime.mediaPeriodId.value(), periodId);
  EXPECT_EQ(eventTime.eventPlaybackPositionMs, 1500);
  EXPECT_EQ(eventTime.totalBufferedDurationMs, 2500);
  EXPECT_EQ(eventTime.currentTimelineWindowCount, 1);
  EXPECT_GT(eventTime.eventTimeMs, 0);
  // The media item of the window is reachable from the event time.
  ASSERT_NE(eventTime.getMediaItem(), nullptr);
  EXPECT_EQ(eventTime.getMediaItem()->getUrl(), "memory://test.wav");
}

TEST(AnalyticsCollectorTest, ForwardsTheLoadEventsWithTheWindowTheyBelongTo) {
  auto collector = std::make_shared<AnalyticsCollector>();
  auto listener = std::make_shared<RecordingAnalyticsListener>();
  collector->addListener(listener);

  // The player as a whole is playing the first window.
  collector->setState(createTimeline("memory://first.wav"), /* windowIndex= */ 0,
      MediaPeriodId(PeriodUid("first")), /* positionMs= */ 0, /* bufferedDurationMs= */ 0);

  // The event belongs to the second window, whose source is loaded ahead of the player.
  const auto secondWindowPeriodId = MediaPeriodId(PeriodUid("second"), 2);
  const auto mediaLoadData = MediaLoadData(MediaType::kMediaTypeAudio,
      /* trackFormat= */ nullptr, /* mediaStartTimeMs= */ 100, /* mediaEndTimeMs= */ 200);
  collector->onLoadStarted(/* windowIndex= */ 1, secondWindowPeriodId,
      LoadEventInfo(1, "memory://second.wav", {}, 0, 0, 0), mediaLoadData);

  EXPECT_EQ(listener->loadStartedCount, 1);
  // The window and the period of the event win over the ones of the player, so a listener can
  // attribute a load of a source that is being prepared ahead of the playback.
  EXPECT_EQ(listener->lastEventTime.windowIndex, 1);
  ASSERT_TRUE(listener->lastEventTime.mediaPeriodId.has_value());
  EXPECT_EQ(listener->lastEventTime.mediaPeriodId.value(), secondWindowPeriodId);
  EXPECT_EQ(listener->lastMediaLoadData.mediaStartTimeMs, 100);

  collector->onLoadCompleted(1, secondWindowPeriodId, LoadEventInfo(1, "memory://second.wav", {}, 0, 0, 0),
      mediaLoadData);
  EXPECT_EQ(listener->loadCompletedCount, 1);
}

TEST(AnalyticsCollectorTest, ForwardsTheErrorOfALoadWithItsMessage) {
  auto collector = std::make_shared<AnalyticsCollector>();
  auto listener = std::make_shared<RecordingAnalyticsListener>();
  collector->addListener(listener);

  Error error(ErrCode::kUnknown, "the container is not supported");
  collector->onLoadError(/* windowIndex= */ 0, MediaPeriodId(PeriodUid("period")),
      LoadEventInfo(1, "memory://test.wav", {}, 0, 0, 0), MediaLoadData(), &error, /* wasCanceled= */ true);

  EXPECT_EQ(listener->loadErrorCount, 1);
  EXPECT_EQ(listener->lastErrorMessage, "the container is not supported");
  EXPECT_TRUE(listener->lastWasCanceled);
}

TEST(AnalyticsCollectorTest, ReportsTheVideoSizeAndStopsAfterTheListenerIsRemoved) {
  auto collector = std::make_shared<AnalyticsCollector>();
  auto listener = std::make_shared<RecordingAnalyticsListener>();
  collector->addListener(listener);

  collector->onVideoSizeChanged(1920, 1080);
  EXPECT_EQ(listener->videoSizeCount, 1);
  EXPECT_EQ(listener->lastWidth, 1920);
  EXPECT_EQ(listener->lastHeight, 1080);

  collector->removeListener(listener);
  EXPECT_EQ(collector->getListenerCount(), 0);
  collector->onVideoSizeChanged(640, 360);
  EXPECT_EQ(listener->videoSizeCount, 1);
}

TEST(AnalyticsCollectorTest, CallsEveryListenerAndToleratesOneThatRemovesItself) {
  auto collector = std::make_shared<AnalyticsCollector>();
  auto first = std::make_shared<RecordingAnalyticsListener>();
  auto second = std::make_shared<RecordingAnalyticsListener>();
  collector->addListener(first);
  collector->addListener(second);

  // A listener that unregisters itself while it handles an event must not stop the dispatch, which is
  // why the collector calls a snapshot of the list.
  class SelfRemovingListener final : public AnalyticsListener {
  public:
    explicit SelfRemovingListener(std::shared_ptr<AnalyticsCollector> collector) noexcept
        : collector(std::move(collector)) {
    }

    void onIsLoadingChanged(const EventTime& eventTime, bool isLoading) override {
      static_cast<void>(eventTime);
      static_cast<void>(isLoading);
      ++count;
      collector->removeListener(sharedListener);
    }

    std::shared_ptr<AnalyticsCollector> collector;
    std::shared_ptr<AnalyticsListener> sharedListener;
    int count = 0;
  };

  auto selfRemoving = std::make_shared<SelfRemovingListener>(collector);
  selfRemoving->sharedListener = selfRemoving;
  collector->addListener(selfRemoving);

  collector->onIsLoadingChanged(true);
  EXPECT_EQ(selfRemoving->count, 1);
  EXPECT_EQ(first->isLoadingCount, 1);
  EXPECT_EQ(second->isLoadingCount, 1);
  EXPECT_EQ(collector->getListenerCount(), 2);
}

TEST(AnalyticsCollectorTest, ForwardsTheLifecycleOfAMediaPeriod) {
  auto collector = std::make_shared<AnalyticsCollector>();
  auto listener = std::make_shared<RecordingAnalyticsListener>();
  collector->addListener(listener);

  // The period belongs to the second window of the playlist, which is the one that is loaded ahead of
  // the playback.
  const auto secondWindowPeriodId = MediaPeriodId(PeriodUid("second"), /* windowSequenceNumber= */ 7);
  collector->onMediaPeriodCreated(/* windowIndex= */ 1, secondWindowPeriodId);

  EXPECT_EQ(listener->mediaPeriodCreatedCount, 1);
  EXPECT_EQ(listener->lastEventTime.windowIndex, 1);
  ASSERT_TRUE(listener->lastEventTime.mediaPeriodId.has_value());
  EXPECT_EQ(listener->lastEventTime.mediaPeriodId.value(), secondWindowPeriodId);

  collector->onMediaPeriodReleased(1, secondWindowPeriodId);
  EXPECT_EQ(listener->mediaPeriodReleasedCount, 1);
  EXPECT_EQ(listener->lastEventTime.windowIndex, 1);
  ASSERT_TRUE(listener->lastEventTime.mediaPeriodId.has_value());
  EXPECT_EQ(listener->lastEventTime.mediaPeriodId.value(), secondWindowPeriodId);
}

TEST(EventTimeTest, ReportsNoMediaItemWhenTheEventBelongsToNoWindow) {
  EventTime eventTime;
  EXPECT_EQ(eventTime.getMediaItem(), nullptr);
  EXPECT_EQ(eventTime.windowIndex, Definitions::kIndexUnset);

  eventTime.timeline = createTimeline("memory://test.wav");
  eventTime.windowIndex = Definitions::kIndexUnset;
  EXPECT_EQ(eventTime.getMediaItem(), nullptr);

  eventTime.windowIndex = 7;
  EXPECT_EQ(eventTime.getMediaItem(), nullptr);

  eventTime.windowIndex = 0;
  ASSERT_NE(eventTime.getMediaItem(), nullptr);
  EXPECT_EQ(eventTime.getMediaItem()->getUrl(), "memory://test.wav");
}

} // namespace anonymous
} // namespace coral
