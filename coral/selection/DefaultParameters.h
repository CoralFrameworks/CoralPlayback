/*
 * Copyright (C) 2024 The Coral Project
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

#ifndef CORAL_PLAYBACK_SELECTION_DEFAULT_PARAMETERS_H
#define CORAL_PLAYBACK_SELECTION_DEFAULT_PARAMETERS_H

#include <coral/selection/Configuration.h>
#include <coral/selection/SelectionOverride.h>
#include <coral/selection/TrackGroup.h>

#include <optional>
#include <set>
#include <unordered_map>

namespace coral {

/**
 * Extends {@link TrackSelectionParameters} by adding fields that are specific
 * to {@link DefaultTrackSelector}.
 */
struct DefaultParameters final {
public:
  // Video
  /**
   * Maximum allowed video width in pixels. The default value is {@link INT_MAX}
   * (i.e. no constraint).
   *
   * <p>To constrain adaptive video track selections to be suitable for a given
   * viewport (the region of the display within which video will be played), use
   * ({@link #viewportWidth},
   * {@link #viewportHeight} and {@link #viewportOrientationMayChange}) instead.
   */
  int maxVideoWidth;
  /**
   * Maximum allowed video height in pixels. The default value is {@link
   * INT_MAX} (i.e. no constraint).
   *
   * <p>To constrain adaptive video track selections to be suitable for a given
   * viewport (the region of the display within which video will be played), use
   * ({@link #viewportWidth}, {@link #viewportHeight} and {@link
   * #viewportOrientationMayChange}) instead.
   */
  int maxVideoHeight;
  /**
   * Maximum allowed video frame rate in hertz. The default value is {@link
   * INT_MAX} (i.e. no constraint).
   */
  int maxVideoFrameRate;
  /**
   * Maximum allowed video bitrate in bits per second. The default value is
   * {@link INT_MAX} (i.e. no constraint).
   */
  int maxVideoBitrate;
  /**
   * Minimum allowed video width in pixels. The default value is 0 (i.e. no
   * constraint).
   */
  int minVideoWidth;
  /**
   * Minimum allowed video height in pixels. The default value is 0 (i.e. no
   * constraint).
   */
  int minVideoHeight;
  /**
   * Minimum allowed video frame rate in hertz. The default value is 0 (i.e. no
   * constraint).
   */
  int minVideoFrameRate;
  /**
   * Minimum allowed video bitrate in bits per second. The default value is 0
   * (i.e. no constraint).
   */
  int minVideoBitrate;
  /**
   * Whether to exceed the {@link #maxVideoWidth}, {@link #maxVideoHeight} and
   * {@link #maxVideoBitrate} constraints when no selection can be made
   * otherwise. The default value is
   * {@code true}.
   */
  bool exceedVideoConstraintsIfNecessary;
  /**
   * Whether to allow adaptive video selections containing mixed MIME types.
   * Adaptations between different MIME types may not be completely seamless, in
   * which case {@link #allowVideoNonSeamlessAdaptiveness} also needs to be
   * {@code true} for mixed MIME type selections to be made. The default value
   * is {@code false}.
   */
  bool allowVideoMixedMimeTypeAdaptiveness;
  /**
   * Whether to allow adaptive video selections where adaptation may not be
   * completely seamless. The default value is {@code true}.
   */
  bool allowVideoNonSeamlessAdaptiveness;
  /**
   * Viewport width in pixels. Constrains video track selections for adaptive
   * content so that only tracks suitable for the viewport are selected. The
   * default value is the physical width of the primary display, in pixels.
   */
  int viewportWidth;
  /**
   * Viewport height in pixels. Constrains video track selections for adaptive
   * content so that only tracks suitable for the viewport are selected. The
   * default value is the physical height of the primary display, in pixels.
   */
  int viewportHeight;
  /**
   * Whether the viewport orientation may change during playback. Constrains
   * video track selections for adaptive content so that only tracks suitable
   * for the viewport are selected. The default value is {@code true}.
   */
  bool viewportOrientationMayChange;
  // Audio
  /**
   * The preferred language for audio and forced text tracks as an IETF BCP 47
   * conformant tag.
   * {@code null} selects the default track, or the first track if there's no
   * default. The default value is {@code null}.
   */
  std::string preferredAudioLanguage;
  /**
   * Maximum allowed audio channel count. The default value is {@link INT_MAX}
   * (i.e. no constraint).
   */
  int maxAudioChannelCount;
  /**
   * Maximum allowed audio bitrate in bits per second. The default value is
   * {@link INT_MAX} (i.e. no constraint).
   */
  int maxAudioBitrate;
  /**
   * Whether to exceed the {@link #maxAudioChannelCount} and {@link
   * #maxAudioBitrate} constraints when no selection can be made otherwise. The
   * default value is {@code true}.
   */
  bool exceedAudioConstraintsIfNecessary;
  /**
   * Whether to allow adaptive audio selections containing mixed MIME types.
   * Adaptations between different MIME types may not be completely seamless.
   * The default value is {@code false}.
   */
  bool allowAudioMixedMimeTypeAdaptiveness;
  /**
   * Whether to allow adaptive audio selections containing mixed sample rates.
   * Adaptations between different sample rates may not be completely seamless.
   * The default value is {@code false}.
   */
  bool allowAudioMixedSampleRateAdaptiveness;
  /**
   * Whether to allow adaptive audio selections containing mixed channel counts.
   * Adaptations between different channel counts may not be completely
   * seamless. The default value is {@code false}.
   */
  bool allowAudioMixedChannelCountAdaptiveness;
  // Text
  /**
   * The preferred language for text tracks as an IETF BCP 47 conformant tag.
   * {@code null} selects the default track if there is one, or no track
   * otherwise. The default value is {@code null}, or the language of the
   * accessibility {@link CaptioningManager} if enabled.
   */
  std::string preferredTextLanguage;
  /**
   * The preferred {@link C.RoleFlags} for text tracks. {@code 0} selects the
   * default track if there is one, or no track otherwise. The default value is
   * {@code 0}, or {@link C#ROLE_FLAG_SUBTITLE} | {@link
   * C#ROLE_FLAG_DESCRIBES_MUSIC_AND_SOUND} if the accessibility {@link
   * CaptioningManager} is enabled.
   */
  int preferredTextRoleFlags;
  /**
   * Whether a text track with undetermined language should be selected if no
   * track with {@link #preferredTextLanguage} is available, or if {@link
   * #preferredTextLanguage} is unset. The default value is {@code false}.
   */
  bool selectUndeterminedTextLanguage;
  /**
   * Bitmask of selection flags that are disabled for text track selections. See
   * {@link MediaSelectionFlags}. The default value is {@code 0} (i.e. no
   * flags).
   */
  MediaSelectionFlags disabledTextTrackSelectionFlags;
  // General
  /**
   * Whether to force selection of the single lowest bitrate audio and video
   * tracks that comply with all other constraints. The default value is {@code
   * false}.
   */
  bool forceLowestBitrate;
  /**
   * Whether to force selection of the highest bitrate audio and video tracks
   * that comply with all other constraints. The default value is {@code false}.
   */
  bool forceHighestSupportedBitrate;
  /**
   * Whether to exceed renderer capabilities when no selection can be made
   * otherwise.
   *
   * <p>This parameter applies when all of the tracks available for a renderer
   * exceed the renderer's reported capabilities. If the parameter is {@code
   * true} then the lowest quality track will still be selected. Playback may
   * succeed if the renderer has under-reported its true capabilities. If {@code
   * false} then no track will be selected. The default value is
   * {@code true}.
   */
  bool exceedSelectionCapabilitiesIfNecessary;
  /**
   * Whether to allow the selection of more than one adaptive track across renderers. If
   * {@code false}, the selector will attempt to select at most one adaptive track across all
   * renderers. The default value is {@code true}.
   */
  bool allowMultipleAdaptiveSelections = true;
  /**
   * The audio session id to use when tunneling, or {@link
   * Configuration::AUDIO_SESSION_ID_UNSET} if tunneling is disabled.
   * The default value is {@link Configuration::AUDIO_SESSION_ID_UNSET}
   * (i.e. tunneling is disabled).
   */
  int tunnelingAudioSessionId;

  std::unordered_map<int, bool> rendererDisabledFlags;

  /**
   * The tracks a renderer has to select, indexed by renderer index.
   *
   * <p>An application that knows which track it wants - because the user chose one - sets an override
   * for the renderer, and the selector stops deciding for it.
   */
  std::unordered_map<int, SelectionOverride> selectionOverrides;

  /**
   * The media types that must not be played.
   *
   * <p>A renderer that handles a disabled media type is not enabled, whatever its tracks are. The
   * default is empty, i.e. every type is played.
   */
  std::set<MediaType> disabledTrackTypes;

  DefaultParameters(
      // Video
      int maxVideoWidth, int maxVideoHeight, int maxVideoFrameRate,
      int maxVideoBitrate, int minVideoWidth, int minVideoHeight,
      int minVideoFrameRate, int minVideoBitrate,
      bool exceedVideoConstraintsIfNecessary,
      bool allowVideoMixedMimeTypeAdaptiveness,
      bool allowVideoNonSeamlessAdaptiveness, int viewportWidth,
      int viewportHeight, bool viewportOrientationMayChange,
      // Audio
      std::string preferredAudioLanguage, int maxAudioChannelCount,
      int maxAudioBitrate, bool exceedAudioConstraintsIfNecessary,
      bool allowAudioMixedMimeTypeAdaptiveness,
      bool allowAudioMixedSampleRateAdaptiveness,
      bool allowAudioMixedChannelCountAdaptiveness,
      // Text
      std::string preferredTextLanguage, int preferredTextRoleFlags,
      bool selectUndeterminedTextLanguage,
      MediaSelectionFlags disabledTextTrackSelectionFlags,
      // General
      bool forceLowestBitrate, bool forceHighestSupportedBitrate,
      bool exceedSelectionCapabilitiesIfNecessary, int tunnelingAudioSessionId)
      : maxVideoWidth(maxVideoWidth), maxVideoHeight(maxVideoHeight),
        maxVideoFrameRate(maxVideoFrameRate), maxVideoBitrate(maxVideoBitrate),
        minVideoWidth(minVideoWidth), minVideoHeight(minVideoHeight),
        minVideoFrameRate(minVideoFrameRate), minVideoBitrate(minVideoBitrate),
        exceedVideoConstraintsIfNecessary(exceedVideoConstraintsIfNecessary),
        allowVideoMixedMimeTypeAdaptiveness(
            allowVideoMixedMimeTypeAdaptiveness),
        allowVideoNonSeamlessAdaptiveness(allowVideoNonSeamlessAdaptiveness),
        viewportWidth(viewportWidth), viewportHeight(viewportHeight),
        viewportOrientationMayChange(viewportOrientationMayChange),
        preferredAudioLanguage(preferredAudioLanguage),
        maxAudioChannelCount(maxAudioChannelCount),
        maxAudioBitrate(maxAudioBitrate),
        exceedAudioConstraintsIfNecessary(exceedAudioConstraintsIfNecessary),
        allowAudioMixedMimeTypeAdaptiveness(
            allowAudioMixedMimeTypeAdaptiveness),
        allowAudioMixedSampleRateAdaptiveness(
            allowAudioMixedSampleRateAdaptiveness),
        allowAudioMixedChannelCountAdaptiveness(
            allowAudioMixedChannelCountAdaptiveness),
        preferredTextLanguage(preferredTextLanguage),
        preferredTextRoleFlags(preferredTextRoleFlags),
        selectUndeterminedTextLanguage(selectUndeterminedTextLanguage),
        disabledTextTrackSelectionFlags(disabledTextTrackSelectionFlags),
        forceLowestBitrate(forceLowestBitrate),
        forceHighestSupportedBitrate(forceHighestSupportedBitrate),
        exceedSelectionCapabilitiesIfNecessary(
            exceedSelectionCapabilitiesIfNecessary),
        tunnelingAudioSessionId(tunnelingAudioSessionId) {}

  [[nodiscard]] bool getRendererDisabled(int rendererIndex) const {
    const auto position = rendererDisabledFlags.find(rendererIndex);
    return position != rendererDisabledFlags.end() && position->second;
  }

  /**
   * Returns whether the media type of a renderer is disabled.
   *
   * @param trackType The media type the renderer handles.
   */
  [[nodiscard]] bool isTrackTypeDisabled(const MediaType trackType) const {
    return disabledTrackTypes.count(trackType) != 0;
  }

  /**
   * Returns the override of a renderer, or nothing when the renderer has none or when the override
   * names a track group the renderer does not have.
   *
   * <p>The second case is what makes an override survive the track groups of a renderer changing: an
   * override that no longer applies is ignored rather than applied to the wrong group.
   *
   * @param rendererIndex The index of the renderer.
   * @param groups The track groups of the renderer.
   */
  [[nodiscard]] std::optional<SelectionOverride> getSelectionOverride(
      const int rendererIndex, const std::shared_ptr<TrackGroupArray>& groups) const {
    const auto position = selectionOverrides.find(rendererIndex);
    if (position == selectionOverrides.end()) {
      return std::nullopt;
    }
    const auto& override = position->second;
    if (groups == nullptr || override.groupIndex < 0 ||
        override.groupIndex >= groups->length()) {
      return std::nullopt;
    }
    return override;
  }

  /**
   * Returns whether a renderer has an override that applies to its track groups.
   *
   * @param rendererIndex The index of the renderer.
   * @param groups The track groups of the renderer.
   */
  bool hasSelectionOverride(const int rendererIndex,
      const std::shared_ptr<TrackGroupArray>& groups) const {
    return getSelectionOverride(rendererIndex, groups).has_value();
  }
};

/**
 * A builder for {@link DefaultParameters}. See the {@link DefaultParameters}
 * documentation for explanations of the parameters that can be configured using
 * this builder.
 */
class ParametersBuilder final {
private:
  // Video
  int maxVideoWidth;
  int maxVideoHeight;
  int maxVideoFrameRate;
  int maxVideoBitrate;
  int minVideoWidth;
  int minVideoHeight;
  int minVideoFrameRate;
  int minVideoBitrate;
  bool exceedVideoConstraintsIfNecessary;
  bool allowVideoMixedMimeTypeAdaptiveness;
  bool allowVideoNonSeamlessAdaptiveness;
  int viewportWidth;
  int viewportHeight;
  bool viewportOrientationMayChange;
  // Audio
  std::string preferredAudioLanguage;
  int maxAudioChannelCount;
  int maxAudioBitrate;
  bool exceedAudioConstraintsIfNecessary;
  bool allowAudioMixedMimeTypeAdaptiveness;
  bool allowAudioMixedSampleRateAdaptiveness;
  bool allowAudioMixedChannelCountAdaptiveness;
  // Text
  std::string preferredTextLanguage;
  int preferredTextRoleFlags;
  bool selectUndeterminedTextLanguage;
  MediaSelectionFlags disabledTextTrackSelectionFlags;

  // General
  bool forceLowestBitrate;
  bool forceHighestSupportedBitrate;
  bool exceedSelectionCapabilitiesIfNecessary;
  bool allowMultipleAdaptiveSelections;
  int tunnelingAudioSessionId;
  std::unordered_map<int, bool> rendererDisabledFlags;
  std::unordered_map<int, SelectionOverride> selectionOverrides;
  std::set<MediaType> disabledTrackTypes;

public:
  ParametersBuilder() { setInitialValues(); }

  ParametersBuilder(const DefaultParameters& initialValues) {
    // Video
    maxVideoWidth = initialValues.maxVideoWidth;
    maxVideoHeight = initialValues.maxVideoHeight;
    maxVideoFrameRate = initialValues.maxVideoFrameRate;
    maxVideoBitrate = initialValues.maxVideoBitrate;
    minVideoWidth = initialValues.minVideoWidth;
    minVideoHeight = initialValues.minVideoHeight;
    minVideoFrameRate = initialValues.minVideoFrameRate;
    minVideoBitrate = initialValues.minVideoBitrate;
    exceedVideoConstraintsIfNecessary =
        initialValues.exceedVideoConstraintsIfNecessary;
    allowVideoMixedMimeTypeAdaptiveness =
        initialValues.allowVideoMixedMimeTypeAdaptiveness;
    allowVideoNonSeamlessAdaptiveness =
        initialValues.allowVideoNonSeamlessAdaptiveness;
    viewportWidth = initialValues.viewportWidth;
    viewportHeight = initialValues.viewportHeight;
    viewportOrientationMayChange = initialValues.viewportOrientationMayChange;
    // Audio
    preferredAudioLanguage = initialValues.preferredAudioLanguage;
    maxAudioChannelCount = initialValues.maxAudioChannelCount;
    maxAudioBitrate = initialValues.maxAudioBitrate;
    exceedAudioConstraintsIfNecessary =
        initialValues.exceedAudioConstraintsIfNecessary;
    allowAudioMixedMimeTypeAdaptiveness =
        initialValues.allowAudioMixedMimeTypeAdaptiveness;
    allowAudioMixedSampleRateAdaptiveness =
        initialValues.allowAudioMixedSampleRateAdaptiveness;
    allowAudioMixedChannelCountAdaptiveness =
        initialValues.allowAudioMixedChannelCountAdaptiveness;
    // Text
    preferredTextLanguage = initialValues.preferredTextLanguage;
    preferredTextRoleFlags = initialValues.preferredTextRoleFlags;
    selectUndeterminedTextLanguage =
        initialValues.selectUndeterminedTextLanguage;
    disabledTextTrackSelectionFlags =
        initialValues.disabledTextTrackSelectionFlags;
    // General
    forceLowestBitrate = initialValues.forceLowestBitrate;
    forceHighestSupportedBitrate = initialValues.forceHighestSupportedBitrate;
    exceedSelectionCapabilitiesIfNecessary =
        initialValues.exceedSelectionCapabilitiesIfNecessary;
    tunnelingAudioSessionId = initialValues.tunnelingAudioSessionId;
    allowMultipleAdaptiveSelections = initialValues.allowMultipleAdaptiveSelections;
    rendererDisabledFlags = initialValues.rendererDisabledFlags;
    selectionOverrides = initialValues.selectionOverrides;
    disabledTrackTypes = initialValues.disabledTrackTypes;
  }

  // General

  /**
   * Disables or re-enables a renderer, whatever its tracks are.
   *
   * <p>A disabled renderer is given no track at all, and is not enabled, so the media it would have
   * played is not played.
   *
   * @param rendererIndex The index of the renderer.
   * @param disabled Whether the renderer is disabled.
   * @return This builder.
   */
  ParametersBuilder& setRendererDisabled(const int rendererIndex, const bool disabled) {
    if (disabled) {
      rendererDisabledFlags[rendererIndex] = true;
    } else {
      rendererDisabledFlags.erase(rendererIndex);
    }
    return *this;
  }

  /**
   * Forces a renderer to select specific tracks, instead of the tracks the selector picks.
   *
   * <p>This is what an application uses when the user chooses an audio track: the choice is imposed on
   * the selector rather than expressed as a preference.
   *
   * @param rendererIndex The index of the renderer.
   * @param override The override, whose track group must belong to the renderer's track groups.
   * @return This builder.
   */
  ParametersBuilder& setSelectionOverride(const int rendererIndex,
      SelectionOverride override) {
    // The override has no default constructor because it always describes tracks, so it is assigned
    // rather than looked up.
    selectionOverrides.insert_or_assign(rendererIndex, std::move(override));
    return *this;
  }

  /**
   * Removes the override of a renderer, so that the selector decides for it again.
   *
   * @param rendererIndex The index of the renderer.
   * @return This builder.
   */
  ParametersBuilder& clearSelectionOverride(const int rendererIndex) {
    selectionOverrides.erase(rendererIndex);
    return *this;
  }

  /**
   * Removes the overrides of every renderer.
   *
   * @return This builder.
   */
  ParametersBuilder& clearSelectionOverrides() {
    selectionOverrides.clear();
    return *this;
  }

  /**
   * Sets whether a media type is played.
   *
   * @param trackType The media type.
   * @param disabled Whether a renderer that handles the type must not be enabled.
   * @return This builder.
   */
  ParametersBuilder& setTrackTypeDisabled(const MediaType trackType, const bool disabled) {
    if (disabled) {
      disabledTrackTypes.insert(trackType);
    } else {
      disabledTrackTypes.erase(trackType);
    }
    return *this;
  }

  /**
   * Sets the media types that are played.
   *
   * @param types The media types that must not be played.
   * @return This builder.
   */
  ParametersBuilder& setDisabledTrackTypes(std::set<MediaType> types) {
    disabledTrackTypes = std::move(types);
    return *this;
  }

  // Video

  /**
   * Equivalent to {@link #setMaxVideoSize setMaxVideoSize(1279, 719)}.
   *
   * @return This builder.
   */
  ParametersBuilder& setMaxVideoSizeSd() { return setMaxVideoSize(1279, 719); }

  /**
   * Equivalent to {@link #setMaxVideoSize setMaxVideoSize(INT_MAX, INT_MAX)}.
   *
   * @return This builder.
   */
  ParametersBuilder& clearVideoSizeConstraints() {
    return setMaxVideoSize(INT_MAX, INT_MAX);
  }

  /**
   * Sets the maximum allowed video width and height.
   *
   * @param maxVideoWidth Maximum allowed video width in pixels.
   * @param maxVideoHeight Maximum allowed video height in pixels.
   * @return This builder.
   */
  ParametersBuilder& setMaxVideoSize(int maxVideoWidth, int maxVideoHeight) {
    this->maxVideoWidth = maxVideoWidth;
    this->maxVideoHeight = maxVideoHeight;
    return *this;
  }

  /**
   * Sets the maximum allowed video frame rate.
   *
   * @param maxVideoFrameRate Maximum allowed video frame rate in hertz.
   * @return This builder.
   */
  ParametersBuilder& setMaxVideoFrameRate(int maxVideoFrameRate) {
    this->maxVideoFrameRate = maxVideoFrameRate;
    return *this;
  }

  /**
   * Sets the maximum allowed video bitrate.
   *
   * @param maxVideoBitrate Maximum allowed video bitrate in bits per second.
   * @return This builder.
   */
  ParametersBuilder& setMaxVideoBitrate(int maxVideoBitrate) {
    this->maxVideoBitrate = maxVideoBitrate;
    return *this;
  }

  /**
   * Sets the minimum allowed video width and height.
   *
   * @param minVideoWidth Minimum allowed video width in pixels.
   * @param minVideoHeight Minimum allowed video height in pixels.
   * @return This builder.
   */
  ParametersBuilder& setMinVideoSize(int minVideoWidth, int minVideoHeight) {
    this->minVideoWidth = minVideoWidth;
    this->minVideoHeight = minVideoHeight;
    return *this;
  }

  /**
   * Sets the minimum allowed video frame rate.
   *
   * @param minVideoFrameRate Minimum allowed video frame rate in hertz.
   * @return This builder.
   */
  ParametersBuilder& setMinVideoFrameRate(int minVideoFrameRate) {
    this->minVideoFrameRate = minVideoFrameRate;
    return *this;
  }

  /**
   * Sets the minimum allowed video bitrate.
   *
   * @param minVideoBitrate Minimum allowed video bitrate in bits per second.
   * @return This builder.
   */
  ParametersBuilder& setMinVideoBitrate(int minVideoBitrate) {
    this->minVideoBitrate = minVideoBitrate;
    return *this;
  }

  /**
   * Sets whether to exceed the {@link #setMaxVideoBitrate}, {@link
   * #setMaxVideoSize(int, int)} and {@link #setMaxVideoFrameRate} constraints
   * when no selection can be made otherwise.
   *
   * @param exceedVideoConstraintsIfNecessary Whether to exceed video
   * constraints when no selection can be made otherwise.
   * @return This builder.
   */
  ParametersBuilder&
  setExceedVideoConstraintsIfNecessary(bool exceedVideoConstraintsIfNecessary) {
    this->exceedVideoConstraintsIfNecessary = exceedVideoConstraintsIfNecessary;
    return *this;
  }

  /**
   * Sets whether to allow adaptive video selections containing mixed MIME
   * types.
   *
   * <p>Adaptations between different MIME types may not be completely seamless,
   * in which case
   * {@link #setAllowVideoNonSeamlessAdaptiveness(bool)} also needs to be {@code
   * true} for mixed MIME type selections to be made.
   *
   * @param allowVideoMixedMimeTypeAdaptiveness Whether to allow adaptive video
   * selections containing mixed MIME types.
   * @return This builder.
   */
  ParametersBuilder& setAllowVideoMixedMimeTypeAdaptiveness(
      bool allowVideoMixedMimeTypeAdaptiveness) {
    this->allowVideoMixedMimeTypeAdaptiveness =
        allowVideoMixedMimeTypeAdaptiveness;
    return *this;
  }

  /**
   * Sets whether to allow adaptive video selections where adaptation may not be
   * completely seamless.
   *
   * @param allowVideoNonSeamlessAdaptiveness Whether to allow adaptive video
   * selections where adaptation may not be completely seamless.
   * @return This builder.
   */
  ParametersBuilder&
  setAllowVideoNonSeamlessAdaptiveness(bool allowVideoNonSeamlessAdaptiveness) {
    this->allowVideoNonSeamlessAdaptiveness = allowVideoNonSeamlessAdaptiveness;
    return *this;
  }

  /**
   * Equivalent to {@link #setViewportSize setViewportSize(INT_MAX, INT_MAX,
   * true)}.
   *
   * @return This builder.
   */
  ParametersBuilder& clearViewportSizeConstraints() {
    return setViewportSize(INT_MAX, INT_MAX, true);
  }

  /**
   * Sets the viewport size to constrain adaptive video selections so that only
   * tracks suitable for the viewport are selected.
   *
   * @param viewportWidth Viewport width in pixels.
   * @param viewportHeight Viewport height in pixels.
   * @param viewportOrientationMayChange Whether the viewport orientation may
   * change during playback.
   * @return This builder.
   */
  ParametersBuilder& setViewportSize(int viewportWidth, int viewportHeight,
      bool viewportOrientationMayChange) {
    this->viewportWidth = viewportWidth;
    this->viewportHeight = viewportHeight;
    this->viewportOrientationMayChange = viewportOrientationMayChange;
    return *this;
  }

  // Audio
  /**
   * Sets the preferred language for audio and forced text tracks.
   *
   * @param preferredAudioLanguage Preferred audio language as an IETF BCP 47
   * conformant tag, or
   *     {@code null} to select the default track, or the first track if there's
   * no default.
   * @return This builder.
   */
  ParametersBuilder&
  setPreferredAudioLanguage(const std::string& preferredAudioLanguage) {
    this->preferredAudioLanguage = preferredAudioLanguage;
    return *this;
  }

  /**
   * Sets the maximum allowed audio channel count.
   *
   * @param maxAudioChannelCount Maximum allowed audio channel count.
   * @return This builder.
   */
  ParametersBuilder& setMaxAudioChannelCount(int maxAudioChannelCount) {
    this->maxAudioChannelCount = maxAudioChannelCount;
    return *this;
  }

  /**
   * Sets the maximum allowed audio bitrate.
   *
   * @param maxAudioBitrate Maximum allowed audio bitrate in bits per second.
   * @return This builder.
   */
  ParametersBuilder& setMaxAudioBitrate(int maxAudioBitrate) {
    this->maxAudioBitrate = maxAudioBitrate;
    return *this;
  }

  /**
   * Sets whether to exceed the {@link #setMaxAudioChannelCount(int)} and {@link
   * #setMaxAudioBitrate(int)} constraints when no selection can be made
   * otherwise.
   *
   * @param exceedAudioConstraintsIfNecessary Whether to exceed audio
   * constraints when no selection can be made otherwise.
   * @return This builder.
   */
  ParametersBuilder&
  setExceedAudioConstraintsIfNecessary(bool exceedAudioConstraintsIfNecessary) {
    this->exceedAudioConstraintsIfNecessary = exceedAudioConstraintsIfNecessary;
    return *this;
  }

  /**
   * Sets whether to allow adaptive audio selections containing mixed MIME
   * types.
   *
   * <p>Adaptations between different MIME types may not be completely seamless.
   *
   * @param allowAudioMixedMimeTypeAdaptiveness Whether to allow adaptive audio
   * selections containing mixed MIME types.
   * @return This builder.
   */
  ParametersBuilder& setAllowAudioMixedMimeTypeAdaptiveness(
      bool allowAudioMixedMimeTypeAdaptiveness) {
    this->allowAudioMixedMimeTypeAdaptiveness =
        allowAudioMixedMimeTypeAdaptiveness;
    return *this;
  }

  /**
   * Sets whether to allow adaptive audio selections containing mixed sample
   * rates.
   *
   * <p>Adaptations between different sample rates may not be completely
   * seamless.
   *
   * @param allowAudioMixedSampleRateAdaptiveness Whether to allow adaptive
   * audio selections containing mixed sample rates.
   * @return This builder.
   */
  ParametersBuilder& setAllowAudioMixedSampleRateAdaptiveness(
      bool allowAudioMixedSampleRateAdaptiveness) {
    this->allowAudioMixedSampleRateAdaptiveness =
        allowAudioMixedSampleRateAdaptiveness;
    return *this;
  }

  /**
   * Sets whether to allow adaptive audio selections containing mixed channel
   * counts.
   *
   * <p>Adaptations between different channel counts may not be completely
   * seamless.
   *
   * @param allowAudioMixedChannelCountAdaptiveness Whether to allow adaptive
   * audio selections containing mixed channel counts.
   * @return This builder.
   */
  ParametersBuilder& setAllowAudioMixedChannelCountAdaptiveness(
      bool allowAudioMixedChannelCountAdaptiveness) {
    this->allowAudioMixedChannelCountAdaptiveness =
        allowAudioMixedChannelCountAdaptiveness;
    return *this;
  }

  // Text

  /**
   * Sets the preferred language for text tracks.
   *
   * @param preferredTextLanguage Preferred text language as an IETF BCP 47
   * conformant tag, or
   *     {@code null} to select the default track if there is one, or no track
   * otherwise.
   * @return This builder.
   */
  ParametersBuilder&
  setPreferredTextLanguage(const std::string& preferredTextLanguage) {
    this->preferredTextLanguage = preferredTextLanguage;
    return *this;
  }

  /**
   * Sets the preferred {@link C.RoleFlags} for text tracks.
   *
   * @param preferredTextRoleFlags Preferred text role flags.
   * @return This builder.
   */
  ParametersBuilder& setPreferredTextRoleFlags(int preferredTextRoleFlags) {
    this->preferredTextRoleFlags = preferredTextRoleFlags;
    return *this;
  }

  /**
   * Sets whether a text track with undetermined language should be selected if
   * no track with
   * {@link #setPreferredTextLanguage(String)} is available, or if the preferred
   * language is unset.
   *
   * @param selectUndeterminedTextLanguage Whether a text track with
   * undetermined language should be selected if no preferred language track is
   * available.
   * @return This builder.
   */
  ParametersBuilder&
  setSelectUndeterminedTextLanguage(bool selectUndeterminedTextLanguage) {
    this->selectUndeterminedTextLanguage = selectUndeterminedTextLanguage;
    return *this;
  }

  /**
   * Sets a bitmask of selection flags that are disabled for text track
   * selections.
   *
   * @param disabledTextTrackSelectionFlags A bitmask of {@link
   * MediaSelectionFlags} that are disabled for text track selections.
   * @return This builder.
   */
  ParametersBuilder& setDisabledTextTrackSelectionFlags(
      MediaSelectionFlags disabledTextTrackSelectionFlags) {
    this->disabledTextTrackSelectionFlags = disabledTextTrackSelectionFlags;
    return *this;
  }

  // General

  /**
   * Sets whether to force selection of the single lowest bitrate audio and
   * video tracks that comply with all other constraints.
   *
   * @param forceLowestBitrate Whether to force selection of the single lowest
   * bitrate audio and video tracks.
   * @return This builder.
   */
  ParametersBuilder& setForceLowestBitrate(bool forceLowestBitrate) {
    this->forceLowestBitrate = forceLowestBitrate;
    return *this;
  }

  /**
   * Sets whether to force selection of the highest bitrate audio and video
   * tracks that comply with all other constraints.
   *
   * @param forceHighestSupportedBitrate Whether to force selection of the
   * highest bitrate audio and video tracks.
   * @return This builder.
   */
  ParametersBuilder&
  setForceHighestSupportedBitrate(bool forceHighestSupportedBitrate) {
    this->forceHighestSupportedBitrate = forceHighestSupportedBitrate;
    return *this;
  }

  /**
   * Sets whether to exceed renderer capabilities when no selection can be made
   * otherwise.
   *
   * <p>This parameter applies when all of the tracks available for a renderer
   * exceed the renderer's reported capabilities. If the parameter is {@code
   * true} then the lowest quality track will still be selected. Playback may
   * succeed if the renderer has under-reported its true capabilities. If {@code
   * false} then no track will be selected.
   *
   * @param exceedSelectionCapabilitiesIfNecessary Whether to exceed renderer
   * capabilities when no selection can be made otherwise.
   * @return This builder.
   */
  ParametersBuilder& setExceedSelectionCapabilitiesIfNecessary(
      bool exceedSelectionCapabilitiesIfNecessary) {
    this->exceedSelectionCapabilitiesIfNecessary =
        exceedSelectionCapabilitiesIfNecessary;
    return *this;
  }

  /**
   * Sets the audio session id to use when tunneling.
   *
   * <p>Enables or disables tunneling. To enable tunneling, pass an audio
   * session id to use when in tunneling mode. Session ids can be generated
   * using {@link C#generateAudioSessionIdV21(Context)}. To disable tunneling
   * pass {@link Configuration::AUDIO_SESSION_ID_UNSET}. Tunneling will
   * only be activated if it's both enabled and supported by the audio and video
   * renderers for the selected tracks.
   *
   * @param tunnelingAudioSessionId The audio session id to use when tunneling,
   * or {@link Configuration::AUDIO_SESSION_ID_UNSET} to disable
   * tunneling.
   * @return This builder.
   */
  ParametersBuilder& setTunnelingAudioSessionId(int tunnelingAudioSessionId) {
    this->tunnelingAudioSessionId = tunnelingAudioSessionId;
    return *this;
  }

  /**
   * Sets whether to allow the selection of more than one adaptive track across
   * renderers. If {@code false}, the selector will attempt to select at most
   * one adaptive track across all renderers.
   *
   * @param allowMultipleAdaptiveSelections Whether to allow multiple adaptive
   * selections.
   * @return This builder.
   */
  ParametersBuilder& setAllowMultipleAdaptiveSelections(
      bool allowMultipleAdaptiveSelections) {
    this->allowMultipleAdaptiveSelections = allowMultipleAdaptiveSelections;
    return *this;
  }

  DefaultParameters build() {
    auto parameters = DefaultParameters(
        // Video
        maxVideoWidth, maxVideoHeight, maxVideoFrameRate, maxVideoBitrate,
        minVideoWidth, minVideoHeight, minVideoFrameRate, minVideoBitrate,
        exceedVideoConstraintsIfNecessary, allowVideoMixedMimeTypeAdaptiveness,
        allowVideoNonSeamlessAdaptiveness, viewportWidth, viewportHeight,
        viewportOrientationMayChange,
        // Audio
        preferredAudioLanguage.c_str(), maxAudioChannelCount, maxAudioBitrate,
        exceedAudioConstraintsIfNecessary, allowAudioMixedMimeTypeAdaptiveness,
        allowAudioMixedSampleRateAdaptiveness,
        allowAudioMixedChannelCountAdaptiveness,
        // Text
        preferredTextLanguage.c_str(), preferredTextRoleFlags,
        selectUndeterminedTextLanguage, disabledTextTrackSelectionFlags,
        // General
        forceLowestBitrate, forceHighestSupportedBitrate,
        exceedSelectionCapabilitiesIfNecessary, tunnelingAudioSessionId);
    // The fields below are not part of the value constructor because they do not describe the media
    // the selector is looking for, so they are copied over separately.
    parameters.allowMultipleAdaptiveSelections = allowMultipleAdaptiveSelections;
    parameters.rendererDisabledFlags = rendererDisabledFlags;
    parameters.selectionOverrides = selectionOverrides;
    parameters.disabledTrackTypes = disabledTrackTypes;
    return parameters;
  }

private:
  void setInitialValues() {
    // Video
    maxVideoWidth = INT_MAX;
    maxVideoHeight = INT_MAX;
    maxVideoFrameRate = INT_MAX;
    maxVideoBitrate = INT_MAX;
    // The minimum constraints are "no constraint" by default, like ExoPlayer's Parameters.Builder:
    // leaving them indeterminate would make the video track choice depend on whatever the stack
    // happened to hold, because they are compared against every candidate format.
    minVideoWidth = 0;
    minVideoHeight = 0;
    minVideoFrameRate = 0;
    minVideoBitrate = 0;
    exceedVideoConstraintsIfNecessary = true;
    allowVideoMixedMimeTypeAdaptiveness = false;
    allowVideoNonSeamlessAdaptiveness = true;
    viewportWidth = INT_MAX;
    viewportHeight = INT_MAX;
    viewportOrientationMayChange = true;
    // Audio
    preferredAudioLanguage = "";
    maxAudioChannelCount = INT_MAX;
    maxAudioBitrate = INT_MAX;
    exceedAudioConstraintsIfNecessary = true;
    allowAudioMixedMimeTypeAdaptiveness = false;
    allowAudioMixedSampleRateAdaptiveness = false;
    allowAudioMixedChannelCountAdaptiveness = false;
    // Text
    preferredTextLanguage = "";
    preferredTextRoleFlags = 0;
    selectUndeterminedTextLanguage = false;
    // No selection flag is ignored by default, so the flags of a text track are compared as they
    // are, like ExoPlayer's Parameters.Builder.
    disabledTextTrackSelectionFlags = MediaSelectionFlags::kMediaSelectionFlagNone;
    // General
    forceLowestBitrate = false;
    forceHighestSupportedBitrate = false;
    exceedSelectionCapabilitiesIfNecessary = true;
    allowMultipleAdaptiveSelections = true;
    tunnelingAudioSessionId = Configuration::AUDIO_SESSION_ID_UNSET;
    // No renderer is disabled by default, no renderer has an override, and every media type is played.
    rendererDisabledFlags.clear();
    selectionOverrides.clear();
    disabledTrackTypes.clear();
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_DEFAULT_PARAMETERS_H
