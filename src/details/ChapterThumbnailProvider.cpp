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

#include <coral/playback/ChapterThumbnailProvider.h>

#include <atomic>
#include <mutex>
#include <utility>

namespace coral {

struct ChapterThumbnailProvider::State {
  State(std::shared_ptr<ThumbnailProvider> source, std::vector<Chapter> list)
      : provider(std::move(source)), chapters(std::move(list)) {
  }

  std::shared_ptr<ThumbnailProvider> provider;
  const std::vector<Chapter> chapters;
  std::mutex mutex;
  uint64_t generation = 0;
  bool cancelled = false;
};

ChapterThumbnailProvider::ChapterThumbnailProvider(std::shared_ptr<ThumbnailProvider> provider,
                                                   std::vector<Chapter> chapters)
    : mState(std::make_shared<State>(std::move(provider), std::move(chapters))) {
}

std::shared_ptr<ChapterThumbnailProvider> ChapterThumbnailProvider::create(
    std::shared_ptr<ThumbnailProvider> provider, std::vector<Chapter> chapters) {
  if (provider == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<ChapterThumbnailProvider>(
      new ChapterThumbnailProvider(std::move(provider), std::move(chapters)));
}

ChapterThumbnailProvider::~ChapterThumbnailProvider() {
  cancel();
}

void ChapterThumbnailProvider::requestThumbnails(Callback callback) {
  uint64_t requestGeneration = 0;
  std::vector<Chapter> chapters;
  {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    requestGeneration = ++mState->generation;
    mState->cancelled = false;
    chapters = mState->chapters;
  }
  if (chapters.empty()) {
    if (callback) {
      callback({});
    }
    return;
  }

  auto results = std::make_shared<std::vector<ChapterThumbnail>>(chapters.size());
  auto remaining = std::make_shared<std::atomic<size_t>>(chapters.size());
  auto completed = std::make_shared<std::atomic_bool>(false);
  for (size_t index = 0; index < chapters.size(); ++index) {
    (*results)[index].chapter = chapters[index];
    const int64_t positionUs = chapters[index].startUs;
    mState->provider->requestThumbnail(positionUs,
      [state = mState, results, remaining, completed, callback, requestGeneration, index,
       positionUs](Thumbnail thumbnail) {
        {
          const std::lock_guard<std::mutex> lock(state->mutex);
          if (state->cancelled || requestGeneration != state->generation) {
            return;
          }
        }
        (*results)[index].thumbnail = std::move(thumbnail);
        if (remaining->fetch_sub(1, std::memory_order_acq_rel) != 1) {
          return;
        }
        if (completed->exchange(true, std::memory_order_acq_rel)) {
          return;
        }
        std::vector<ChapterThumbnail> output = *results;
        if (callback) {
          callback(std::move(output));
        }
      });
  }
}

void ChapterThumbnailProvider::cancel() {
  const std::lock_guard<std::mutex> lock(mState->mutex);
  ++mState->generation;
  mState->cancelled = true;
  mState->provider->cancel();
}

} // namespace coral
