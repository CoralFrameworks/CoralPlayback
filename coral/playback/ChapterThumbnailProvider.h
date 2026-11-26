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

#ifndef CORAL_PLAYBACK_CHAPTER_THUMBNAIL_PROVIDER_H
#define CORAL_PLAYBACK_CHAPTER_THUMBNAIL_PROVIDER_H

#include <coral/playback/Chapters.h>
#include <coral/playback/Thumbnail.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace coral {

/** A chapter together with the thumbnail decoded at its start. */
struct ChapterThumbnail {
  Chapter chapter;
  Thumbnail thumbnail;
};

/**
 * Requests one thumbnail per chapter at the chapter's start position.
 *
 * <p>The provider is a convenience for a chapter list or a seek bar; it keeps the chapter order and
 * invokes one callback when every chapter has an answer (or with a null frame when the wrapped
 * provider could not decode one). A later request cancels the previous batch, and
 * {@link cancel()} drops all outstanding callbacks.
 */
class ChapterThumbnailProvider final {
public:
  using Callback = std::function<void(std::vector<ChapterThumbnail>)>;

  static std::shared_ptr<ChapterThumbnailProvider> create(
      std::shared_ptr<ThumbnailProvider> provider, std::vector<Chapter> chapters);

  ~ChapterThumbnailProvider();

  ChapterThumbnailProvider(const ChapterThumbnailProvider&) = delete;
  ChapterThumbnailProvider& operator=(const ChapterThumbnailProvider&) = delete;

  /** Starts a batch; a previous batch is cancelled if it is still in flight. */
  void requestThumbnails(Callback callback);

  void cancel();

private:
  ChapterThumbnailProvider(std::shared_ptr<ThumbnailProvider> provider,
                           std::vector<Chapter> chapters);

  struct State;
  std::shared_ptr<State> mState;
};

} // namespace coral

#endif // CORAL_PLAYBACK_CHAPTER_THUMBNAIL_PROVIDER_H
