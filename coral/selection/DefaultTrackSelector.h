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

#ifndef CORAL_PLAYBACK_SELECTION_DEFAULT_TRACK_SELECTOR_H
#define CORAL_PLAYBACK_SELECTION_DEFAULT_TRACK_SELECTOR_H

#include <coral/selection/DefaultParameters.h>
#include <coral/selection/MappingTrackSelector.h>
#include <coral/util/SystemLanguage.h>

#include <utility>

namespace coral {

class DefaultTrackSelector final : public MappingTrackSelector {
protected:
  class AudioTrackScore;

  class TextTrackScore;

public:
  DefaultTrackSelector();

  explicit DefaultTrackSelector(
    const DefaultParameters& parameters,
    std::unique_ptr<TrackSelection::Factory> factory) noexcept;

  // The factory overload below hides the public overload of the base class, which is the one the
  // player calls, so it is brought back into scope.
  using MappingTrackSelector::selectTracks;

  /** Returns the parameters the selector decides with. */
  [[nodiscard]] const DefaultParameters& getParameters() const {
    return parameters;
  }

  /**
   * Sets the parameters the selector decides with, and invalidates the selections it made with the
   * previous ones.
   *
   * @param parameters The new parameters.
   */
  void setParameters(const DefaultParameters& parameters);

  /**
   * Disables or re-enables a renderer, and invalidates the selections.
   *
   * <p>Convenience for an application that only wants to turn one renderer off:
   * {@code setParameters(ParametersBuilder(getParameters()).setRendererDisabled(i, disabled).build())}.
   *
   * @param rendererIndex The index of the renderer.
   * @param disabled Whether the renderer is disabled.
   */
  void setRendererDisabled(int rendererIndex, bool disabled);

  /**
   * Forces a renderer to select specific tracks, and invalidates the selections.
   *
   * @param rendererIndex The index of the renderer.
   * @param override The override, or nothing to let the selector decide for the renderer again.
   */
  void setSelectionOverride(int rendererIndex,
      const std::optional<SelectionOverride>& override);

  /**
   * Returns the override of a renderer, or nothing when it has none.
   *
   * @param rendererIndex The index of the renderer.
   */
  [[nodiscard]] std::optional<SelectionOverride> getSelectionOverride(int rendererIndex) const;

  /**
   * Returns whether a renderer has an override that applies to its track groups, i.e. whether the
   * selector decides for it or the application does.
   *
   * @param rendererIndex The index of the renderer.
   */
  [[nodiscard]] bool hasSelectionOverride(int rendererIndex) const;

protected:
  std::pair<std::vector<std::shared_ptr<Configuration>>,
    std::vector<std::shared_ptr<TrackSelection>>>
  selectTracks(
    const std::shared_ptr<MappedTrackInfo>& mappedTrackInfo,
    std::vector<std::vector<std::vector<int>>> rendererFormatSupports,
    std::vector<int> rendererMixedMimeTypeAdaptationSupport) override;

  std::vector<std::unique_ptr<TrackSelection::Definition>> selectAllTracks(
    const std::shared_ptr<MappedTrackInfo>& mappedTrackInfo,
    const std::vector<std::vector<std::vector<int>>>& rendererFormatSupports,
    const std::vector<int>& rendererMixedMimeTypeAdaptationSupports,
    const DefaultParameters& params);

  std::unique_ptr<TrackSelection::Definition>
  selectVideoTrack(const std::shared_ptr<TrackGroupArray>& groups,
    const std::vector<std::vector<int>>& formatSupport,
    int mixedMimeTypeAdaptationSupports,
    const DefaultParameters& params,
    bool enableAdaptiveTrackSelection);

  static std::unique_ptr<TrackSelection::Definition>
  selectAdaptiveVideoTrack(const std::shared_ptr<TrackGroupArray>& groups,
    const std::vector<std::vector<int>>& formatSupport,
    int mixedMimeTypeAdaptationSupports,
    const DefaultParameters& params);

  static std::vector<int> getAdaptiveVideoTracksForGroup(
    const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& formatSupport, bool allowMixedMimeTypes,
    int requiredAdaptiveSupport, int maxVideoWidth, int maxVideoHeight,
    int maxVideoFrameRate, int maxVideoBitrate, int minVideoWidth,
    int minVideoHeight, int minVideoFrameRate, int minVideoBitrate,
    int viewportWidth, int viewportHeight, bool viewportOrientationMayChange);

  static int getAdaptiveVideoTrackCountForMimeType(
    const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& formatSupport, int requiredAdaptiveSupport,
    std::string mimeType, int maxVideoWidth, int maxVideoHeight,
    int maxVideoFrameRate, int maxVideoBitrate, int minVideoWidth,
    int minVideoHeight, int minVideoFrameRate, int minVideoBitrate,
    const std::vector<int>& selectedTrackIndices);

  static void filterAdaptiveVideoTrackCountForMimeType(
    const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& formatSupport, int requiredAdaptiveSupport,
    std::string mimeType, int maxVideoWidth, int maxVideoHeight,
    int maxVideoFrameRate, int maxVideoBitrate, int minVideoWidth,
    int minVideoHeight, int minVideoFrameRate, int minVideoBitrate,
    std::vector<int>& selectedTrackIndices);

  static bool isSupportedAdaptiveVideoTrack(
    const std::shared_ptr<MediaFormat>& format, std::string mimeType,
    int formatSupport, int requiredAdaptiveSupport, int maxVideoWidth,
    int maxVideoHeight, int maxVideoFrameRate, int maxVideoBitrate,
    int minVideoWidth, int minVideoHeight, int minVideoFrameRate,
    int minVideoBitrate);

  static std::unique_ptr<TrackSelection::Definition>
  selectFixedVideoTrack(const std::shared_ptr<TrackGroupArray>& groups,
    const std::vector<std::vector<int>>& formatSupport,
    const DefaultParameters& params);

  std::pair<std::unique_ptr<TrackSelection::Definition>,
    std::unique_ptr<AudioTrackScore>>
  selectAudioTrack(const std::shared_ptr<TrackGroupArray>& groups,
    const std::vector<std::vector<int>>& formatSupport,
    int mixedMimeTypeAdaptationSupports,
    const DefaultParameters& params,
    bool enableAdaptiveTrackSelection);

  static std::vector<int>
  getAdaptiveAudioTracks(const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& formatSupport,
    int primaryTrackIndex, int maxAudioBitrate,
    bool allowMixedMimeTypeAdaptiveness,
    bool allowMixedSampleRateAdaptiveness,
    bool allowAudioMixedChannelCountAdaptiveness);

  static bool isSupportedAdaptiveAudioTrack(
    const std::shared_ptr<MediaFormat>& format, int formatSupport,
    const std::shared_ptr<MediaFormat>& primaryFormat, int maxAudioBitrate,
    bool allowMixedMimeTypeAdaptiveness,
    bool allowMixedSampleRateAdaptiveness,
    bool allowAudioMixedChannelCountAdaptiveness);

  std::pair<std::unique_ptr<TrackSelection::Definition>,
    std::unique_ptr<TextTrackScore>>
  selectTextTrack(const std::shared_ptr<TrackGroupArray>& groups,
    const std::vector<std::vector<int>>& formatSupport,
    const DefaultParameters& params,
    const std::string& selectedAudioLanguage);

  std::unique_ptr<TrackSelection::Definition>
  selectOtherTrack(MediaType trackType,
    const std::shared_ptr<TrackGroupArray>& groups,
    const std::vector<std::vector<int>>& formatSupport,
    const DefaultParameters& params);

private:
  /**
   * If a dimension (i.e. width or height) of a video is greater or equal to
   * this fraction of the corresponding viewport dimension, then the video is
   * considered as filling the viewport (in that dimension).
   */
  static constexpr float FRACTION_TO_CONSIDER_FULLSCREEN = 0.98f;

  /** Represents an undetermined language as an ISO 639-2 language code. */
  static constexpr auto LANGUAGE_UNDETERMINED = "und";

  std::unique_ptr<TrackSelection::Factory> trackSelectionFactory;
  DefaultParameters parameters;

protected:
  /**
   * Determines whether tunneling should be enabled, replacing {@link
   * Configuration}s in
   * {@code rendererConfigurations} with configurations that enable tunneling on
   * the appropriate renderers if so.
   *
   * @param mappedTrackInfo Mapped track information.
   * @param rendererFormatSupports The {@link Capabilities} for each mapped
   * track, indexed by renderer, track group and track (in that order).
   * @param rendererConfigurations The renderer configurations. Configurations
   * may be replaced with ones that enable tunneling as a result of this call.
   * @param trackSelections The renderer track selections.
   * @param tunnelingAudioSessionId The audio session id to use when tunneling,
   * or {@link C#AUDIO_SESSION_ID_UNSET} if tunneling should not be enabled.
   */
  static void maybeConfigureRenderersForTunneling(
    const std::shared_ptr<MappedTrackInfo>& mappedTrackInfo,
    const std::vector<std::vector<std::vector<int>>>& rendererFormatSupports,
    std::vector<std::shared_ptr<Configuration>>
    & rendererConfigurations,
    const std::vector<std::shared_ptr<TrackSelection>>& trackSelections,
    int tunnelingAudioSessionId);

  /**
   * Returns whether a renderer supports tunneling for a {@link TrackSelection}.
   *
   * @param formatSupport The {@link Capabilities} for each track, indexed by
   * group index and track index (in that order).
   * @param trackGroups The {@link TrackGroupArray}s for the renderer.
   * @param selection The track selection.
   * @return Whether the renderer supports tunneling for the {@link
   * TrackSelection}.
   */
  static bool rendererSupportsTunneling(
    const std::vector<std::vector<int>>& formatSupport,
    const std::shared_ptr<TrackGroupArray>& trackGroups,
    const std::shared_ptr<TrackSelection>& selection);

  /**
   * Returns true if the {@link FormatSupport} in the given {@link Capabilities}
   * is {@link SelectionCapabilities#FORMAT_HANDLED} or if {@code
   * allowExceedsCapabilities} is set and the format support is {@link
   * SelectionCapabilities#FORMAT_EXCEEDS_CAPABILITIES}.
   *
   * @param formatSupport {@link Capabilities}.
   * @param allowExceedsCapabilities Whether to return true if {@link
   * FormatSupport} is {@link SelectionCapabilities#FORMAT_EXCEEDS_CAPABILITIES}.
   * @return True if {@link FormatSupport} is {@link
   * SelectionCapabilities#FORMAT_HANDLED}, or if
   *     {@code allowExceedsCapabilities} is set and the format support is
   * {@link SelectionCapabilities#FORMAT_EXCEEDS_CAPABILITIES}.
   */
  static bool isSupported(int formatSupport, bool allowExceedsCapabilities) {
    auto maskedSupport = SelectionCapabilities::getFormatSupport(
      (SelectionCapabilities::Capabilities) formatSupport);
    return maskedSupport == SelectionCapabilities::FORMAT_HANDLED ||
           (
             allowExceedsCapabilities &&
             maskedSupport == SelectionCapabilities::FORMAT_EXCEEDS_CAPABILITIES);
  }

  /**
   * Normalizes the input string to null if it does not define a language, or
   * returns it otherwise.
   *
   * @param language The string.
   * @return The string, optionally normalized to null if it does not define a
   * language.
   */
  static std::string normalizeUndeterminedLanguageToNull(std::string language) {
    if (language.empty() || language == LANGUAGE_UNDETERMINED) {
      return "";
    } else {
      return language;
    }
  }

  /**
   * Returns a score for how well a language specified in a {@linkFormat}
   * matches a given language.
   *
   * @param format The {@link Format}.
   * @param language The language, or null.
   * @param allowUndeterminedFormatLanguage Whether matches with an empty or
   * undetermined format language tag are allowed.
   * @return A score of 4 if the languages match fully, a score of 3 if the
   * languages match partly, a score of 2 if the languages don't match but
   * belong to the same main language, a score of 1 if the format language is
   * undetermined and such a match is allowed, and a score of 0 if the languages
   * don't match at all.
   */
  static int getFormatLanguageScore(const std::shared_ptr<MediaFormat>& format,
    std::string language,
    bool allowUndeterminedFormatLanguage);

  static std::vector<int>
  getViewportFilteredTrackIndices(const std::shared_ptr<TrackGroup>& group,
    int viewportWidth, int viewportHeight,
    bool orientationMayChange);

  /**
   * Given viewport dimensions and video dimensions, computes the maximum size
   * of the video as it will be rendered to fit inside of the viewport.
   */
  static std::pair<int, int> getMaxVideoSizeInViewport(bool orientationMayChange,
    int viewportWidth, int viewportHeight,
    int videoWidth, int videoHeight);

  static bool startWith(const std::string& str, const std::string& head) {
    return str.compare(0, head.size(), head) == 0;
  }

  static std::vector<std::string> splitAtFirst(const std::string& str,
    const char* a) {
    auto res = std::vector<std::string>();
    auto pos = str.find(a);
    if (std::string::npos != pos) {
      res.push_back(str.substr(0, pos));
      res.push_back(str.substr(pos + 1));
    } else {
      res.push_back(str);
    }
    return res;
  }

  static int ceilDivide(int numerator, int denominator) {
    return (numerator + denominator - 1) / denominator;
  }

  /**
   * Chains comparisons so that the first one that is not equal decides the result.
   *
   * <p>This is the counterpart of Guava's {@code ComparisonChain}, which ExoPlayer's track scores
   * use: every {@code compare} call is only taken into account while the chain is still undecided,
   * so the order in which the fields are compared is the priority order. Without the short circuit
   * the last differing field would win instead, which reverses the priority.
   */
  class ComparisonChain final {
  public:
    static ComparisonChain start() { return ComparisonChain(); }

    template<typename T>
    ComparisonChain& compare(const T& left, const T& right) {
      return apply(compareTo(left, right));
    }

    ComparisonChain& compareTrueFirst(bool left, bool right) {
      return apply(compareTo(right, left));
    }

    ComparisonChain& compareFalseFirst(bool left, bool right) {
      return apply(compareTo(left, right));
    }

    int result() const { return mResult; }

  private:
    int mResult;

    ComparisonChain() : mResult(0) {
    }

    /** Records a comparison result, unless the chain is already decided. */
    ComparisonChain& apply(const int result) {
      if (mResult != 0) {
        return *this;
      }
      if (result < 0) {
        mResult = -1;
      } else if (result > 0) {
        mResult = 1;
      }
      return *this;
    }

    /**
     * Compares the two specified values.
     * @tparam T The value type
     * @param left the first value to compare
     * @param right the second value to compare
     * @return the value 0 if left is numerically equal to right; a value less
     * than 0 if left is numerically less than right; and a value greater than 0
     * if left is numerically greater than right.
     */
    template<typename T>
    static int compareTo(const T& left, const T& right) {
      if (left == right) {
        return 0;
      }
      return left < right ? -1 : 1;
    }
  };

  /** Represents how well a video track matches the selection {@link
   * Parameters}. */
  class VideoTrackScore final {
  public:
    /**
     * Whether the provided format is within the parameter maximum constraints.
     * If {@code false}, the format should not be selected.
     */
    bool isWithinMaxConstraints;
    DefaultParameters parameters;
    bool isWithinMinConstraints;
    bool isWithinSelectionCapabilities;
    int bitrate;
    int pixelCount;

    explicit VideoTrackScore(const std::shared_ptr<MediaFormat>& format,
      const DefaultParameters& parameters,
      int formatSupport,
      bool isSuitableForViewport) noexcept
      : parameters(parameters) {
      isWithinMaxConstraints =
        isSuitableForViewport &&
        (
          format->getWidth() == MediaFormat::NO_VALUE ||
          format->getWidth() <= parameters.maxVideoWidth) &&
        (
          format->getHeight() == MediaFormat::NO_VALUE ||
          format->getHeight() <= parameters.maxVideoHeight) &&
        (
          format->getFrameRate() == MediaFormat::NO_VALUE ||
          format->getFrameRate() <= parameters.maxVideoFrameRate) &&
        (
          format->getBitrate() == MediaFormat::NO_VALUE ||
          format->getBitrate() <= parameters.maxVideoBitrate);
      isWithinMinConstraints =
        isSuitableForViewport &&
        (format->getWidth() == MediaFormat::NO_VALUE ||
         format->getWidth() >= parameters.minVideoWidth) &&
        (
          format->getHeight() == MediaFormat::NO_VALUE ||
          format->getHeight() >= parameters.minVideoHeight) &&
        (
          format->getFrameRate() == MediaFormat::NO_VALUE ||
          format->getFrameRate() >= parameters.minVideoFrameRate) &&
        (
          format->getBitrate() == MediaFormat::NO_VALUE ||
          format->getBitrate() >= parameters.minVideoBitrate);
      isWithinSelectionCapabilities =
        isSupported((SelectionCapabilities::Capabilities) formatSupport, false);
      bitrate = format->getBitrate();
      pixelCount = format->getPixelCount();
    }

    int compareTo(const std::unique_ptr<VideoTrackScore>& other) {
      if (other == nullptr) {
        return 1;
      }
      // The chain short-circuits, so the fields are compared in ExoPlayer's priority order:
      // capabilities, then the constraints, then the quality of the two formats.
      return ComparisonChain::start()
             .compareFalseFirst(isWithinSelectionCapabilities,
               other->isWithinSelectionCapabilities)
             .compareFalseFirst(isWithinMaxConstraints,
               other->isWithinMaxConstraints)
             .compareFalseFirst(isWithinMinConstraints,
               other->isWithinMinConstraints)
             .compare(parameters.forceLowestBitrate ? bitrate : 0,
               parameters.forceLowestBitrate ? other->bitrate : 0)
             .compare((isWithinMaxConstraints && isWithinSelectionCapabilities)
                        ? pixelCount
                        : 0,
               (other->isWithinMaxConstraints && other->isWithinSelectionCapabilities)
                 ? other->pixelCount
                 : 0)
             .compare((isWithinMaxConstraints && isWithinSelectionCapabilities)
                        ? bitrate
                        : 0,
               (other->isWithinMaxConstraints && other->isWithinSelectionCapabilities)
                 ? other->bitrate
                 : 0)
             .result();
    }
  };

  /** Represents how well an audio track matches the selection {@link
   * Parameters}. */
  class AudioTrackScore final {
  public:
    /**
     * Whether the provided format is within the parameter constraints. If
     * {@code false}, the format should not be selected.
     */
    bool isWithinConstraints;

    std::string language;
    DefaultParameters parameters;
    bool isWithinSelectionCapabilities;
    int preferredLanguageScore;
    int localeLanguageMatchIndex;
    int localeLanguageScore;
    bool isDefaultSelectionFlag;
    int channelCount;
    int sampleRate;
    int bitrate;

    explicit AudioTrackScore(const std::shared_ptr<MediaFormat>& format,
      const DefaultParameters& parameters,
      int formatSupport) noexcept
      : language(normalizeUndeterminedLanguageToNull(format->getLanguage())),
        parameters(parameters) {
      isWithinSelectionCapabilities =
        isSupported(formatSupport, /* allowExceedsCapabilities= */ false);
      preferredLanguageScore = getFormatLanguageScore(
        format, parameters.preferredAudioLanguage, false);
      isDefaultSelectionFlag =
        (format->getSelectionFlags() & MediaSelectionFlags::kMediaSelectionDefault) != 0;
      channelCount = format->getChannelCount();
      sampleRate = format->getSampleRate();
      bitrate = format->getBitrate();
      isWithinConstraints =
        (format->getBitrate() == MediaFormat::NO_VALUE ||
         format->getBitrate() <= parameters.maxAudioBitrate) &&
        (format->getChannelCount() == MediaFormat::NO_VALUE ||
         format->getChannelCount() <= parameters.maxAudioChannelCount);
      // When the application expressed no preference of its own, the language of the system decides,
      // which is ExoPlayer's secondary language score.
      const auto& localeLanguages = SystemLanguage::getLanguageCodes();
      auto bestMatchIndex = INT_MAX;
      auto bestMatchScore = 0;
      for (size_t i = 0; i < localeLanguages.size(); ++i) {
        const auto score =
          getFormatLanguageScore(format, localeLanguages[i], /* allowUndeterminedFormatLanguage= */ false);
        if (score > 0) {
          bestMatchIndex = static_cast<int>(i);
          bestMatchScore = score;
          break;
        }
      }
      localeLanguageMatchIndex = bestMatchIndex;
      localeLanguageScore = bestMatchScore;
    }

    int compareTo(const std::unique_ptr<AudioTrackScore>& other) {
      if (other == nullptr) {
        return 1;
      }
      auto chain = ComparisonChain::start();
      chain = chain.compareFalseFirst(isWithinSelectionCapabilities,
        other->isWithinSelectionCapabilities);
      chain = chain.compareFalseFirst(preferredLanguageScore, other->preferredLanguageScore);
      chain = chain.compareFalseFirst(isWithinConstraints, other->isWithinConstraints);
      chain = chain.compare(parameters.forceLowestBitrate ? bitrate : 0,
        parameters.forceLowestBitrate ? other->bitrate : 0);
      chain = chain.compareFalseFirst(isDefaultSelectionFlag, other->isDefaultSelectionFlag);
      chain = chain.compare(other->localeLanguageMatchIndex, localeLanguageMatchIndex);
      chain = chain.compare(localeLanguageScore, other->localeLanguageScore);
      chain = chain.compare(channelCount, other->channelCount);
      chain = chain.compare(sampleRate, other->sampleRate);
      const auto sameLanguage = language == other->language;
      chain = chain.compare(sameLanguage ? (isWithinConstraints && isWithinSelectionCapabilities ? bitrate : other->bitrate) : 0,
        sameLanguage ? (isWithinConstraints && isWithinSelectionCapabilities ? other->bitrate : bitrate) : 0);
      return chain.result();
    }
  };

  /** Represents how well a text track matches the selection {@link Parameters}.
   */
  class TextTrackScore {
  public:
    /**
     * Whether the provided format is within the parameter constraints. If
     * {@code false}, the format should not be selected.
     */
    bool isWithinConstraints;

    bool isWithinSelectionCapabilities;
    bool isDefault;
    bool isForced;
    int preferredLanguageScore;
    int preferredRoleFlagsScore;
    int selectedAudioLanguageScore;
    bool hasCaptionRoleFlags;

    explicit TextTrackScore(const std::shared_ptr<MediaFormat>& format,
      const DefaultParameters& parameters,
      int trackFormatSupport,
      const std::string& selectedAudioLanguage) noexcept {
      isWithinSelectionCapabilities = isSupported(
        trackFormatSupport, /* allowExceedsCapabilities= */ false);
      auto maskedSelectionFlags = format->getSelectionFlags() &
                                  ~parameters.disabledTextTrackSelectionFlags;
      isDefault = (
                    maskedSelectionFlags &
                    MediaSelectionFlags::kMediaSelectionDefault) != 0;
      isForced = (
                   maskedSelectionFlags &
                   MediaSelectionFlags::kMediaSelectionForced) != 0;
      preferredLanguageScore =
        getFormatLanguageScore(format, parameters.preferredTextLanguage,
          parameters.selectUndeterminedTextLanguage);
      // A role a parameter asks for scores positively; the caption role flags are a tie breaker.
      preferredRoleFlagsScore = static_cast<int>(
        format->getRoleFlags() & static_cast<uint32_t>(parameters.preferredTextRoleFlags));
      hasCaptionRoleFlags =
        (format->getRoleFlags() &
          (static_cast<uint32_t>(RoleFlags::kRoleFlagCaption) |
            static_cast<uint32_t>(RoleFlags::kRoleFlagDescribesMusicAndSound))) != 0;
      auto selectedAudioLanguageUndetermined =
        normalizeUndeterminedLanguageToNull(selectedAudioLanguage).empty();
      selectedAudioLanguageScore = getFormatLanguageScore(
        format, selectedAudioLanguage, selectedAudioLanguageUndetermined);
      isWithinConstraints = preferredLanguageScore > 0 ||
                            (
                              parameters.preferredTextLanguage.empty() &&
                              preferredRoleFlagsScore > 0) ||
                            isDefault ||
                            (isForced && selectedAudioLanguageScore > 0);
    }

    int compareTo(const std::unique_ptr<TextTrackScore>& other) {
      if (other == nullptr) {
        return 1;
      }
      auto chain =
        ComparisonChain::start()
        .compareFalseFirst(isWithinSelectionCapabilities,
          other->isWithinSelectionCapabilities)
        .compare(preferredLanguageScore, other->preferredLanguageScore)
        .compare(preferredRoleFlagsScore, other->preferredRoleFlagsScore)
        .compareFalseFirst(isDefault, other->isDefault)
        .compareFalseFirst(
          (preferredLanguageScore == 0) ? isForced : other->isForced,
          (preferredLanguageScore == 0) ? other->isForced : isForced)
        .compare(selectedAudioLanguageScore,
          other->selectedAudioLanguageScore);
      if (preferredRoleFlagsScore == 0) {
        chain = chain.compareTrueFirst(hasCaptionRoleFlags,
          other->hasCaptionRoleFlags);
      }
      return chain.result();
    }
  };

  /**
   * Represents how well any other track (non video, audio or text) matches the
   * selection {@link Parameters}.
   */
  class OtherTrackScore {
  public:
    bool isDefault;
    bool isWithinSelectionCapabilities;

    explicit OtherTrackScore(const std::shared_ptr<MediaFormat>& format,
      int trackFormatSupport) noexcept {
      isDefault = (
                    format->getSelectionFlags() &
                    MediaSelectionFlags::kMediaSelectionDefault) != 0;
      isWithinSelectionCapabilities = isSupported(
        trackFormatSupport, /* allowExceedsCapabilities= */ false);
    }

    int compareTo(const std::unique_ptr<OtherTrackScore>& other) {
      if (other == nullptr) {
        return 1;
      }
      return ComparisonChain::start()
             .compareFalseFirst(this->isWithinSelectionCapabilities,
               other->isWithinSelectionCapabilities)
             .compareFalseFirst(isDefault, other->isDefault)
             .result();
    }
  };
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_DEFAULT_TRACK_SELECTOR_H
