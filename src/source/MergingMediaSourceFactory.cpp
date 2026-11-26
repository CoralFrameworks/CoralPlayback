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

#include <coral/source/MergingMediaSourceFactory.h>

#include <coral/source/MediaSourceFactory.h>
#include <coral/source/MergingMediaSource.h>
#include <coral/source/ProgressiveMediaSource.h>

namespace coral {

MergingMediaSourceFactory::MergingMediaSourceFactory(
    std::shared_ptr<MediaSourceFactory> primaryFactory) noexcept
    : primaryFactory(primaryFactory != nullptr
                       ? std::move(primaryFactory)
                       : std::make_shared<ProgressiveMediaSourceFactory>()) {
}

std::shared_ptr<MediaSource> MergingMediaSourceFactory::createMediaSource(
    const std::shared_ptr<MediaItem>& mediaItem) {
  if (mediaItem == nullptr) {
    return nullptr;
  }
  auto primary = primaryFactory->createMediaSource(mediaItem);
  const auto& subtitles = mediaItem->getSubtitles();
  if (subtitles.empty()) {
    return primary;
  }

  std::vector<std::shared_ptr<MediaSource>> sources;
  std::vector<SubtitleMetadata> metadata;
  sources.push_back(primary);
  metadata.emplace_back();
  for (const auto& subtitle: subtitles) {
    sources.push_back(std::make_shared<ProgressiveMediaSource>(
        MediaItem::fromUrl(subtitle.url)));
    SubtitleMetadata info;
    info.present = true;
    info.mimeType = subtitle.mimeType;
    info.language = subtitle.language;
    info.label = subtitle.label;
    info.id = subtitle.id;
    info.roleFlags = subtitle.roleFlags;
    info.selectionFlags = subtitle.selectionFlags;
    metadata.push_back(std::move(info));
  }
  return std::make_shared<MergingMediaSource>(std::move(sources), std::move(metadata));
}

} // namespace coral