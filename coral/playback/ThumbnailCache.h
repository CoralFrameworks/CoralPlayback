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

#ifndef CORAL_PLAYBACK_THUMBNAIL_CACHE_H
#define CORAL_PLAYBACK_THUMBNAIL_CACHE_H

#include <coral/playback/Thumbnail.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace coral {

/** Tuning for {@link CachingThumbnailProvider}. */
struct ThumbnailCacheConfig {
  /** Maximum number of decoded thumbnails kept, least recently used first. Zero disables caching. */
  size_t capacity = 64;

  /**
   * The width of a cache bucket in microseconds. Requests inside the same bucket share one decode.
   * Zero selects 500 ms.
   */
  int64_t bucketUs = 500'000;

  /** How many buckets before the requested one to prefetch. */
  int prefetchBefore = 2;

  /** How many buckets after the requested one to prefetch. */
  int prefetchAfter = 2;

  /**
   * The distance between prefetched buckets in microseconds. Zero uses {@link bucketUs}; a value
   * smaller than the bucket is raised to the bucket so two prefetches never target the same bucket.
   */
  int64_t prefetchStepUs = 1'000'000;
};

/** Hit/miss/prefetch counters, for diagnostics and for tests. */
struct ThumbnailCacheStats {
  uint64_t hits = 0;
  uint64_t misses = 0;
  uint64_t prefetches = 0;
  uint64_t coalesced = 0;
};

/**
 * A caching and prefetching decorator around another {@link ThumbnailProvider}.
 *
 * <p>A request is rounded down to a bucket. Thumbnails in the same bucket are decoded once; further
 * requests are coalesced while the decode is in flight, then served from an LRU cache. After each
 * user request the provider also asks for the configured buckets around it, so dragging a seek bar
 * finds the neighbouring frames already decoded.
 *
 * <p>The wrapper is thread-safe and can be installed directly with
 * {@code Player::setThumbnailProvider}. The wrapped provider may call its callback on any thread.
 * Callbacks are not invoked after {@link cancel()}.
 */
class CachingThumbnailProvider final : public ThumbnailProvider,
                                       public std::enable_shared_from_this<CachingThumbnailProvider> {
public:
  static std::shared_ptr<CachingThumbnailProvider> create(
      std::shared_ptr<ThumbnailProvider> provider, ThumbnailCacheConfig config = {});

  ~CachingThumbnailProvider() override;

  CachingThumbnailProvider(const CachingThumbnailProvider&) = delete;
  CachingThumbnailProvider& operator=(const CachingThumbnailProvider&) = delete;

  void requestThumbnail(int64_t positionUs, Callback callback) override;

  /**
   * Warms the bucket containing @p positionUs and its configured neighbours without a user
   * callback. A later request for the same bucket is served from the cache.
   */
  void prefetch(int64_t positionUs);

  void cancel() override;

  [[nodiscard]] ThumbnailCacheStats stats() const;

  /** The number of decoded thumbnails currently cached. */
  [[nodiscard]] size_t cachedCount() const;

  /** Drops the cached frames without cancelling the wrapped provider. */
  void clearCache();

private:
  CachingThumbnailProvider(std::shared_ptr<ThumbnailProvider> provider,
                           ThumbnailCacheConfig config);

  struct Impl;
  std::shared_ptr<Impl> mImpl;
};

} // namespace coral

#endif // CORAL_PLAYBACK_THUMBNAIL_CACHE_H
