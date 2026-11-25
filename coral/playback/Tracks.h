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

#ifndef CORAL_PLAYBACK_TRACKS_H
#define CORAL_PLAYBACK_TRACKS_H

#include <media/Format.h>
#include <media/Types.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace coral {

/** The kind of media a track carries. */
enum class TrackType : uint8_t {
  Video = 0,
  Audio = 1,
  Text = 2,
  Metadata = 3,
  Unknown = 4,
};

/** One selectable track and the state the player currently has for it. */
struct Track {
  /** The kind of media the track carries. */
  TrackType type = TrackType::Unknown;

  /** The renderer the track is mapped to. It is the index a selection names. */
  int rendererIndex = 0;

  /** The index of the track group inside the renderer's group array. */
  int groupIndex = 0;

  /** The index of the track inside its group. */
  int trackIndex = 0;

  /** Whether the selector currently selects this track. */
  bool selected = false;

  /** Whether the renderer can play the track. An unsupported track is listed but not selectable. */
  bool supported = false;

  /** The format of the track. It carries language, codecs, resolution and bitrate when known. */
  std::shared_ptr<MediaFormat> format;
};

/** A group of tracks that share a renderer and a container-level grouping. */
struct TrackGroupInfo {
  /** The kind of media the group carries. */
  TrackType type = TrackType::Unknown;

  /** The renderer the group is mapped to. */
  int rendererIndex = 0;

  /** The index of the group inside the renderer's group array. */
  int groupIndex = 0;

  /** Whether any track in the group is currently selected. */
  bool selected = false;

  /** The tracks of the group, in the order the container or manifest describes them. */
  std::vector<Track> tracks;
};

/**
 * A snapshot of the tracks of the current media item.
 *
 * <p>This mirrors the part of ExoPlayer's {@code Tracks} a player menu needs: the groups, their
 * tracks, and which one is selected. The snapshot is rebuilt when the player changes selection or
 * starts a new period; a listener receives the new value through
 * {@code Player.EventListener::onTracksChanged}.
 */
struct Tracks {
  /** The groups of the current period, ordered by renderer and then by group. */
  std::vector<TrackGroupInfo> groups;

  /** @return every track of every group, in the same order as the groups. */
  [[nodiscard]] std::vector<Track> allTracks() const {
    std::vector<Track> tracks;
    for (const auto& group : groups) {
      tracks.insert(tracks.end(), group.tracks.begin(), group.tracks.end());
    }
    return tracks;
  }

  /** @return the selected tracks, which is normally one per enabled renderer. */
  [[nodiscard]] std::vector<Track> selectedTracks() const {
    std::vector<Track> selected;
    for (const auto& group : groups) {
      for (const auto& track : group.tracks) {
        if (track.selected) {
          selected.push_back(track);
        }
      }
    }
    return selected;
  }

  [[nodiscard]] bool isEmpty() const noexcept {
    return groups.empty();
  }
};

/** @return the type of a media format, or TrackType::Unknown when it carries none. */
[[nodiscard]] inline TrackType trackTypeOf(const MediaType type) noexcept {
  switch (type) {
    case MediaType::kMediaTypeVideo:
      return TrackType::Video;
    case MediaType::kMediaTypeAudio:
      return TrackType::Audio;
    case MediaType::kMediaTypeSubtitle:
      return TrackType::Text;
    case MediaType::kMediaTypeMetadata:
      return TrackType::Metadata;
    default:
      return TrackType::Unknown;
  }
}

} // namespace coral

#endif // CORAL_PLAYBACK_TRACKS_H
