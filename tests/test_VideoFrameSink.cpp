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
 * @file test_VideoFrameSink.cpp
 *
 * The sink is a public seam: an application (or a test) can install its own adapter instead of the
 * player's CPU sink. This case proves the renderer hands it the decoded frames without needing an
 * output presentation path, which is what a golden-image recorder needs.
 */

#include <coral/playback/LivePlayer.h>
#include <coral/playback/Player.h>
#include <coral/playback/VideoFrameSink.h>

#include <effect/EffectEngine.h>
#include <effect/VideoFrameInputNode.h>

#include <media/PixelBuffer.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace coral {
namespace {

using namespace std::chrono_literals;

/** A sink that records the frames the renderer presents and releases them. */
class RecordingVideoFrameSink final : public VideoFrameSink {
public:
  bool submit(std::shared_ptr<AVPixelBuffer> frame, int64_t positionUs) override {
    {
      const std::lock_guard<std::mutex> lock(mutex);
      positions.push_back(positionUs);
    }
    if (frame != nullptr) {
      frame->release();
    }
    submitted.fetch_add(1, std::memory_order_acq_rel);
    return true;
  }

  std::vector<int64_t> recordedPositions() {
    const std::lock_guard<std::mutex> lock(mutex);
    return positions;
  }

  void discard(std::shared_ptr<AVPixelBuffer> frame) override {
    if (frame != nullptr) {
      frame->release();
    }
    discarded.fetch_add(1, std::memory_order_acq_rel);
  }

  void flush() override {
  }

  std::atomic<int> submitted{0};
  std::atomic<int> discarded{0};

private:
  std::mutex mutex;
  std::vector<int64_t> positions;
};

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

TEST(VideoFrameSinkTest, ReceivesDecodedFramesWithoutAnOutputPath) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  auto player = LivePlayer::create(path, LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);

  const auto input = VideoFrameInputNode::create(320, 180);
  ASSERT_NE(input, nullptr);
  const auto sink = std::make_shared<RecordingVideoFrameSink>();

  VideoEffectInput effectInput;
  effectInput.inputNode = input;
  effectInput.frameSink = sink;
  // A null output factory keeps the graph headless: the test only exercises the sink seam, not a
  // window or a readback.
  player->enableVideoEffectPipeline(nullptr, effectInput);
  ASSERT_TRUE(player->isVideoEffectPipelineEnabled());

  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);

  player->prepare();
  player->play();

  if (!waitFor([&] { return engine->isRunning(); }, 5s)) {
    player->release();
    GTEST_SKIP() << "the effect engine could not start on this host: " << engine->diagnostics();
  }
  EXPECT_TRUE(waitFor([&] { return sink->submitted.load(std::memory_order_acquire) >= 10; }, 15s))
      << "the renderer never handed enough frames to the custom sink: " << engine->diagnostics();

  player->release();

  // VideoToolbox calls back in decode order; the renderer must not see a frame whose presentation
  // time is older than the one before it, or its release control drops the reordered frames.
  const auto positions = sink->recordedPositions();
  ASSERT_GT(positions.size(), 1u);
  EXPECT_TRUE(std::is_sorted(positions.begin(), positions.end()))
      << "presentation times went backwards: " << positions[0] << ", " << positions[1];
}

} // namespace
} // namespace coral
