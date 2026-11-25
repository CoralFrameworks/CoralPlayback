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

#ifndef CORAL_PLAYBACK_SELECTION_MAPPING_TRACK_SELECTOR_H
#define CORAL_PLAYBACK_SELECTION_MAPPING_TRACK_SELECTOR_H

#include <coral/selection/TrackSelector.h>

#include <algorithm>
#include <vector>

namespace coral {

/**
 * Provides mapped track information for each renderer.
 */
class MappedTrackInfo final : public TrackInfo {
public:
  /** The renderer does not have any associated tracks. */
  static constexpr int RENDERER_SUPPORT_NO_TRACKS = 0;
  /**
   * The renderer has tracks mapped to it, but all are unsupported. In other
   * words, {@link #getTrackSupport(int, int, int)} returns {@link
   * SelectionCapabilities#FORMAT_UNSUPPORTED_DRM},
   * {@link SelectionCapabilities#FORMAT_UNSUPPORTED_SUBTYPE} or {@link
   * SelectionCapabilities#FORMAT_UNSUPPORTED_TYPE} for all tracks mapped to the
   * renderer.
   */
  static constexpr int RENDERER_SUPPORT_UNSUPPORTED_TRACKS = 1;
  /**
   * The renderer has tracks mapped to it and at least one is of a supported
   * type, but all such tracks exceed the renderer's capabilities. In other
   * words, {@link #getTrackSupport(int, int, int)} returns {@link
   * SelectionCapabilities#FORMAT_EXCEEDS_CAPABILITIES} for at least one track
   * mapped to the renderer, but does not return {@link
   * SelectionCapabilities#FORMAT_HANDLED} for any tracks mapped to the renderer.
   */
  static constexpr int RENDERER_SUPPORT_EXCEEDS_CAPABILITIES_TRACKS = 2;
  /**
   * The renderer has tracks mapped to it, and at least one such track is
   * playable. In other words, {@link #getTrackSupport(int, int, int)} returns
   * {@link SelectionCapabilities#FORMAT_HANDLED} for at least one track mapped
   * to the renderer.
   */
  static constexpr int RENDERER_SUPPORT_PLAYABLE_TRACKS = 3;

  explicit MappedTrackInfo(
      const std::vector<const char*>& rendererNames,
      const std::vector<MediaType>& rendererTrackTypes,
      const std::vector<std::shared_ptr<TrackGroupArray>>& rendererTrackGroups,
      const std::vector<int>& rendererMixedMimeTypeAdaptiveSupports,
      const std::vector<std::vector<std::vector<int>>>& rendererFormatSupports,
      const std::shared_ptr<TrackGroupArray>& unmappedTrackGroups) noexcept;

  ~MappedTrackInfo() override;

  /**
   * Returns the number of renderers.
   */
  int getRendererCount() const { return rendererTrackTypes.size(); }

  /**
   * Returns the name of the renderer at a given index.
   *
   * @see Renderer#getName()
   * @param rendererIndex The renderer index.
   * @return The name of the renderer.
   */
  const char* getRendererName(int rendererIndex) {
    return rendererNames[rendererIndex];
  }

  /**
   * Returns the track type that the renderer at a given index handles.
   *
   * @param rendererIndex The renderer index.
   * @return The media type of the Renderer
   */
  MediaType getRendererType(int rendererIndex) const {
    return rendererTrackTypes[rendererIndex];
  }

  /**
   * Returns the {@link TrackGroup}s mapped to the renderer at the specified
   * index.
   *
   * @param rendererIndex The renderer index.
   * @return The corresponding {@link TrackGroup}s.
   */
  const std::shared_ptr<TrackGroupArray>& getTrackGroups(int rendererIndex) const {
    return rendererTrackGroups[rendererIndex];
  }

  /**
   * Returns the extent to which a renderer can play the tracks that are mapped
   * to it.
   *
   * @param rendererIndex The renderer index.
   * @return The {@link RendererSupport}.
   */
  int getRendererSupport(int rendererIndex);

  /**
   * Returns the extent to which tracks of a specified type are supported. This
   * is the best level of support obtained from {@link #getRendererSupport(int)}
   * for all renderers that handle the specified type. If no such renderers
   * exist then {@link #RENDERER_SUPPORT_NO_TRACKS} is returned.
   *
   * @param trackType The track type.
   * @return The {@link RendererSupport}.
   */
  int getTypeSupport(MediaType trackType);

  /**
   * Returns the extent to which an individual track is supported by the
   * renderer.
   *
   * @param rendererIndex The renderer index.
   * @param groupIndex The index of the track group to which the track belongs.
   * @param trackIndex The index of the track within the track group.
   * @return The {@link FormatSupport}.
   */
  SelectionCapabilities::FormatSupport
  getTrackSupport(int rendererIndex, int groupIndex, int trackIndex);

  /**
   * Returns the extent to which a renderer supports adaptation between
   * supported tracks in a specified {@link TrackGroup}.
   *
   * <p>Tracks for which {@link #getTrackSupport(int, int, int)} returns {@link
   * SelectionCapabilities#FORMAT_HANDLED} are always considered. Tracks for
   * which {@link #getTrackSupport(int, int, int)} returns {@link
   * SelectionCapabilities#FORMAT_EXCEEDS_CAPABILITIES} are also considered if
   * {@code includeCapabilitiesExceededTracks} is set to {@code true}. Tracks
   * for which {@link #getTrackSupport(int, int, int)} returns {@link
   * SelectionCapabilities#FORMAT_UNSUPPORTED_DRM},
   * {@link SelectionCapabilities#FORMAT_UNSUPPORTED_TYPE} or {@link
   * SelectionCapabilities#FORMAT_UNSUPPORTED_SUBTYPE} are never considered.
   *
   * @param rendererIndex The renderer index.
   * @param groupIndex The index of the track group.
   * @param includeCapabilitiesExceededTracks Whether tracks that exceed the
   * capabilities of the renderer are included when determining support.
   * @return The {@link AdaptiveSupport}.
   */
  SelectionCapabilities::AdaptiveSupport
  getAdaptiveSupport(int rendererIndex, int groupIndex,
      bool includeCapabilitiesExceededTracks);

  /**
   * Returns the extent to which a renderer supports adaptation between
   * specified tracks within a
   * {@link TrackGroup}.
   *
   * @param rendererIndex The renderer index.
   * @param groupIndex The index of the track group.
   * @return The {@link AdaptiveSupport}.
   */
  SelectionCapabilities::AdaptiveSupport
  getAdaptiveSupport(int rendererIndex, int groupIndex,
      std::vector<int> trackIndices);

  /**
   * Returns {@link TrackGroup}s not mapped to any renderer.
   */
  const std::shared_ptr<TrackGroupArray>& getUnmappedTrackGroups() const {
    return unmappedTrackGroups;
  }

private:
  std::vector<const char*> rendererNames;
  std::vector<MediaType> rendererTrackTypes;
  std::vector<std::shared_ptr<TrackGroupArray>> rendererTrackGroups;
  std::vector<int> rendererMixedMimeTypeAdaptiveSupports;
  std::vector<std::vector<std::vector<int>>> rendererFormatSupports;
  std::shared_ptr<TrackGroupArray> unmappedTrackGroups;
};

/**
 * Base class for {@link TrackSelector}s that first establish a mapping between
 * {@link TrackGroup}s and {@link Renderer}s, and then from that mapping create
 * a {@link TrackSelection} for each renderer.
 */
class MappingTrackSelector : public TrackSelector {
public:
  /**
   * Returns the mapping information for the currently active track selection,
   * or null if no selection is currently active.
   */
  const std::shared_ptr<MappedTrackInfo>& getCurrentMappedTrackInfo() const {
    return mCurrentMappedTrackInfo;
  }

  std::shared_ptr<TrackSelectorResult>
  selectTracks(const std::vector<std::shared_ptr<SelectionCapabilities>>
  & rendererCapabilities,
      const std::shared_ptr<TrackGroupArray>& trackGroups) override;

  void onSelectionActivated(const std::shared_ptr<TrackInfo>& info) override {
    mCurrentMappedTrackInfo = std::dynamic_pointer_cast<MappedTrackInfo>(info);
  }

protected:
  virtual std::pair<std::vector<std::shared_ptr<Configuration>>,
      std::vector<std::shared_ptr<TrackSelection>>>
  selectTracks(
      const std::shared_ptr<MappedTrackInfo>& mappedTrackInfo,
      std::vector<std::vector<std::vector<int>>> rendererFormatSupports,
      std::vector<int> trackMixedMimeTypeAdaptationSupport) = 0;

private:
  int findRenderer(const std::vector<std::shared_ptr<SelectionCapabilities>>
  & rendererCapabilities,
      const std::shared_ptr<TrackGroup>& group,
      const std::vector<int>& rendererTrackGroupCounts,
      bool preferUnassociatedRenderer);

  static std::vector<int> getFormatSupport(
      const std::shared_ptr<SelectionCapabilities>& SelectionCapabilities,
      const std::shared_ptr<TrackGroup>& group);

  static std::vector<int> getMixedMimeTypeAdaptationSupports(
      const std::vector<std::shared_ptr<SelectionCapabilities>>& SelectionCapabilities);

  /**
   * Checks that every selection belongs to the mapped track info of its renderer and deselects the
   * renderers whose selection does not.
   *
   * <p>ExoPlayer throws {@code ExoPlaybackException} when a {@link TrackSelector} returns a
   * selection that is stale or out of range. A selector that cannot report an error must not let
   * such a selection through either: the renderer would then index a track group or a format that
   * does not exist, which reads out of bounds. Deselecting the renderer is the same outcome for the
   * playback and cannot be missed.
   *
   * @param mappedTrackInfo The mapping the selections were made from.
   * @param selections The selections to check, updated in place.
   */
  static void validateSelections(const std::shared_ptr<MappedTrackInfo>& mappedTrackInfo,
      std::vector<std::shared_ptr<TrackSelection>>& selections);

  std::shared_ptr<MappedTrackInfo> mCurrentMappedTrackInfo;
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_MAPPING_TRACK_SELECTOR_H
