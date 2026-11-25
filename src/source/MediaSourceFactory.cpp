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
#include <coral/source/MediaSourceFactory.h>

#include <coral/source/ProgressiveMediaSource.h>

#include <datasource/DataSource.h>

#include <utility>

namespace coral {

std::shared_ptr<MediaSource>
DefaultMediaSourceFactory::createMediaSource(const std::shared_ptr<MediaItem>& mediaItem) {
  if (createSource) {
    return createSource(mediaItem);
  }
  // The progressive source demuxes the media with the extractors of the extractor module, which is what
  // reads the containers on both platforms. The default data source reads the media of the local file
  // system, and the media of an http or an https uri.
  return std::make_shared<ProgressiveMediaSource>(mediaItem, createDefaultDataSourceFactory());
}

std::shared_ptr<MediaSource>
ProgressiveMediaSourceFactory::createMediaSource(const std::shared_ptr<MediaItem>& mediaItem) {
  return std::make_shared<ProgressiveMediaSource>(mediaItem, dataSourceFactory);
}

} // namespace coral
