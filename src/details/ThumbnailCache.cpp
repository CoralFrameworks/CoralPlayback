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

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace coral {

namespace {

int64_t normalizeBucket(const int64_t bucketUs) {
  return bucketUs > 0 ? bucketUs : 500'000;
}

ThumbnailCacheConfig normalize(const ThumbnailCacheConfig& config) {
  ThumbnailCacheConfig result = config;
  result.bucketUs = normalizeBucket(config.bucketUs);
  if (result.prefetchStepUs <= 0) {
    result.prefetchStepUs = result.bucketUs;
  }
  result.prefetchStepUs = std::max(result.prefetchStepUs, result.bucketUs);
  result.prefetchBefore = std::max(result.prefetchBefore, 0);
  result.prefetchAfter = std::max(result.prefetchAfter, 0);
  return result;
}

} // namespace

struct CachingThumbnailProvider::Impl : public std::enable_shared_from_this<CachingThumbnailProvider::Impl> {
  Impl(std::shared_ptr<ThumbnailProvider> source, ThumbnailCacheConfig tuning)
      : provider(std::move(source)), config(normalize(tuning)) {
  }

  struct Entry {
    Thumbnail thumbnail;
    uint64_t lastUse = 0;
  };

  struct Pending {
    std::vector<Callback> callbacks;
    bool prefetch = false;
  };

  int64_t bucketOf(const int64_t positionUs) const {
    if (positionUs <= 0) {
      return 0;
    }
    return positionUs / config.bucketUs;
  }

  int64_t positionOf(const int64_t bucket) const {
    return bucket * config.bucketUs + config.bucketUs / 2;
  }

  void evictLocked() {
    while (cache.size() > config.capacity) {
      auto oldest = cache.begin();
      for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (it->second.lastUse < oldest->second.lastUse) {
          oldest = it;
        }
      }
      cache.erase(oldest);
    }
  }

  void finish(const int64_t bucket, const uint64_t requestGeneration, Thumbnail thumbnail) {
    std::vector<Callback> callbacks;
    bool wasPrefetch = false;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      if (requestGeneration != generation) {
        return;
      }
      if (sourceBusyBucket == bucket) {
        sourceBusy = false;
        sourceBusyBucket = -1;
      }
      const auto pendingIt = pending.find(bucket);
      if (pendingIt == pending.end()) {
        // A stale callback for a bucket that was cancelled or removed; continue with the queue.
      } else {
        wasPrefetch = pendingIt->second.prefetch;
        callbacks = std::move(pendingIt->second.callbacks);
        pending.erase(pendingIt);
        if (thumbnail.frame != nullptr) {
          Entry entry;
          entry.thumbnail = thumbnail;
          entry.lastUse = ++tick;
          cache[bucket] = std::move(entry);
          evictLocked();
        } else if (wasPrefetch) {
          failedPrefetches.insert(bucket);
        }
        if (wasPrefetch) {
          ++stats.prefetches;
        }
      }
    }
    for (auto& callback : callbacks) {
      if (callback) {
        callback(thumbnail);
      }
    }
    startNext();
  }

  void startNext() {
    int64_t bucket = 0;
    uint64_t requestGeneration = 0;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      if (sourceBusy || queue.empty()) {
        return;
      }
      while (!queue.empty()) {
        bucket = queue.front();
        queue.pop_front();
        if (pending.find(bucket) != pending.end()) {
          break;
        }
      }
      if (pending.find(bucket) == pending.end()) {
        return;
      }
      sourceBusy = true;
      sourceBusyBucket = bucket;
      requestGeneration = generation;
    }
    auto self = shared_from_this();
    provider->requestThumbnail(positionOf(bucket),
      [self, bucket, requestGeneration](Thumbnail thumbnail) {
        self->finish(bucket, requestGeneration, std::move(thumbnail));
      });
  }

  void enqueueLocked(const int64_t bucket, Callback callback, const bool prefetch) {
    auto& waiting = pending[bucket];
    if (callback) {
      waiting.callbacks.push_back(std::move(callback));
    }
    if (!prefetch) {
      waiting.prefetch = false;
    }
    // User requests go ahead of every queued prefetch. A bucket that is already in flight stays
    // where it is; its result will be shared with the new callback.
    if (sourceBusyBucket != bucket) {
      const auto queued = std::find(queue.begin(), queue.end(), bucket);
      if (queued != queue.end()) {
        queue.erase(queued);
      }
      if (prefetch) {
        queue.push_back(bucket);
      } else {
        queue.push_front(bucket);
      }
    }
  }

  void requestInternal(const int64_t positionUs, Callback callback, const bool prefetch,
                       const bool allowPrefetch) {
    const int64_t bucket = bucketOf(positionUs);
    Thumbnail cachedThumbnail;
    bool servedFromCache = false;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      if (config.capacity > 0) {
        const auto cached = cache.find(bucket);
        if (cached != cache.end()) {
          cached->second.lastUse = ++tick;
          ++stats.hits;
          cachedThumbnail = cached->second.thumbnail;
          servedFromCache = true;
        }
      }
      if (!servedFromCache) {
        ++stats.misses;
        if (pending.find(bucket) != pending.end()) {
          ++stats.coalesced;
        }
        enqueueLocked(bucket, std::move(callback), prefetch);
        if (allowPrefetch && !prefetch) {
          schedulePrefetchLocked(bucket);
        }
      }
    }
    if (servedFromCache) {
      if (callback) {
        callback(cachedThumbnail);
      }
      return;
    }
    startNext();
  }

  void schedulePrefetchLocked(const int64_t requestedBucket) {
    for (int offset = -config.prefetchBefore; offset <= config.prefetchAfter; ++offset) {
      if (offset == 0) {
        continue;
      }
      const int64_t bucket =
        requestedBucket + offset * (config.prefetchStepUs / config.bucketUs);
      if (bucket < 0) {
        continue;
      }
      if (config.capacity > 0 && cache.find(bucket) != cache.end()) {
        continue;
      }
      if (pending.find(bucket) != pending.end() ||
          failedPrefetches.find(bucket) != failedPrefetches.end()) {
        continue;
      }
      pending[bucket].prefetch = true;
      queue.push_back(bucket);
    }
  }

  std::shared_ptr<ThumbnailProvider> provider;
  ThumbnailCacheConfig config;
  mutable std::mutex mutex;
  std::unordered_map<int64_t, Entry> cache;
  std::unordered_map<int64_t, Pending> pending;
  std::set<int64_t> failedPrefetches;
  std::deque<int64_t> queue;
  bool sourceBusy = false;
  int64_t sourceBusyBucket = -1;
  uint64_t tick = 0;
  uint64_t generation = 0;
  ThumbnailCacheStats stats;
};

CachingThumbnailProvider::CachingThumbnailProvider(std::shared_ptr<ThumbnailProvider> provider,
                                                   ThumbnailCacheConfig config)
    : mImpl(std::make_shared<Impl>(std::move(provider), config)) {
}

std::shared_ptr<CachingThumbnailProvider> CachingThumbnailProvider::create(
    std::shared_ptr<ThumbnailProvider> provider, ThumbnailCacheConfig config) {
  if (provider == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<CachingThumbnailProvider>(
      new CachingThumbnailProvider(std::move(provider), config));
}

CachingThumbnailProvider::~CachingThumbnailProvider() = default;

void CachingThumbnailProvider::requestThumbnail(const int64_t positionUs, Callback callback) {
  mImpl->requestInternal(positionUs, std::move(callback), false, true);
}

void CachingThumbnailProvider::prefetch(const int64_t positionUs) {
  mImpl->requestInternal(positionUs, nullptr, true, true);
}

void CachingThumbnailProvider::cancel() {
  {
    const std::lock_guard<std::mutex> lock(mImpl->mutex);
    ++mImpl->generation;
    mImpl->pending.clear();
    mImpl->queue.clear();
    mImpl->sourceBusy = false;
    mImpl->sourceBusyBucket = -1;
    mImpl->cache.clear();
    mImpl->failedPrefetches.clear();
  }
  if (mImpl->provider != nullptr) {
    mImpl->provider->cancel();
  }
}

ThumbnailCacheStats CachingThumbnailProvider::stats() const {
  const std::lock_guard<std::mutex> lock(mImpl->mutex);
  return mImpl->stats;
}

size_t CachingThumbnailProvider::cachedCount() const {
  const std::lock_guard<std::mutex> lock(mImpl->mutex);
  return mImpl->cache.size();
}

void CachingThumbnailProvider::clearCache() {
  const std::lock_guard<std::mutex> lock(mImpl->mutex);
  mImpl->cache.clear();
  mImpl->failedPrefetches.clear();
}

} // namespace coral
