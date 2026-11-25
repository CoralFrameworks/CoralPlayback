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
#ifndef CORAL_PLAYBACK_SOURCE_MEDIA_LOAD_DATA_H
#define CORAL_PLAYBACK_SOURCE_MEDIA_LOAD_DATA_H

#include <media/Definitions.h>
#include <media/Format.h>

#include <memory>
#include <utility>

namespace coral {

/** Descriptor for data being loaded or selected by a media source. */
struct MediaLoadData {
  /**
   * One of the {@link MediaType} constants if the data corresponds to media of a specific type.
   */
  MediaType mediaType;
  /**
   * The format of the track to which the data belongs. Null if the data does not belong to a
   * specific track.
   */
  std::shared_ptr<MediaFormat> trackFormat;
  /**
   * The start time of the media, or {@link Definitions::kTimeUnset} if the data does not belong to
   * a specific media period.
   */
  int64_t mediaStartTimeMs;
  /**
   * The end time of the media, or {@link Definitions::kTimeUnset} if the data does not belong to a
   * specific media period or the end time is unknown.
   */
  int64_t mediaEndTimeMs;

  MediaLoadData()
      : mediaType(MediaType::kMediaTypeDefault),
        trackFormat(nullptr),
        mediaStartTimeMs(Definitions::kTimeUnset),
        mediaEndTimeMs(Definitions::kTimeUnset) {
  }

  MediaLoadData(const MediaType mediaType,
      std::shared_ptr<MediaFormat> trackFormat,
      const int64_t mediaStartTimeMs,
      const int64_t mediaEndTimeMs) noexcept
      : mediaType(mediaType),
        trackFormat(std::move(trackFormat)),
        mediaStartTimeMs(mediaStartTimeMs),
        mediaEndTimeMs(mediaEndTimeMs) {
  }

  MediaLoadData(const MediaLoadData& other) = default;
  MediaLoadData& operator=(const MediaLoadData& other) = default;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MEDIA_LOAD_DATA_H
