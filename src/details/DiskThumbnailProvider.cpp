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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace coral {
namespace {

constexpr std::array<char, 4> kMagic = {'C', 'T', 'H', '1'};
constexpr uint32_t kMaxDimension = 16'384;

uint64_t fnv1a(const std::string& value) {
  uint64_t hash = 1469598103934665603ull;
  for (const unsigned char byte : value) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

int64_t normalizeBucket(const int64_t bucketUs) {
  return bucketUs > 0 ? bucketUs : 500'000;
}

bool readExact(std::ifstream& stream, void* data, const size_t size) {
  stream.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
  return stream.good() || stream.gcount() == static_cast<std::streamsize>(size);
}

std::shared_ptr<PixelBuffer> loadFrame(const std::filesystem::path& path, const int64_t positionUs) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return nullptr;
  }
  std::array<char, 4> magic = {};
  uint32_t width = 0;
  uint32_t height = 0;
  int64_t storedPositionUs = 0;
  uint32_t byteCount = 0;
  if (!readExact(stream, magic.data(), magic.size()) || magic != kMagic ||
      !readExact(stream, &width, sizeof(width)) || !readExact(stream, &height, sizeof(height)) ||
      !readExact(stream, &storedPositionUs, sizeof(storedPositionUs)) ||
      !readExact(stream, &byteCount, sizeof(byteCount)) || width == 0 || height == 0 ||
      width > kMaxDimension || height > kMaxDimension ||
      byteCount != width * height * 4) {
    return nullptr;
  }
  auto frame = std::make_shared<BgraPixelBuffer>(static_cast<int>(width), static_cast<int>(height));
  if (frame->size() != byteCount ||
      !readExact(stream, frame->data(), frame->size())) {
    return nullptr;
  }
  static_cast<void>(storedPositionUs);
  static_cast<void>(positionUs);
  return PixelBuffer::fromAVPixelBuffer(frame);
}

bool saveFrame(const std::filesystem::path& path, const Thumbnail& thumbnail) {
  if (thumbnail.frame == nullptr) {
    return false;
  }
  if (thumbnail.frame->format() != PixelFormat::kFormatBGRA) {
    return false;
  }
  PixelBufferPlane planes[4] = {};
  uint32_t planeCount = 0;
  if (!thumbnail.frame->lock(PixelBufferLockMode::Read, planes, &planeCount) ||
      planeCount != 1 || planes[0].data == nullptr || planes[0].rowBytes <= 0) {
    return false;
  }
  const int width = thumbnail.frame->width();
  const int height = thumbnail.frame->height();
  const size_t expected = static_cast<size_t>(width) * height * 4;
  if (width <= 0 || height <= 0 || static_cast<size_t>(planes[0].rowBytes) !=
      static_cast<size_t>(width) * 4) {
    thumbnail.frame->unlock();
    return false;
  }

  std::filesystem::path temporary = path;
  temporary += ".tmp";
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  if (!stream) {
    thumbnail.frame->unlock();
    return false;
  }
  const uint32_t byteCount = static_cast<uint32_t>(expected);
  stream.write(kMagic.data(), kMagic.size());
  stream.write(reinterpret_cast<const char*>(&width), sizeof(uint32_t));
  stream.write(reinterpret_cast<const char*>(&height), sizeof(uint32_t));
  stream.write(reinterpret_cast<const char*>(&thumbnail.positionUs), sizeof(int64_t));
  stream.write(reinterpret_cast<const char*>(&byteCount), sizeof(byteCount));
  stream.write(reinterpret_cast<const char*>(planes[0].data), byteCount);
  stream.close();
  thumbnail.frame->unlock();
  if (!stream) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
  }
  if (error) {
    std::filesystem::remove(temporary, error);
    return false;
  }
  return true;
}

void prune(const std::filesystem::path& directory, const size_t maxFiles) {
  if (maxFiles == 0) {
    return;
  }
  std::error_code error;
  std::vector<std::filesystem::directory_entry> files;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
    if (entry.is_regular_file() && entry.path().extension() == ".thumb") {
      files.push_back(entry);
    }
  }
  if (error || files.size() <= maxFiles) {
    return;
  }
  std::sort(files.begin(), files.end(), [](const auto& lhs, const auto& rhs) {
    std::error_code lhsError;
    std::error_code rhsError;
    return std::filesystem::last_write_time(lhs, lhsError) <
           std::filesystem::last_write_time(rhs, rhsError);
  });
  for (size_t index = 0; index + maxFiles < files.size(); ++index) {
    std::filesystem::remove(files[index], error);
    error.clear();
  }
}

} // namespace

struct DiskThumbnailProvider::State {
  State(std::string key, std::shared_ptr<ThumbnailProvider> source,
        DiskThumbnailCacheConfig tuning)
      : mediaKey(std::move(key)), provider(std::move(source)), config(std::move(tuning)) {
    config.bucketUs = normalizeBucket(config.bucketUs);
    directory = config.directory;
    hash = fnv1a(mediaKey);
  }

  std::filesystem::path pathFor(const int64_t bucket) const {
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << hash << "-"
         << std::dec << bucket << ".thumb";
    return directory / name.str();
  }

  int64_t bucketOf(const int64_t positionUs) const {
    return positionUs <= 0 ? 0 : positionUs / config.bucketUs;
  }

  int64_t positionOf(const int64_t bucket) const {
    return bucket * config.bucketUs + config.bucketUs / 2;
  }

  void finish(const int64_t bucket, const uint64_t requestGeneration, Thumbnail thumbnail,
              const bool writeDisk) {
    std::vector<Callback> callbacks;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      if (requestGeneration != generation) {
        return;
      }
      const auto pendingIt = pending.find(bucket);
      if (pendingIt == pending.end()) {
        return;
      }
      callbacks = std::move(pendingIt->second);
      pending.erase(pendingIt);
    }
    if (writeDisk && thumbnail.frame != nullptr) {
      saveFrame(pathFor(bucket), thumbnail);
      prune(directory, config.maxFiles);
    }
    for (auto& callback : callbacks) {
      if (callback) {
        callback(thumbnail);
      }
    }
  }

  std::string mediaKey;
  std::shared_ptr<ThumbnailProvider> provider;
  DiskThumbnailCacheConfig config;
  std::filesystem::path directory;
  uint64_t hash = 0;
  std::mutex mutex;
  std::unordered_map<int64_t, std::vector<Callback>> pending;
  uint64_t generation = 0;
};

DiskThumbnailProvider::DiskThumbnailProvider(std::string mediaKey,
                                             std::shared_ptr<ThumbnailProvider> provider,
                                             DiskThumbnailCacheConfig config)
    : mState(std::make_shared<State>(std::move(mediaKey), std::move(provider), std::move(config))) {
}

std::shared_ptr<DiskThumbnailProvider> DiskThumbnailProvider::create(
    std::string mediaKey, std::shared_ptr<ThumbnailProvider> provider,
    DiskThumbnailCacheConfig config) {
  if (mediaKey.empty() || provider == nullptr || config.directory.empty()) {
    return nullptr;
  }
  std::error_code error;
  std::filesystem::create_directories(config.directory, error);
  if (error) {
    return nullptr;
  }
  return std::shared_ptr<DiskThumbnailProvider>(
      new DiskThumbnailProvider(std::move(mediaKey), std::move(provider), std::move(config)));
}

DiskThumbnailProvider::~DiskThumbnailProvider() {
  cancel();
}

void DiskThumbnailProvider::requestThumbnail(const int64_t positionUs, Callback callback) {
  const int64_t bucket = mState->bucketOf(positionUs);
  if (auto cached = loadFrame(mState->pathFor(bucket), positionUs); cached != nullptr) {
    Thumbnail thumbnail;
    thumbnail.positionUs = positionUs;
    thumbnail.frame = std::move(cached);
    if (callback) {
      callback(std::move(thumbnail));
    }
    return;
  }

  uint64_t requestGeneration = 0;
  bool start = false;
  {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    requestGeneration = mState->generation;
    auto& waiting = mState->pending[bucket];
    if (callback) {
      waiting.push_back(std::move(callback));
    }
    if (!waiting.empty() && mState->pending[bucket].size() == 1) {
      start = true;
    }
  }
  if (!start) {
    return;
  }
  auto self = shared_from_this();
  mState->provider->requestThumbnail(mState->positionOf(bucket),
    [self, bucket, requestGeneration](Thumbnail thumbnail) {
      self->mState->finish(bucket, requestGeneration, std::move(thumbnail), true);
    });
}

void DiskThumbnailProvider::cancel() {
  {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    ++mState->generation;
    mState->pending.clear();
  }
  mState->provider->cancel();
}

} // namespace coral
