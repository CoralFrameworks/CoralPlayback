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
#ifndef CORAL_PLAYBACK_SOURCE_MEDIA_ITEM_H
#define CORAL_PLAYBACK_SOURCE_MEDIA_ITEM_H

#include <media/Definitions.h>
#include <media/Types.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace coral {

/** Representation of a media item. */
class MediaItem {
public:
  /**
   * Creates a {@link MediaItem} for the given url.
   *
   * @param url The url.
   * @return An {@link MediaItem} for the given url.
   */
  static std::shared_ptr<MediaItem> fromUrl(const std::string& url);

  /** Properties for a text track. */
  struct SubtitleConfiguration {

    /** The url to the subtitle file. */
    const std::string url;

    /** The MIME type. */
    const std::string mimeType;

    /** The language. */
    const std::string language;

    /** The label. */
    const std::string label;

    /**
     * The ID of the subtitles. This will be propagated to the {@link Format#id} of the subtitle
     * track created from this configuration.
     */
    const std::string id;

    /** Role flags such as caption/forced/commentary for this side-loaded track. */
    const uint32_t roleFlags;

    /** Selection flags such as default/forced for this side-loaded track. */
    const uint32_t selectionFlags;

    explicit SubtitleConfiguration(std::string url,
        std::string mimeType,
        std::string language,
        std::string label,
        std::string id,
        const uint32_t roleFlags = 0,
        const uint32_t selectionFlags = 0) noexcept
        : url(std::move(url)),
          mimeType(std::move(mimeType)),
          language(std::move(language)),
          label(std::move(label)),
          id(std::move(id)),
          roleFlags(roleFlags),
          selectionFlags(selectionFlags) {}

    SubtitleConfiguration(const SubtitleConfiguration& other) = delete;
    SubtitleConfiguration& operator=(const SubtitleConfiguration& other) = delete;
    SubtitleConfiguration(SubtitleConfiguration&& other) noexcept = default;
    SubtitleConfiguration& operator=(SubtitleConfiguration&& other) noexcept = delete;
  };

  /** Creates an item with the given side-loaded subtitle configurations. */
  static std::shared_ptr<MediaItem> fromUrl(const std::string& url,
    std::vector<SubtitleConfiguration> subtitles);

  /**
   * Creates an item with subtitles and the display duration of an image item.
   *
   * <p>`imageDurationMs` follows ExoPlayer's `MediaItem.Builder.setImageDurationMs`: when it is
   * `Definitions::kTimeUnset` the item has no explicit image duration.
   */
  static std::shared_ptr<MediaItem> fromUrl(const std::string& url,
    std::vector<SubtitleConfiguration> subtitles,
    int64_t imageDurationMs);

  /** Optionally clips the media item to a custom start and end position. */
  struct ClippingConfiguration {
    /** The start position in milliseconds. This is a value larger than or equal to zero. */
    const int64_t startPositionMs;

    /** The start position in microseconds. This is a value larger than or equal to zero. */
    const int64_t startPositionUs;

    /**
     * The end position in milliseconds. This is a value larger than or equal to zero or {@link
     * Definitions::kEndOfSource} to play to the end of the stream.
     */
    const int64_t endPositionMs;

    /**
     * The end position in microseconds. This is a value larger than or equal to zero or {@link
     * Definitions::kTimeEndOfSource} to play to the end of the stream.
     */
    const int64_t endPositionUs;

    /** Sets whether the start point is guaranteed to be a key frame. */
    const bool startsAtKeyFrame;

    explicit ClippingConfiguration(int64_t startPositionUs,
        int64_t endPositionUs,
        bool startsAtKeyFrame) noexcept
        : startPositionUs(startPositionUs),
          startPositionMs(std::round(startPositionUs / Definitions::kMicrosPerMillisecond)),
          endPositionUs(endPositionUs),
          endPositionMs(std::round(endPositionUs / Definitions::kMicrosPerMillisecond)),
          startsAtKeyFrame(startsAtKeyFrame) {
    }

    ClippingConfiguration(const ClippingConfiguration& other) = delete;
    ClippingConfiguration& operator=(const ClippingConfiguration& other) = delete;
  };

  /** Live playback configuration. */
  struct LiveConfiguration {
    /**
     * Target offset from the live edge, in milliseconds, or {@link Definitions::kTimeUnset} to
     * use the media-defined default.
     */
    const int64_t targetOffsetMs;

    /**
     * The minimum allowed offset from the live edge, in milliseconds, or
     * {@link Definitions::kTimeUnset} to use the media-defined default.
     */
    const int64_t minOffsetMs;

    /**
     * The maximum allowed offset from the live edge, in milliseconds, or
     * {@link Definitions::kTimeUnset} to use the media-defined default.
     */
    const int64_t maxOffsetMs;

    /**
     * Minimum factor by which playback can be sped up, or {@link Definitions::kRateUnset}
     * to use the media-defined default.
     */
    const float minPlaybackSpeed;

    /**
     * Maximum factor by which playback can be sped up, or {@link Definitions::kRateUnset}
     * to use the media-defined default.
     */
    const float maxPlaybackSpeed;

    explicit LiveConfiguration(int64_t targetOffsetMs,
        int64_t minOffsetMs,
        int64_t maxOffsetMs,
        float minPlaybackSpeed,
        float maxPlaybackSpeed) noexcept
        : targetOffsetMs(targetOffsetMs),
          minOffsetMs(minOffsetMs),
          maxOffsetMs(maxOffsetMs),
          minPlaybackSpeed(minPlaybackSpeed),
          maxPlaybackSpeed(maxPlaybackSpeed) {}

    LiveConfiguration(const LiveConfiguration& other) = delete;
    LiveConfiguration& operator=(const LiveConfiguration& other) = delete;
  };

  /** Properties for local playback. */
  struct LocalConfiguration {

    /** The url of the MediaItem. */
    const std::string url;

    /**
     * The optional MIME type of the item, or {@code null} if unspecified.
     *
     * The MIME type can be used to disambiguate media items that have a URI which does not allow
     * to infer the actual media type.
     */
    const std::string mimeType;

    /** Optional subtitles to be sideloaded. */
    const std::vector<SubtitleConfiguration> subtitles;

    /** Duration for image assets in milliseconds. */
    const int64_t imageDurationMs;

    explicit LocalConfiguration(std::string url,
        std::string mimeType,
        std::vector<SubtitleConfiguration> subtitles,
        int64_t imageDurationMs) noexcept
        : url(std::move(url)),
          mimeType(std::move(mimeType)),
          subtitles(std::move(subtitles)),
          imageDurationMs(imageDurationMs) {}

    LocalConfiguration(const LocalConfiguration& other) = delete;
    LocalConfiguration& operator=(const LocalConfiguration& other) = delete;
  };

  [[nodiscard]] bool hasLocalConfiguration() const noexcept {
    return localConfiguration != nullptr;
  }

  /** The image duration in milliseconds, or `Definitions::kTimeUnset`. */
  [[nodiscard]] int64_t getImageDurationMs() const noexcept;

private:
  /** Identifies the media item. */
  std::string mediaId;

  /**
   * Optional configuration for local playback. May be {@code null} if shared over process
   * boundaries.
   */
  std::unique_ptr<LocalConfiguration> localConfiguration;

  /** The live playback configuration. */
  std::unique_ptr<LiveConfiguration> liveConfiguration;

  /** The clipping configuration. */
  std::unique_ptr<ClippingConfiguration> clippingConfiguration;

  explicit MediaItem(std::string mediaId,
      std::unique_ptr<LocalConfiguration> localConfiguration,
      std::unique_ptr<LiveConfiguration> liveConfiguration,
      std::unique_ptr<ClippingConfiguration> clippingConfiguration) noexcept
      : mediaId(std::move(mediaId)),
        localConfiguration(std::move(localConfiguration)),
        liveConfiguration(std::move(liveConfiguration)),
        clippingConfiguration(std::move(clippingConfiguration)) {
  }

public:
  [[nodiscard]] std::string getUrl() const {
    if (localConfiguration) {
      return localConfiguration->url;
    }
    return "";
  }

  /** The side-loaded subtitle configurations of the item, empty when it has none. */
  [[nodiscard]] const std::vector<SubtitleConfiguration>& getSubtitles() const {
    static const std::vector<SubtitleConfiguration> kEmpty;
    return localConfiguration ? localConfiguration->subtitles : kEmpty;
  }
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MEDIA_ITEM_H
