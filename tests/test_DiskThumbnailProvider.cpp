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

#include <coral/playback/DiskThumbnailProvider.h>

#include <media/PixelBuffer.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace coral {
namespace {

std::shared_ptr<PixelBuffer> makeBgraFrame(const uint8_t value) {
  auto frame = std::make_shared<BgraPixelBuffer>(4, 4);
  std::fill(frame->data(), frame->data() + frame->size(), value);
  return PixelBuffer::fromAVPixelBuffer(frame);
}

class FakeThumbnailProvider final : public ThumbnailProvider {
public:
  void requestThumbnail(const int64_t positionUs, Callback callback) override {
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      ++mRequests;
    }
    Thumbnail thumbnail;
    thumbnail.positionUs = positionUs;
    thumbnail.frame = makeBgraFrame(0x40);
    if (callback) {
      callback(std::move(thumbnail));
    }
  }

  int requests() const {
    const std::lock_guard<std::mutex> lock(mMutex);
    return mRequests;
  }

private:
  mutable std::mutex mMutex;
  int mRequests = 0;
};

TEST(DiskThumbnailProviderTest, ServesANewProviderFromDisk) {
  const auto directory = std::filesystem::temp_directory_path() /
    ("coral-thumb-cache-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::remove_all(directory);

  DiskThumbnailCacheConfig config;
  config.directory = directory.string();
  config.bucketUs = 500'000;
  config.maxFiles = 8;

  auto firstSource = std::make_shared<FakeThumbnailProvider>();
  auto first = DiskThumbnailProvider::create("file:///video.mp4", firstSource, config);
  ASSERT_NE(first, nullptr);
  std::shared_ptr<PixelBuffer> firstFrame;
  first->requestThumbnail(1'200'000, [&](Thumbnail thumbnail) {
    firstFrame = thumbnail.frame;
  });
  ASSERT_NE(firstFrame, nullptr);
  EXPECT_EQ(firstSource->requests(), 1);

  size_t files = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().extension() == ".thumb") {
      ++files;
    }
  }
  EXPECT_EQ(files, 1u);

  auto secondSource = std::make_shared<FakeThumbnailProvider>();
  auto second = DiskThumbnailProvider::create("file:///video.mp4", secondSource, config);
  ASSERT_NE(second, nullptr);
  std::shared_ptr<PixelBuffer> secondFrame;
  second->requestThumbnail(1'300'000, [&](Thumbnail thumbnail) {
    secondFrame = thumbnail.frame;
  });
  ASSERT_NE(secondFrame, nullptr);
  EXPECT_EQ(secondSource->requests(), 0) << "the second request should not decode again";
  EXPECT_EQ(secondFrame->width(), 4);
  EXPECT_EQ(secondFrame->height(), 4);

  first->cancel();
  second->cancel();
  std::filesystem::remove_all(directory);
}

} // namespace
} // namespace coral
