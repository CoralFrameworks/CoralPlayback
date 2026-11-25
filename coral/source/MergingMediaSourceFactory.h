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
#ifndef CORAL_PLAYBACK_SOURCE_MERGING_MEDIA_SOURCE_FACTORY_H
#define CORAL_PLAYBACK_SOURCE_MERGING_MEDIA_SOURCE_FACTORY_H

#include <coral/source/MediaSourceFactory.h>

#include <memory>

namespace coral {

/**
 * Creates a {@link MergingMediaSource} for an item that carries side-loaded subtitles.
 *
 * <p>The first source is the item itself, created by the primary factory; every subtitle
 * configuration becomes a {@link ProgressiveMediaSource} of its own file. An item without
 * subtitles returns the primary source unchanged.
 */
class MergingMediaSourceFactory final : public MediaSourceFactory {
public:
  explicit MergingMediaSourceFactory(
      std::shared_ptr<MediaSourceFactory> primaryFactory = nullptr) noexcept;

  std::shared_ptr<MediaSource>
  createMediaSource(const std::shared_ptr<MediaItem>& mediaItem) override;

private:
  std::shared_ptr<MediaSourceFactory> primaryFactory;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MERGING_MEDIA_SOURCE_FACTORY_H
