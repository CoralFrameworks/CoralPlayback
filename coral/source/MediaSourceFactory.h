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
#ifndef CORAL_PLAYBACK_SOURCE_MEDIA_SOURCE_FACTORY_H
#define CORAL_PLAYBACK_SOURCE_MEDIA_SOURCE_FACTORY_H

#include <coral/source/MediaSource.h>
#include <coral/source/ProgressiveMediaPeriod.h>

#include <functional>
#include <memory>
#include <utility>

namespace coral {

/**
 * Creates the {@link MediaSource} of a {@link MediaItem}.
 *
 * <p>Mirrors ExoPlayer's {@code MediaSource.Factory}, which is what the player uses to turn the
 * media item an application set into the source it plays. It is what lets the player support
 * several kinds of media without knowing about them: a factory that reads progressive containers,
 * one that reads an adaptive stream, and one that reads the media with the platform decoders are
 * interchangeable.
 */
class MediaSourceFactory {
public:
  virtual ~MediaSourceFactory() = default;

  /**
   * Creates the media source that provides the media of {@code mediaItem}.
   *
   * @param mediaItem The media item to play. Must not be null.
   *
   * @return A media source. Never null.
   */
  virtual std::shared_ptr<MediaSource>
  createMediaSource(const std::shared_ptr<MediaItem>& mediaItem) = 0;
};

/**
 * The factory the player uses by default.
 *
 * <p>It creates an {@link ExtractorMediaSource}, i.e. it reads the media with the extractor and the
 * decoders the platform provides, which is what the player has always done. Replacing it with
 * {@link ProgressiveMediaSourceFactory} makes the player use the extractors of `libs/extractor`
 * instead.
 *
 * <p>The factory can also be built around a closure, which keeps an application that only needs to
 * choose between the sources from having to define a class.
 */
class DefaultMediaSourceFactory final : public MediaSourceFactory {
public:
  /** Creates the factory that reads the media with the platform extractor. */
  DefaultMediaSourceFactory() = default;

  /**
   * Creates the factory that calls {@code createSource} for every item.
   *
   * @param createSource The closure that creates the source of an item.
   */
  explicit DefaultMediaSourceFactory(
      std::function<std::shared_ptr<MediaSource>(const std::shared_ptr<MediaItem>&)> createSource)
      : createSource(std::move(createSource)) {
  }

  std::shared_ptr<MediaSource>
  createMediaSource(const std::shared_ptr<MediaItem>& mediaItem) override;

private:
  std::function<std::shared_ptr<MediaSource>(const std::shared_ptr<MediaItem>&)> createSource;
};

/**
 * A {@link MediaSourceFactory} that reads the media with the extractors of `libs/extractor`.
 *
 * <p>Every item is played by a {@link ProgressiveMediaSource}, which demuxes the container on its
 * own instead of handing it to the platform extractor.
 */
class ProgressiveMediaSourceFactory final : public MediaSourceFactory {
public:
  /**
   * @param dataSourceFactory The factory of the data source of the sources it creates, or null to
   *                          read the media of the local file system.
   */
  explicit ProgressiveMediaSourceFactory(
      ProgressiveMediaPeriod::DataSourceFactory dataSourceFactory = nullptr) noexcept
      : dataSourceFactory(std::move(dataSourceFactory)) {
  }

  std::shared_ptr<MediaSource>
  createMediaSource(const std::shared_ptr<MediaItem>& mediaItem) override;

private:
  ProgressiveMediaPeriod::DataSourceFactory dataSourceFactory;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MEDIA_SOURCE_FACTORY_H
