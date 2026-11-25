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

#include <coral/playback/LoadControl.h>
#include <coral/selection/TrackSelection.h>
#include <coral/source/MediaItem.h>
#include <coral/source/MediaPeriodId.h>
#include <coral/source/SinglePeriodTimeline.h>
#include <coral/source/Timeline.h>

// The default load control is an internal type of the library, see the CMakeLists of this target for the
// include path that exposes it.
#include "details/DefaultLoadControl.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace coral {
namespace {

constexpr int64_t kDurationUs = 60 * 1000 * 1000;
constexpr int64_t kSecondUs = 1000 * 1000;

/** The timeline of a single media item, together with the id of the period it holds. */
struct Fixture {
  std::shared_ptr<Timeline> timeline;
  MediaPeriodId periodId;
};

Fixture createFixture(const std::string& url) {
  auto timeline = std::make_shared<SinglePeriodTimeline>(kDurationUs,
      /* isSeekable= */ true,
      /* isDynamic= */ false,
      /* isLive= */ false,
      MediaItem::fromUrl(url));
  auto period = std::make_shared<Timeline::Period>();
  // The uid of a period is only set when it is asked for, which is how a player identifies the period
  // it reports to the load control.
  timeline->getPeriod(0, period, /* setIds= */ true);
  return { timeline, MediaPeriodId(period->uid.value()) };
}

LoadControl::Parameters createParameters(const Fixture& fixture, const int64_t bufferedDurationUs) {
  LoadControl::Parameters parameters;
  parameters.timeline = fixture.timeline;
  parameters.mediaPeriodId = fixture.periodId;
  parameters.playbackPositionUs = 0;
  parameters.bufferedDurationUs = bufferedDurationUs;
  parameters.playbackSpeed = 1.0f;
  parameters.playWhenReady = true;
  parameters.rebuffering = false;
  parameters.targetLiveOffsetUs = LoadControl::kTimeUnset;
  parameters.lastRebufferRealtimeMs = LoadControl::kTimeUnset;
  return parameters;
}

// ==================================================================================================

TEST(DefaultLoadControlTest, ContinuesLoadingWhileTheBufferIsNotFull) {
  auto loadControl = std::make_shared<DefaultLoadControl>();
  const auto fixture = createFixture("http://example.com/media.mp4");

  // Nothing is buffered yet, so the player has to keep loading.
  EXPECT_TRUE(loadControl->shouldContinueLoading(createParameters(fixture, 0)))
      << "the player has to load when nothing is buffered";

  // A minute of media is more than the default control ever buffers.
  EXPECT_FALSE(loadControl->shouldContinueLoading(createParameters(fixture, kDurationUs)))
      << "the player has to stop loading when the maximum buffer is reached";
}

TEST(DefaultLoadControlTest, KeepsLoadingWhenTheBufferedDurationIsUnknown) {
  auto loadControl = std::make_shared<DefaultLoadControl>();
  const auto fixture = createFixture("http://example.com/media.mp4");
  ASSERT_TRUE(loadControl->shouldContinueLoading(createParameters(fixture, 0)));

  // An unknown buffered duration is not a buffer that is longer than the maximum one: it leaves the
  // state of the control alone. Reading it as an unsigned duration turns it into the largest possible
  // value, which stops the loading of a player that has nothing buffered yet.
  EXPECT_TRUE(loadControl->shouldContinueLoading(createParameters(fixture, LoadControl::kTimeUnset)))
      << "an unknown buffered duration must not stop the loading";
}

TEST(DefaultLoadControlTest, StartsPlaybackOnlyOnceEnoughIsBuffered) {
  auto loadControl = std::make_shared<DefaultLoadControl>();
  const auto fixture = createFixture("http://example.com/media.mp4");

  EXPECT_FALSE(loadControl->shouldStartPlayback(createParameters(fixture, 0)))
      << "the playback must not start without buffered media";
  EXPECT_TRUE(loadControl->shouldStartPlayback(createParameters(fixture, 5 * kSecondUs)))
      << "five seconds of media is enough to start the playback";
}

TEST(DefaultLoadControlTest, BuffersLessForALocalFileThanForARemoteMedia) {
  const auto localFixture = createFixture("file:///sdcard/media.mp4");
  const auto remoteFixture = createFixture("http://example.com/media.mp4");
  auto localControl = std::make_shared<DefaultLoadControl>();
  auto remoteControl = std::make_shared<DefaultLoadControl>();

  // Five seconds is below the minimum buffer of a stream and above the minimum buffer of a local file,
  // so the stream keeps loading while the local file already has all the media it asks for.
  EXPECT_TRUE(remoteControl->shouldContinueLoading(createParameters(remoteFixture, 5 * kSecondUs)))
      << "a remote media is not local playback";
  EXPECT_FALSE(localControl->shouldContinueLoading(createParameters(localFixture, 5 * kSecondUs)))
      << "a file on the device is local playback";
}

TEST(DefaultLoadControlTest, SelectsTheTracksOfThePlayer) {
  auto loadControl = std::make_shared<DefaultLoadControl>();
  const auto fixture = createFixture("http://example.com/media.mp4");

  // The selection of the tracks is what the player does as soon as it is prepared, and it is where the
  // load control is asked whether the media is local: it must not read anything that was not
  // initialized, and the allocator has to be given the target buffer size of the selection.
  auto selections = std::make_shared<TrackSelectionArray>(
      std::vector<std::shared_ptr<TrackSelection>>{ nullptr });
  loadControl->onTracksSelected(createParameters(fixture, 0), nullptr, selections);

  EXPECT_EQ(loadControl->getAllocator()->getTotalBytesAllocated(), 0);
  EXPECT_TRUE(loadControl->shouldContinueLoading(createParameters(fixture, 0)));
}

} // namespace anonymous
} // namespace coral
