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

#include <coral/playback/Player.h>
#include <coral/source/MediaItem.h>
#include <coral/source/MediaSource.h>
#include <coral/source/MediaSourceFactory.h>
#include <coral/source/SilenceMediaSource.h>

// The player implementation is an internal type of the library, see the CMakeLists of this target for
// the include path that exposes it.
#include "details/AvLivePlayer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace coral {
namespace {

using namespace std::chrono_literals;

/** The duration of the media the player is given: short enough to play it out in a test. */
constexpr int64_t kSilenceDurationUs = 300000;
constexpr auto kTimeout = 10000ms;

/**
 * Builds the source of the player.
 *
 * <p>Silence is the source that lets a test play a whole media item without a decoder: its duration is
 * known before it is prepared, and its period generates the media instead of reading it.
 */
class SilenceMediaSourceFactory final : public MediaSourceFactory {
public:
  explicit SilenceMediaSourceFactory(const int64_t durationUs) noexcept
      : durationUs(durationUs) {
  }

  std::shared_ptr<MediaSource> createMediaSource(
      const std::shared_ptr<MediaItem>& mediaItem) override {
    return std::make_shared<SilenceMediaSource>(durationUs, mediaItem);
  }

private:
  int64_t durationUs;
};

/** Polls a condition until it holds or the timeout expires. */
bool waitFor(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}

/**
 * Creates a player without renderers.
 *
 * <p>Nothing renders, but the player still prepares its media source, runs its playback thread and
 * drives its clock, which is all that the positions and the states of a test depend on.
 */
std::shared_ptr<AvLivePlayer> createPlayerWithoutRenderers() {
  auto player =
      std::make_shared<AvLivePlayer>(std::vector<std::shared_ptr<Renderer>>{});
  player->setMediaSourceFactory(
      std::make_shared<SilenceMediaSourceFactory>(kSilenceDurationUs));
  player->setMediaItem(MediaItem::fromUrl("silence://default-position"));
  return player;
}

/**
 * Records the playback parameters the player reported.
 *
 * <p>The parameters are reported on the playback thread, so the recorded values are guarded: a test that
 * reads them does so from its own thread.
 */
class RecordingPlaybackParametersListener final : public Player::EventListener {
public:
  void onPlaybackParametersChanged(const Parameters& parameters) override {
    std::lock_guard<std::mutex> lock(mutex_);
    speed_ = parameters.speed;
    pitch_ = parameters.pitch;
  }

  [[nodiscard]] float speed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return speed_;
  }

  [[nodiscard]] float pitch() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pitch_;
  }

private:
  mutable std::mutex mutex_;
  float speed_ = 0.0f;
  float pitch_ = 0.0f;
};

// ==================================================================================================

TEST(AvLivePlayerTest, StartsAndStopsTheEffectPipeline) {
  auto player = createPlayerWithoutRenderers();
  player->enableVideoEffectPipeline(nullptr);
  EXPECT_TRUE(player->isVideoEffectPipelineEnabled());

  player->prepare();
  EXPECT_TRUE(waitFor(
      [&] { return player->getPlaybackState() != Player::State::STATE_IDLE; }, kTimeout));

  player->stop();
  player->release();
}

// ==================================================================================================

TEST(AvLivePlayerTest, PlayingAnEndedPlayerRestartsItFromTheDefaultPosition) {
  auto player = createPlayerWithoutRenderers();
  player->prepare();
  player->play();

  ASSERT_TRUE(waitFor(
      [&] { return player->getPlaybackState() == Player::State::STATE_ENDED; }, kTimeout))
      << "the player never reached the ended state";
  EXPECT_GE(player->getCurrentPositionUs(), kSilenceDurationUs);

  // ExoPlayer's `play()` only sets playWhenReady, so an ended player stays where it is and plays
  // nothing: the application is expected to call `seekToDefaultPosition()` before it plays again. A
  // player that is asked to play an ended media starts it again, which is the effect an application
  // asks for with `play()` on its own.
  player->play();
  ASSERT_TRUE(waitFor(
      [&] { return player->getPlaybackState() != Player::State::STATE_ENDED; }, kTimeout))
      << "the player did not restart";
  EXPECT_LT(player->getCurrentPositionUs(), kSilenceDurationUs);
  EXPECT_TRUE(player->getPlayWhenReady());

  player->release();
}

TEST(AvLivePlayerTest, SeeksToTheDefaultPositionOfTheMedia) {
  auto player = createPlayerWithoutRenderers();
  player->prepare();
  ASSERT_TRUE(waitFor(
      [&] { return player->getPlaybackState() != Player::State::STATE_IDLE; }, kTimeout))
      << "the player was never prepared";

  // The player is seeked away from the position the media declares as its default one, and
  // `seekToDefaultPosition` takes it back there.
  player->seekTo(kSilenceDurationUs / 2);
  ASSERT_TRUE(waitFor([&] {
    return player->getCurrentPositionUs() >= kSilenceDurationUs / 4;
  }, kTimeout)) << "the player did not apply the seek";

  player->seekToDefaultPosition();
  EXPECT_TRUE(waitFor([&] { return player->getCurrentPositionUs() < kSilenceDurationUs / 4; },
      kTimeout)) << "the player did not go back to the default position";

  player->release();
}

TEST(AvLivePlayerTest, PlayingAnIdlePlayerPreparesIt) {
  auto player = createPlayerWithoutRenderers();
  EXPECT_EQ(player->getPlaybackState(), Player::State::STATE_IDLE);

  // `play()` prepares a player that has not been prepared yet, which is what it did before it learned
  // to restart an ended one.
  player->play();
  EXPECT_TRUE(waitFor(
      [&] { return player->getPlaybackState() != Player::State::STATE_IDLE; }, kTimeout))
      << "playing an idle player did not prepare it";
  EXPECT_TRUE(player->getPlayWhenReady());

  player->release();
}

TEST(AvLivePlayerTest, KeepsThePitchOfThePlaybackParameters) {
  auto player = createPlayerWithoutRenderers();
  auto listener = std::make_shared<RecordingPlaybackParametersListener>();
  player->addListener(listener);

  // The pitch is part of the playback parameters and reaches the clock with them. The player used to
  // rebuild the parameters it applies from the speed alone, which dropped the pitch of every caller.
  player->setPlaybackParameters(Parameters(2.0f, 1.5f));
  ASSERT_TRUE(waitFor([&] { return listener->pitch() == 1.5f; }, kTimeout))
      << "the player never reported the pitch";
  EXPECT_FLOAT_EQ(listener->speed(), 2.0f);
  EXPECT_FLOAT_EQ(player->getPlaybackParameters().speed, 2.0f);
  EXPECT_FLOAT_EQ(player->getPlaybackParameters().pitch, 1.5f);

  // A pitch only change is reported as well, which the comparison of the parameters has to see.
  player->setPlaybackParameters(Parameters(2.0f, 0.5f));
  ASSERT_TRUE(waitFor([&] { return listener->pitch() == 0.5f; }, kTimeout))
      << "the player did not report a pitch only change";
  EXPECT_FLOAT_EQ(listener->speed(), 2.0f);

  player->removeListener(listener);
  player->release();
}

} // namespace anonymous
} // namespace coral
