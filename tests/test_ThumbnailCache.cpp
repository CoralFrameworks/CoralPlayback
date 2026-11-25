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

#include <coral/playback/ThumbnailCache.h>

#include <media/PixelBuffer.h>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace coral {
namespace {

std::shared_ptr<PixelBuffer> makeFrame() {
  auto pixels = std::make_shared<I420PixelBuffer>(4, 4);
  return PixelBuffer::fromAVPixelBuffer(pixels);
}

class FakeThumbnailProvider final : public ThumbnailProvider {
public:
  void requestThumbnail(const int64_t positionUs, Callback callback) override {
    std::vector<Callback> callbacks;
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      ++mRequests;
      mPositions.push_back(positionUs);
      if (mAsync) {
        mCallbacks.push_back(std::move(callback));
        return;
      }
      callbacks.push_back(std::move(callback));
    }
    for (auto& item : callbacks) {
      Thumbnail thumbnail;
      thumbnail.positionUs = positionUs;
      thumbnail.frame = makeFrame();
      item(std::move(thumbnail));
    }
  }

  void completeAll() {
    std::vector<Callback> callbacks;
    std::vector<int64_t> positions;
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      callbacks = std::move(mCallbacks);
      mCallbacks.clear();
      positions = mPositions;
    }
    for (size_t i = 0; i < callbacks.size(); ++i) {
      Thumbnail thumbnail;
      thumbnail.positionUs = positions.empty() ? 0 : positions.back();
      thumbnail.frame = makeFrame();
      callbacks[i](std::move(thumbnail));
    }
  }

  int requests() const {
    const std::lock_guard<std::mutex> lock(mMutex);
    return mRequests;
  }

  std::vector<int64_t> positions() const {
    const std::lock_guard<std::mutex> lock(mMutex);
    return mPositions;
  }

  bool mAsync = false;

private:
  mutable std::mutex mMutex;
  int mRequests = 0;
  std::vector<int64_t> mPositions;
  std::vector<Callback> mCallbacks;
};

TEST(ThumbnailCacheTest, CachesByBucketAndPrefetchesNeighbours) {
  auto source = std::make_shared<FakeThumbnailProvider>();
  ThumbnailCacheConfig config;
  config.capacity = 16;
  config.bucketUs = 500'000;
  config.prefetchBefore = 1;
  config.prefetchAfter = 1;
  config.prefetchStepUs = 1'000'000;
  auto cache = CachingThumbnailProvider::create(source, config);
  ASSERT_NE(cache, nullptr);

  int delivered = 0;
  cache->requestThumbnail(1'200'000, [&](Thumbnail thumbnail) {
    ++delivered;
    EXPECT_EQ(thumbnail.positionUs, 1'250'000);
    EXPECT_NE(thumbnail.frame, nullptr);
  });
  EXPECT_EQ(delivered, 1);
  EXPECT_GE(source->requests(), 3) << "the requested bucket and its two neighbours should decode";
  EXPECT_GE(cache->cachedCount(), 3u);

  const int before = source->requests();
  cache->requestThumbnail(1'300'000, [&](Thumbnail) { ++delivered; });
  EXPECT_EQ(delivered, 2);
  EXPECT_EQ(source->requests(), before) << "a request in the cached bucket must not decode again";
  const auto stats = cache->stats();
  EXPECT_GE(stats.hits, 1u);
  EXPECT_GE(stats.misses, 1u);
}

TEST(ThumbnailCacheTest, CoalescesConcurrentRequestsForOneBucket) {
  auto source = std::make_shared<FakeThumbnailProvider>();
  source->mAsync = true;
  auto cache = CachingThumbnailProvider::create(source, ThumbnailCacheConfig{.prefetchBefore = 0,
                                                                             .prefetchAfter = 0});
  ASSERT_NE(cache, nullptr);
  int delivered = 0;
  cache->requestThumbnail(2'000'000, [&](Thumbnail) { ++delivered; });
  cache->requestThumbnail(2'100'000, [&](Thumbnail) { ++delivered; });
  EXPECT_EQ(source->requests(), 1) << "both requests must share one decode";
  source->completeAll();
  EXPECT_EQ(delivered, 2);
}

TEST(ThumbnailCacheTest, EvictsLeastRecentlyUsed) {
  auto source = std::make_shared<FakeThumbnailProvider>();
  ThumbnailCacheConfig config;
  config.capacity = 2;
  config.prefetchBefore = 0;
  config.prefetchAfter = 0;
  auto cache = CachingThumbnailProvider::create(source, config);
  ASSERT_NE(cache, nullptr);
  cache->requestThumbnail(0, [](Thumbnail) {});
  cache->requestThumbnail(1'000'000, [](Thumbnail) {});
  cache->requestThumbnail(0, [](Thumbnail) {});
  cache->requestThumbnail(2'000'000, [](Thumbnail) {});
  EXPECT_EQ(cache->cachedCount(), 2u);
  const int before = source->requests();
  cache->requestThumbnail(1'000'000, [](Thumbnail) {});
  EXPECT_GT(source->requests(), before) << "the least recently used bucket should have been evicted";
}

TEST(ThumbnailCacheTest, PrefetchWarmsTheCacheWithoutACallback) {
  auto source = std::make_shared<FakeThumbnailProvider>();
  ThumbnailCacheConfig config;
  config.prefetchBefore = 0;
  config.prefetchAfter = 0;
  auto cache = CachingThumbnailProvider::create(source, config);
  ASSERT_NE(cache, nullptr);
  cache->prefetch(2'000'000);
  EXPECT_GE(source->requests(), 1);
  const int before = source->requests();
  int delivered = 0;
  cache->requestThumbnail(2'100'000, [&](Thumbnail) { ++delivered; });
  EXPECT_EQ(delivered, 1);
  EXPECT_EQ(source->requests(), before) << "the prefetched bucket should be served from cache";
}

} // namespace
} // namespace coral
