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
#include <coral/playback/Thumbnail.h>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>

namespace coral {
namespace {

class FakeThumbnailProvider final : public ThumbnailProvider {
public:
  void requestThumbnail(const int64_t positionUs, Callback callback) override {
    requested.store(positionUs, std::memory_order_release);
    if (callback) {
      Thumbnail thumbnail;
      thumbnail.positionUs = positionUs;
      callback(std::move(thumbnail));
    }
  }

  void cancel() override {
    cancelled.fetch_add(1, std::memory_order_acq_rel);
  }

  std::atomic<int64_t> requested{0};
  std::atomic<int> cancelled{0};
};

TEST(ThumbnailProviderTest, ForwardsRequestsAndCancelsOnRelease) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  auto player = LivePlayer::create(path);
  ASSERT_NE(player, nullptr);
  auto provider = std::make_shared<FakeThumbnailProvider>();
  player->setThumbnailProvider(provider);
  EXPECT_EQ(player->getThumbnailProvider(), provider);

  int64_t receivedUs = -1;
  player->requestThumbnail(1'500'000, [&](Thumbnail thumbnail) {
    receivedUs = thumbnail.positionUs;
  });
  EXPECT_EQ(provider->requested.load(std::memory_order_acquire), 1'500'000);
  EXPECT_EQ(receivedUs, 1'500'000);

  player->release();
  EXPECT_GE(provider->cancelled.load(std::memory_order_acquire), 1);
}

} // namespace
} // namespace coral
