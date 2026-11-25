/*
 * Copyright (C) 2023 The Coral Project
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

#include <coral/source/MediaItem.h>

namespace coral {

std::shared_ptr<MediaItem> MediaItem::fromUrl(const std::string& url) {
  return fromUrl(url, {});
}

std::shared_ptr<MediaItem> MediaItem::fromUrl(const std::string& url,
    std::vector<SubtitleConfiguration> subtitles) {
  return fromUrl(url, std::move(subtitles), Definitions::kTimeUnset);
}

std::shared_ptr<MediaItem> MediaItem::fromUrl(const std::string& url,
    std::vector<SubtitleConfiguration> subtitles,
    const int64_t imageDurationMs) {
  auto localConfiguration =
      std::make_unique<LocalConfiguration>(url, "", std::move(subtitles), imageDurationMs);
  return std::shared_ptr<MediaItem>(
      new MediaItem("", std::move(localConfiguration), nullptr, nullptr));
}

int64_t MediaItem::getImageDurationMs() const noexcept {
  return localConfiguration != nullptr ? localConfiguration->imageDurationMs
                                       : Definitions::kTimeUnset;
}

} // namespace coral
