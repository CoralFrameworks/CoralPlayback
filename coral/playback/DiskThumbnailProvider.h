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

#ifndef CORAL_PLAYBACK_DISK_THUMBNAIL_PROVIDER_H
#define CORAL_PLAYBACK_DISK_THUMBNAIL_PROVIDER_H

#include <coral/playback/Thumbnail.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace coral {

/** Settings for {@link DiskThumbnailProvider}. */
struct DiskThumbnailCacheConfig {
  /** Directory that stores the thumbnails. It is created when missing. */
  std::string directory;

  /**
   * Optional media identity, e.g. the file's size and modification time or an HTTP ETag. It is
   * mixed into the cache key, so changing the media does not reuse thumbnails from the old bytes.
   * Empty keeps the key at {@code mediaKey} alone.
   */
  std::string mediaFingerprint;

  /** Width of the cache bucket in microseconds. Zero selects 500 ms. */
  int64_t bucketUs = 500'000;

  /** Maximum number of `.thumb` files kept; the oldest are removed after a write. Zero keeps all. */
  size_t maxFiles = 512;

  /** Maximum total bytes of `.thumb` files kept. Zero keeps all. */
  uint64_t maxBytes = 0;
};

/**
 * A disk cache decorator around a {@link ThumbnailProvider}.
 *
 * <p>Each media key and time bucket maps to one BGRA file. A request first tries the file, then the
 * wrapped provider; a decoded CPU-readable BGRA frame is written atomically for the next player
 * session. The provider coalesces concurrent requests for one bucket, and `cancel()` drops the
 * outstanding callbacks and cancels the wrapped provider.
 *
 * <p>The cache only stores frames whose {@code PixelBuffer} is BGRA and CPU-readable. A GPU-only
 * frame is passed through without being written.
 */
class DiskThumbnailProvider final : public ThumbnailProvider,
                                    public std::enable_shared_from_this<DiskThumbnailProvider> {
public:
  static std::shared_ptr<DiskThumbnailProvider> create(
      std::string mediaKey, std::shared_ptr<ThumbnailProvider> provider,
      DiskThumbnailCacheConfig config);

  ~DiskThumbnailProvider() override;

  DiskThumbnailProvider(const DiskThumbnailProvider&) = delete;
  DiskThumbnailProvider& operator=(const DiskThumbnailProvider&) = delete;

  void requestThumbnail(int64_t positionUs, Callback callback) override;

  void cancel() override;

private:
  DiskThumbnailProvider(std::string mediaKey, std::shared_ptr<ThumbnailProvider> provider,
                        DiskThumbnailCacheConfig config);

  struct State;
  std::shared_ptr<State> mState;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DISK_THUMBNAIL_PROVIDER_H
