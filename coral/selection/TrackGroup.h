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

#ifndef CORAL_SELECTION_TRACK_GROUP_H
#define CORAL_SELECTION_TRACK_GROUP_H

#include <media/Format.h>

#include <memory>
#include <vector>

namespace coral {

/**
 * A class whose instances describe a group of tracks in a media item.
 *
 * A TrackGroup contains one or many trackIds for tracks with the same media type.
 * For example, a Subtitle track group can have multi-language tracks.
 */
class TrackGroup final {
public:
  explicit TrackGroup(const MediaType trackType, const std::shared_ptr<MediaFormat>& format) noexcept
    : mMediaType(trackType) {
    formats.push_back(format);
  }

  explicit TrackGroup(const MediaType trackType, const std::vector<std::shared_ptr<MediaFormat>>& formats) noexcept
    : mMediaType(trackType),
      formats(formats) {
  }

  ~TrackGroup() = default;

  TrackGroup(const TrackGroup& other) = default;
  TrackGroup& operator=(const TrackGroup& other) = default;

  /**
   * Returns the number of tracks in the group.
   */
  [[nodiscard]] int length() const { return static_cast<int>(formats.size()); }

  /**
   * Returns the IDs of all the tracks in the group.
   */
  [[nodiscard]] std::vector<int> getTrackIds() const;

  /**
   * Returns all the formats in the group.
   */
  [[nodiscard]] const std::vector<std::shared_ptr<MediaFormat>>& getFormats() const {
    return formats;
  }

  /**
   * Returns the format of the track at a given index.
   *
   * @param index The index of the track.
   * @return The track's format.
   */
  [[nodiscard]] const std::shared_ptr<MediaFormat>& getFormat(int index) const;

  /**
   * Returns the media type of the track group
   */
  [[nodiscard]] MediaType mediaType() const {
    return mMediaType;
  }

  /**
   * Returns the index of the track with the given format in the group. The
   * format is located by identity so, for example, {@code
   * group.indexOf(group.getFormat(index)) == index} even if multiple tracks
   * have formats that contain the same values.
   *
   * @param format The media format.
   *
   * @return The index of the track, or {@link Definitions::kIndexUnset} if no such track exists.
   */
  [[nodiscard]] int indexOf(const std::shared_ptr<MediaFormat>& format) const;

  friend bool operator!=(const TrackGroup& lhs, const TrackGroup& rhs) {
    return !(lhs == rhs);
  }

  friend bool operator==(const TrackGroup& lhs, const TrackGroup& rhs) {
    return std::ranges::equal(lhs.formats, rhs.formats);
  }

private:
  //!< The media type of this track group.
  MediaType mMediaType;

  //!< The formats of all the tracks in the group.
  std::vector<std::shared_ptr<MediaFormat>> formats;
};

/**
 * An array of the TrackGroup
 */
class TrackGroupArray final {
public:
  TrackGroupArray() = default;

  explicit TrackGroupArray(const std::shared_ptr<TrackGroup>& group) noexcept {
    trackGroups.push_back(group);
  }

  explicit TrackGroupArray(
    const std::vector<std::shared_ptr<TrackGroup>>& groups) noexcept
    : trackGroups(groups) {
  }

  ~TrackGroupArray();

  TrackGroupArray(const TrackGroupArray& other) = default;

  TrackGroupArray& operator=(const TrackGroupArray& other) = default;

  [[nodiscard]] int length() const { return (int) trackGroups.size(); }

  [[nodiscard]] const std::shared_ptr<TrackGroup>& get(int index) const {
    return trackGroups[index];
  }

  [[nodiscard]] int indexOf(const std::shared_ptr<TrackGroup>& group) const;

private:
  std::vector<std::shared_ptr<TrackGroup>> trackGroups;
};

} // namespace coral

#endif // CORAL_SELECTION_TRACK_GROUP_H
