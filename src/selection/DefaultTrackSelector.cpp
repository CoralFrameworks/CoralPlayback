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

#include <coral/selection/DefaultTrackSelector.h>
#include <coral/selection/AdaptiveTrackSelection.h>
#include <coral/upstream/DefaultBandwidthMeter.h>

#include <unordered_set>

namespace coral {

DefaultTrackSelector::DefaultTrackSelector()
  : DefaultTrackSelector(ParametersBuilder().build(),
    std::make_unique<AdaptiveTrackSelection::Factory>()) {
}

DefaultTrackSelector::DefaultTrackSelector(
  const DefaultParameters& parameters,
  std::unique_ptr<TrackSelection::Factory> factory) noexcept
  : trackSelectionFactory(factory != nullptr
                          ? std::move(factory)
                          : std::make_unique<AdaptiveTrackSelection::Factory>()),
    parameters(parameters) {
  // A selector always needs a bandwidth estimate: an adaptive selection asks its meter how much
  // bandwidth it may use, and a selector that the player never initialised - which is what a selector
  // an application uses on its own is - would otherwise hand a null meter to every adaptive selection
  // it creates. A player that initialises the selector replaces this one with its own.
  init(/* listener= */ nullptr, std::make_shared<DefaultBandwidthMeter>());
}

void DefaultTrackSelector::setParameters(const DefaultParameters& newParameters) {
  parameters = newParameters;
  // The selections that were made with the previous parameters no longer describe what the
  // application asked for, so the player is told to select the tracks again.
  invalidate();
}

void DefaultTrackSelector::setRendererDisabled(const int rendererIndex, const bool disabled) {
  setParameters(
      ParametersBuilder(parameters).setRendererDisabled(rendererIndex, disabled).build());
}

void DefaultTrackSelector::setSelectionOverride(const int rendererIndex,
    const std::optional<SelectionOverride>& override) {
  auto builder = ParametersBuilder(parameters);
  if (override.has_value()) {
    builder.setSelectionOverride(rendererIndex, override.value());
  } else {
    builder.clearSelectionOverride(rendererIndex);
  }
  setParameters(builder.build());
}

std::optional<SelectionOverride> DefaultTrackSelector::getSelectionOverride(
    const int rendererIndex) const {
  const auto position = parameters.selectionOverrides.find(rendererIndex);
  if (position == parameters.selectionOverrides.end()) {
    return std::nullopt;
  }
  return position->second;
}

bool DefaultTrackSelector::hasSelectionOverride(const int rendererIndex) const {
  return getSelectionOverride(rendererIndex).has_value();
}

std::pair<std::vector<std::shared_ptr<Configuration>>,
  std::vector<std::shared_ptr<TrackSelection>>>
DefaultTrackSelector::selectTracks(
  const std::shared_ptr<MappedTrackInfo>& mappedTrackInfo,
  std::vector<std::vector<std::vector<int>>> rendererFormatSupports,
  std::vector<int> rendererMixedMimeTypeAdaptationSupports) {

  auto params = parameters;
  auto const& rendererCount = mappedTrackInfo->getRendererCount();
  auto definitions =
    selectAllTracks(mappedTrackInfo, rendererFormatSupports,
      rendererMixedMimeTypeAdaptationSupports, params);

  // Apply track disabling and overriding.
  for (auto i = 0; i < rendererCount; ++i) {
    if (params.getRendererDisabled(i) || params.isTrackTypeDisabled(mappedTrackInfo->getRendererType(i))) {
      definitions[i] = nullptr;
      continue;
    }
    auto const& rendererTrackGroups = mappedTrackInfo->getTrackGroups(i);
    const auto override = params.getSelectionOverride(i, rendererTrackGroups);
    if (!override.has_value()) {
      continue;
    }
    // The override replaces whatever the selection picked, including with nothing: an override that
    // names no track is how an application deselects a renderer it keeps enabled.
    definitions[i] = override->tracks.empty()
                     ? nullptr
                     : std::make_unique<TrackSelection::Definition>(
                           rendererTrackGroups->get(override->groupIndex), override->tracks);
  }

  auto rendererTrackSelections = trackSelectionFactory->createTrackSelections(
    definitions, getBandwidthMeter());

  // Initialize the renderer configurations to the default configuration for all
  // renderers with selections, and null otherwise.
  auto rendererConfigurations =
    std::vector<std::shared_ptr<Configuration>>(rendererCount);
  for (auto i = 0; i < rendererCount; ++i) {
    auto forceRendererDisabled = params.getRendererDisabled(i) ||
                                 params.isTrackTypeDisabled(mappedTrackInfo->getRendererType(i));
    auto rendererEnabled =
      !forceRendererDisabled &&
      (
        mappedTrackInfo->getRendererType(i) == MediaType::kMediaTypeInvalid ||
        rendererTrackSelections[i] != nullptr);
    rendererConfigurations[i] =
      rendererEnabled ? std::make_shared<Configuration>() : nullptr;
  }

  // Configure audio and video renderers to use tunneling if appropriate.
  maybeConfigureRenderersForTunneling(
    mappedTrackInfo, rendererFormatSupports, rendererConfigurations,
    rendererTrackSelections, params.tunnelingAudioSessionId);

  return std::make_pair(rendererConfigurations, rendererTrackSelections);
}

std::vector<std::unique_ptr<TrackSelection::Definition>>
DefaultTrackSelector::selectAllTracks(
  const std::shared_ptr<MappedTrackInfo>& mappedTrackInfo,
  const std::vector<std::vector<std::vector<int>>>& rendererFormatSupports,
  const std::vector<int>& rendererMixedMimeTypeAdaptationSupports,
  const DefaultParameters& params) {
  int rendererCount = mappedTrackInfo->getRendererCount();
  auto definitions =
    std::vector<std::unique_ptr<TrackSelection::Definition>>(rendererCount);

  auto seenVideoRendererWithMappedTracks = false;
  auto selectedVideoTracks = false;
  for (auto i = 0; i < rendererCount; ++i) {
    if (MediaType::kMediaTypeVideo == mappedTrackInfo->getRendererType(i)) {
      if (!selectedVideoTracks) {
        definitions[i] = selectVideoTrack(
          mappedTrackInfo->getTrackGroups(i), rendererFormatSupports[i],
          rendererMixedMimeTypeAdaptationSupports[i], params,
          /* enableAdaptiveTrackSelection= */ true);
        selectedVideoTracks = definitions[i] != nullptr;
      }
      seenVideoRendererWithMappedTracks |=
        mappedTrackInfo->getTrackGroups(i)->length() > 0;
    }
  }

  std::unique_ptr<AudioTrackScore> selectedAudioTrackScore = nullptr;
  std::string selectedAudioLanguage = "";
  int selectedAudioRendererIndex = Definitions::kIndexUnset;
  for (auto i = 0; i < rendererCount; ++i) {
    if (MediaType::kMediaTypeAudio == mappedTrackInfo->getRendererType(i)) {
      auto enableAdaptiveTrackSelection =
        params.allowMultipleAdaptiveSelections || !seenVideoRendererWithMappedTracks;
      auto audioSelection = selectAudioTrack(
        mappedTrackInfo->getTrackGroups(i), rendererFormatSupports[i],
        rendererMixedMimeTypeAdaptationSupports[i], params,
        enableAdaptiveTrackSelection);
      if (audioSelection.first != nullptr && audioSelection.second != nullptr &&
          (
            selectedAudioTrackScore == nullptr ||
            audioSelection.second->compareTo(selectedAudioTrackScore) > 0)) {
        if (selectedAudioRendererIndex != Definitions::kIndexUnset) {
          // We've already made a selection for another audio renderer, but it
          // had a lower score. Clear the selection for that renderer.
          definitions[selectedAudioRendererIndex] = nullptr;
        }
        definitions[i] = std::move(audioSelection.first);
        // We assume that audio tracks in the same group have matching language.
        selectedAudioLanguage =
          definitions[i]
          ->group->getFormat(definitions[i]->tracks[0])
          ->getLanguage();
        selectedAudioTrackScore = std::move(audioSelection.second);
        selectedAudioRendererIndex = i;
      }
    }
  }

  std::unique_ptr<TextTrackScore> selectedTextTrackScore = nullptr;
  int selectedTextRendererIndex = Definitions::kIndexUnset;
  for (auto i = 0; i < rendererCount; ++i) {
    auto trackType = mappedTrackInfo->getRendererType(i);
    switch (trackType) {
      case MediaType::kMediaTypeVideo:
      case MediaType::kMediaTypeAudio:
        // Already done. Do nothing.
        break;
      case MediaType::kMediaTypeSubtitle: {
        auto textSelection = selectTextTrack(mappedTrackInfo->getTrackGroups(i),
          rendererFormatSupports[i], params,
          selectedAudioLanguage);
        if (textSelection.first != nullptr && textSelection.second != nullptr &&
            (
              selectedTextTrackScore == nullptr ||
              textSelection.second->compareTo(selectedTextTrackScore) > 0)) {
          if (selectedTextRendererIndex != Definitions::kIndexUnset) {
            // We've already made a selection for another text renderer, but it
            // had a lower score. Clear the selection for that renderer.
            definitions[selectedTextRendererIndex] = nullptr;
          }
          definitions[i] = std::move(textSelection.first);
          selectedTextTrackScore = std::move(textSelection.second);
          selectedTextRendererIndex = i;
        }
        break;
      }
      default: {
        definitions[i] =
          selectOtherTrack(trackType, mappedTrackInfo->getTrackGroups(i),
            rendererFormatSupports[i], params);
        break;
      }
    }
  }


  return definitions;
}

std::unique_ptr<TrackSelection::Definition>
DefaultTrackSelector::selectVideoTrack(
  const std::shared_ptr<TrackGroupArray>& groups,
  const std::vector<std::vector<int>>& formatSupport,
  int mixedMimeTypeAdaptationSupports, const DefaultParameters& params,
  bool enableAdaptiveTrackSelection) {
  std::unique_ptr<TrackSelection::Definition> definition = nullptr;
  if (!params.forceHighestSupportedBitrate && !params.forceLowestBitrate &&
      enableAdaptiveTrackSelection) {
    definition = selectAdaptiveVideoTrack(
      groups, formatSupport, mixedMimeTypeAdaptationSupports, params);
  }
  if (definition == nullptr) {
    definition = selectFixedVideoTrack(groups, formatSupport, params);
  }
  return definition;
}

std::unique_ptr<TrackSelection::Definition>
DefaultTrackSelector::selectAdaptiveVideoTrack(
  const std::shared_ptr<TrackGroupArray>& groups,
  const std::vector<std::vector<int>>& formatSupport,
  int mixedMimeTypeAdaptationSupports, const DefaultParameters& params) {
  auto requiredAdaptiveSupport =
    params.allowVideoNonSeamlessAdaptiveness
      ? (
        SelectionCapabilities::ADAPTIVE_NOT_SEAMLESS |
        SelectionCapabilities::ADAPTIVE_SEAMLESS)
      : SelectionCapabilities::ADAPTIVE_SEAMLESS;
  auto allowMixedMimeTypes =
    params.allowVideoMixedMimeTypeAdaptiveness &&
    (mixedMimeTypeAdaptationSupports & requiredAdaptiveSupport) == requiredAdaptiveSupport;
  for (auto i = 0; i < groups->length(); ++i) {
    auto const& group = groups->get(i);
    auto adaptiveTracks = getAdaptiveVideoTracksForGroup(
      group, formatSupport[i], allowMixedMimeTypes, requiredAdaptiveSupport,
      params.maxVideoWidth, params.maxVideoHeight, params.maxVideoFrameRate,
      params.maxVideoBitrate, params.minVideoWidth, params.minVideoHeight,
      params.minVideoFrameRate, params.minVideoBitrate, params.viewportWidth,
      params.viewportHeight, params.viewportOrientationMayChange);
    if (adaptiveTracks.size() > 0) {
      return std::make_unique<TrackSelection::Definition>(group,
        adaptiveTracks);
    }
  }
  return nullptr;
}

std::vector<int> DefaultTrackSelector::getAdaptiveVideoTracksForGroup(
  const std::shared_ptr<TrackGroup>& group,
  const std::vector<int>& formatSupport, bool allowMixedMimeTypes,
  int requiredAdaptiveSupport, int maxVideoWidth, int maxVideoHeight,
  int maxVideoFrameRate, int maxVideoBitrate, int minVideoWidth,
  int minVideoHeight, int minVideoFrameRate, int minVideoBitrate,
  int viewportWidth, int viewportHeight, bool viewportOrientationMayChange) {
  if (group->length() < 2) {
    return std::vector<int>(0);
  }
  auto selectedTrackIndices = getViewportFilteredTrackIndices(
    group, viewportWidth, viewportHeight, viewportOrientationMayChange);
  if (selectedTrackIndices.size() < 2) {
    return std::vector<int>(0);
  }

  std::string selectedMimeType = "";
  if (!allowMixedMimeTypes) {
    // Select the mime type for which we have the most adaptive tracks.
    auto seenMimeTypes = std::unordered_set<std::string>();
    auto selectedMimeTypeTrackCount = 0;
    for (auto i = 0; i < selectedTrackIndices.size(); ++i) {
      int trackIndex = selectedTrackIndices[i];
      auto sampleMimeType = group->getFormat(trackIndex)->getMimeType();
      if (!sampleMimeType.empty() && seenMimeTypes.count(sampleMimeType) == 0) {
        seenMimeTypes.insert(sampleMimeType);
        auto countForMimeType = getAdaptiveVideoTrackCountForMimeType(
          group, formatSupport, requiredAdaptiveSupport, sampleMimeType,
          maxVideoWidth, maxVideoHeight, maxVideoFrameRate, maxVideoBitrate,
          minVideoWidth, minVideoHeight, minVideoFrameRate, minVideoBitrate,
          selectedTrackIndices);
        if (countForMimeType > selectedMimeTypeTrackCount) {
          selectedMimeType = sampleMimeType;
          selectedMimeTypeTrackCount = countForMimeType;
        }
      }
    }
  }

  // Filter by the selected mime type.
  filterAdaptiveVideoTrackCountForMimeType(
    group, formatSupport, requiredAdaptiveSupport, selectedMimeType,
    maxVideoWidth, maxVideoHeight, maxVideoFrameRate, maxVideoBitrate,
    minVideoWidth, minVideoHeight, minVideoFrameRate, minVideoBitrate,
    selectedTrackIndices);

  return selectedTrackIndices.size() < 2
           ? std::vector<int>(0)
           : selectedTrackIndices;
}

int DefaultTrackSelector::getAdaptiveVideoTrackCountForMimeType(
  const std::shared_ptr<TrackGroup>& group,
  const std::vector<int>& formatSupport, int requiredAdaptiveSupport,
  std::string mimeType, int maxVideoWidth, int maxVideoHeight,
  int maxVideoFrameRate, int maxVideoBitrate, int minVideoWidth,
  int minVideoHeight, int minVideoFrameRate, int minVideoBitrate,
  const std::vector<int>& selectedTrackIndices) {
  auto adaptiveTrackCount = 0;
  for (auto i = 0; i < selectedTrackIndices.size(); ++i) {
    int trackIndex = selectedTrackIndices[i];
    if (isSupportedAdaptiveVideoTrack(
      group->getFormat(trackIndex), mimeType, formatSupport[trackIndex],
      requiredAdaptiveSupport, maxVideoWidth, maxVideoHeight,
      maxVideoFrameRate, maxVideoBitrate, minVideoWidth, minVideoHeight,
      minVideoFrameRate, minVideoBitrate)) {
      adaptiveTrackCount++;
    }
  }
  return adaptiveTrackCount;
}

void DefaultTrackSelector::filterAdaptiveVideoTrackCountForMimeType(
  const std::shared_ptr<TrackGroup>& group,
  const std::vector<int>& formatSupport, int requiredAdaptiveSupport,
  std::string mimeType, int maxVideoWidth, int maxVideoHeight,
  int maxVideoFrameRate, int maxVideoBitrate, int minVideoWidth,
  int minVideoHeight, int minVideoFrameRate, int minVideoBitrate,
  std::vector<int>& selectedTrackIndices) {
  auto position = selectedTrackIndices.begin();
  while (position != selectedTrackIndices.end()) {
    int trackIndex = *position;
    if (!isSupportedAdaptiveVideoTrack(
      group->getFormat(trackIndex), mimeType, formatSupport[trackIndex],
      requiredAdaptiveSupport, maxVideoWidth, maxVideoHeight,
      maxVideoFrameRate, maxVideoBitrate, minVideoWidth, minVideoHeight,
      minVideoFrameRate, minVideoBitrate)) {
      position = selectedTrackIndices.erase(position);
    } else {
      position++;
    }
  }
}

bool DefaultTrackSelector::isSupportedAdaptiveVideoTrack(
  const std::shared_ptr<MediaFormat>& format, std::string mimeType,
  int formatSupport, int requiredAdaptiveSupport, int maxVideoWidth,
  int maxVideoHeight, int maxVideoFrameRate, int maxVideoBitrate,
  int minVideoWidth, int minVideoHeight, int minVideoFrameRate,
  int minVideoBitrate) {
  //    if ((format.roleFlags & C.ROLE_FLAG_TRICK_PLAY) != 0) {
  //        // Ignore trick-play tracks for now.
  //        return false;
  //    }
  return isSupported((SelectionCapabilities::Capabilities) formatSupport,
           false) &&
         ((formatSupport & requiredAdaptiveSupport) != 0) &&
         (mimeType.empty() || format->getMimeType() == mimeType) &&
         (
           format->getWidth() == MediaFormat::NO_VALUE ||
           (
             minVideoWidth <= format->getWidth() &&
             format->getWidth() <= maxVideoWidth)) &&
         (
           format->getHeight() == MediaFormat::NO_VALUE ||
           (
             minVideoHeight <= format->getHeight() &&
             format->getHeight() <= maxVideoHeight)) &&
         (
           format->getFrameRate() == MediaFormat::NO_VALUE ||
           (
             minVideoFrameRate <= format->getFrameRate() &&
             format->getFrameRate() <= maxVideoFrameRate)) &&
         (
           format->getBitrate() == MediaFormat::NO_VALUE ||
           (
             minVideoBitrate <= format->getBitrate() &&
             format->getBitrate() <= maxVideoBitrate));
}

std::unique_ptr<TrackSelection::Definition>
DefaultTrackSelector::selectFixedVideoTrack(
  const std::shared_ptr<TrackGroupArray>& groups,
  const std::vector<std::vector<int>>& formatSupport,
  const DefaultParameters& params) {
  auto selectedTrackIndex = Definitions::kIndexUnset;
  std::shared_ptr<TrackGroup> selectedGroup = nullptr;
  std::unique_ptr<VideoTrackScore> selectedTrackScore = nullptr;
  for (auto groupIndex = 0; groupIndex < groups->length(); ++groupIndex) {
    auto const& trackGroup = groups->get(groupIndex);
    auto const& viewportFilteredTrackIndices = getViewportFilteredTrackIndices(
      trackGroup, params.viewportWidth, params.viewportHeight,
      params.viewportOrientationMayChange);
    auto const& trackFormatSupport = formatSupport[groupIndex];
    for (auto trackIndex = 0; trackIndex < trackGroup->length(); ++trackIndex) {
      auto const& format = trackGroup->getFormat(trackIndex);
      //            if ((format.roleFlags & C.ROLE_FLAG_TRICK_PLAY) != 0) {
      //                // Ignore trick-play tracks for now.
      //                continue;
      //            }
      if (isSupported(trackFormatSupport[trackIndex],
        params.exceedSelectionCapabilitiesIfNecessary)) {
        auto contains =
          std::find(viewportFilteredTrackIndices.begin(),
            viewportFilteredTrackIndices.end(),
            trackIndex) != viewportFilteredTrackIndices.end();
        auto trackScore = std::make_unique<VideoTrackScore>(
          format, params, trackFormatSupport[trackIndex], contains);
        if (!trackScore->isWithinMaxConstraints &&
            !params.exceedVideoConstraintsIfNecessary) {
          // Track should not be selected.
          continue;
        }
        if (selectedTrackScore == nullptr ||
            trackScore->compareTo(selectedTrackScore) > 0) {
          selectedGroup = trackGroup;
          selectedTrackIndex = trackIndex;
          selectedTrackScore = std::move(trackScore);
        }
      }
    }
  }

  return selectedGroup == nullptr
           ? nullptr
           : std::make_unique<TrackSelection::Definition>(selectedGroup,
             selectedTrackIndex);
}

std::pair<std::unique_ptr<TrackSelection::Definition>,
  std::unique_ptr<DefaultTrackSelector::AudioTrackScore>>
DefaultTrackSelector::selectAudioTrack(
  const std::shared_ptr<TrackGroupArray>& groups,
  const std::vector<std::vector<int>>& formatSupport,
  int mixedMimeTypeAdaptationSupports, const DefaultParameters& params,
  bool enableAdaptiveTrackSelection) {
  int selectedTrackIndex = Definitions::kIndexUnset;
  int selectedGroupIndex = Definitions::kIndexUnset;
  std::unique_ptr<AudioTrackScore> selectedTrackScore = nullptr;
  for (auto groupIndex = 0; groupIndex < groups->length(); ++groupIndex) {
    auto const& trackGroup = groups->get(groupIndex);
    auto const& trackFormatSupport = formatSupport[groupIndex];
    for (auto trackIndex = 0; trackIndex < trackGroup->length(); ++trackIndex) {
      if (isSupported(trackFormatSupport[trackIndex],
        params.exceedSelectionCapabilitiesIfNecessary)) {
        auto const& format = trackGroup->getFormat(trackIndex);
        auto trackScore = std::make_unique<AudioTrackScore>(
          format, params, trackFormatSupport[trackIndex]);
        if (!trackScore->isWithinConstraints &&
            !params.exceedAudioConstraintsIfNecessary) {
          // Track should not be selected.
          continue;
        }
        if (selectedTrackScore == nullptr ||
            trackScore->compareTo(selectedTrackScore) > 0) {
          selectedGroupIndex = groupIndex;
          selectedTrackIndex = trackIndex;
          selectedTrackScore = std::move(trackScore);
        }
      }
    }
  }

  if (selectedGroupIndex == Definitions::kIndexUnset) {
    return {};
  }

  auto const& selectedGroup = groups->get(selectedGroupIndex);

  std::unique_ptr<TrackSelection::Definition> definition = nullptr;
  if (!params.forceHighestSupportedBitrate && !params.forceLowestBitrate &&
      enableAdaptiveTrackSelection) {
    // If the group of the track with the highest score allows it, try to enable
    // adaptation.
    auto const& adaptiveTracks = getAdaptiveAudioTracks(
      selectedGroup, formatSupport[selectedGroupIndex], selectedTrackIndex,
      params.maxAudioBitrate, params.allowAudioMixedMimeTypeAdaptiveness,
      params.allowAudioMixedSampleRateAdaptiveness,
      params.allowAudioMixedChannelCountAdaptiveness);
    if (adaptiveTracks.size() > 1) {
      definition = std::make_unique<TrackSelection::Definition>(selectedGroup,
        adaptiveTracks);
    }
  }
  if (definition == nullptr) {
    // We didn't make an adaptive selection, so make a fixed one instead.
    definition = std::make_unique<TrackSelection::Definition>(
      selectedGroup, selectedTrackIndex);
  }
  return std::make_pair(std::move(definition), std::move(selectedTrackScore));
}

std::vector<int> DefaultTrackSelector::getAdaptiveAudioTracks(
  const std::shared_ptr<TrackGroup>& group,
  const std::vector<int>& formatSupport, int primaryTrackIndex,
  int maxAudioBitrate, bool allowMixedMimeTypeAdaptiveness,
  bool allowMixedSampleRateAdaptiveness,
  bool allowAudioMixedChannelCountAdaptiveness) {
  auto const& primaryFormat = group->getFormat(primaryTrackIndex);
  // An adaptive selection asks the renderer to switch between the formats it hands it, so it is only
  // created for a renderer that says it supports adaptation. The video path requires the same, and
  // without it a renderer that reports no adaptive support at all would be given an adaptive audio
  // selection whose switches it cannot perform.
  if (SelectionCapabilities::getAdaptiveSupport(
        static_cast<SelectionCapabilities::Capabilities>(formatSupport[primaryTrackIndex])) ==
      SelectionCapabilities::ADAPTIVE_NOT_SUPPORTED) {
    return std::vector<int>(0);
  }
  auto adaptiveIndices = std::vector<int>(group->length());
  int count = 0;
  for (auto i = 0; i < group->length(); ++i) {
    if (i == primaryTrackIndex ||
        isSupportedAdaptiveAudioTrack(
          group->getFormat(i), formatSupport[i], primaryFormat,
          maxAudioBitrate, allowMixedMimeTypeAdaptiveness,
          allowMixedSampleRateAdaptiveness,
          allowAudioMixedChannelCountAdaptiveness)) {
      adaptiveIndices[count++] = i;
    }
  }
  adaptiveIndices.resize(count);
  return adaptiveIndices;
}

bool DefaultTrackSelector::isSupportedAdaptiveAudioTrack(
  const std::shared_ptr<MediaFormat>& format, int formatSupport,
  const std::shared_ptr<MediaFormat>& primaryFormat, int maxAudioBitrate,
  bool allowMixedMimeTypeAdaptiveness, bool allowMixedSampleRateAdaptiveness,
  bool allowAudioMixedChannelCountAdaptiveness) {
  return isSupported(formatSupport, /* allowExceedsCapabilities= */ false) &&
         (format->getBitrate() == MediaFormat::NO_VALUE ||
          format->getBitrate() <= maxAudioBitrate) &&
         (allowAudioMixedChannelCountAdaptiveness ||
          (format->getChannelCount() != MediaFormat::NO_VALUE &&
           format->getChannelCount() == primaryFormat->getChannelCount())) &&
         (allowMixedMimeTypeAdaptiveness ||
          (!format->getMimeType().empty() &&
           format->getMimeType() == primaryFormat->getMimeType())) &&
         (allowMixedSampleRateAdaptiveness ||
          (format->getSampleRate() != MediaFormat::NO_VALUE &&
           format->getSampleRate() == primaryFormat->getSampleRate()));
}

std::pair<std::unique_ptr<TrackSelection::Definition>,
  std::unique_ptr<DefaultTrackSelector::TextTrackScore>>
DefaultTrackSelector::selectTextTrack(
  const std::shared_ptr<TrackGroupArray>& groups,
  const std::vector<std::vector<int>>& formatSupport,
  const DefaultParameters& params, const std::string& selectedAudioLanguage) {
  std::shared_ptr<TrackGroup> selectedGroup = nullptr;
  int selectedTrackIndex = Definitions::kIndexUnset;
  std::unique_ptr<TextTrackScore> selectedTrackScore = nullptr;
  for (auto groupIndex = 0; groupIndex < groups->length(); ++groupIndex) {
    auto const& trackGroup = groups->get(groupIndex);
    auto const& trackFormatSupport = formatSupport[groupIndex];
    for (auto trackIndex = 0; trackIndex < trackGroup->length(); ++trackIndex) {
      if (isSupported(trackFormatSupport[trackIndex],
        params.exceedSelectionCapabilitiesIfNecessary)) {
        auto const& format = trackGroup->getFormat(trackIndex);
        auto trackScore = std::make_unique<TextTrackScore>(
          format, params, trackFormatSupport[trackIndex],
          selectedAudioLanguage);
        if (trackScore->isWithinConstraints &&
            (
              selectedTrackScore == nullptr ||
              trackScore->compareTo(selectedTrackScore) > 0)) {
          selectedGroup = trackGroup;
          selectedTrackIndex = trackIndex;
          selectedTrackScore = std::move(trackScore);
        }
      }
    }
  }
  if (selectedGroup == nullptr) {
    return {};
  }
  return std::make_pair(std::make_unique<TrackSelection::Definition>(
      selectedGroup, selectedTrackIndex),
    std::move(selectedTrackScore));
}

std::unique_ptr<TrackSelection::Definition>
DefaultTrackSelector::selectOtherTrack(
  MediaType trackType, const std::shared_ptr<TrackGroupArray>& groups,
  const std::vector<std::vector<int>>& formatSupport,
  const DefaultParameters& params) {
  std::shared_ptr<TrackGroup> selectedGroup = nullptr;
  int selectedTrackIndex = Definitions::kIndexUnset;
  std::unique_ptr<OtherTrackScore> selectedTrackScore = nullptr;
  for (int groupIndex = 0; groupIndex < groups->length(); ++groupIndex) {
    auto const& trackGroup = groups->get(groupIndex);
    auto const& trackFormatSupport = formatSupport[groupIndex];
    for (auto trackIndex = 0; trackIndex < trackGroup->length(); ++trackIndex) {
      if (isSupported(trackFormatSupport[trackIndex],
        params.exceedSelectionCapabilitiesIfNecessary)) {
        auto const& format = trackGroup->getFormat(trackIndex);
        auto trackScore = std::make_unique<OtherTrackScore>(
          format, trackFormatSupport[trackIndex]);
        if (selectedTrackScore == nullptr ||
            trackScore->compareTo(selectedTrackScore) > 0) {
          selectedGroup = trackGroup;
          selectedTrackIndex = trackIndex;
          selectedTrackScore = std::move(trackScore);
        }
      }
    }
  }
  return selectedGroup == nullptr
           ? nullptr
           : std::make_unique<TrackSelection::Definition>(selectedGroup,
             selectedTrackIndex);
}

void DefaultTrackSelector::maybeConfigureRenderersForTunneling(
  const std::shared_ptr<MappedTrackInfo>& mappedTrackInfo,
  const std::vector<std::vector<std::vector<int>>>& rendererFormatSupports,
  std::vector<std::shared_ptr<Configuration>>& rendererConfigurations,
  const std::vector<std::shared_ptr<TrackSelection>>& trackSelections,
  int tunnelingAudioSessionId) {
  if (tunnelingAudioSessionId ==
      Configuration::AUDIO_SESSION_ID_UNSET) {
    return;
  }
  // Check whether we can enable tunneling. To enable tunneling we require
  // exactly one audio and one video renderer to support tunneling and have a
  // selection.
  int tunnelingAudioRendererIndex = -1;
  int tunnelingVideoRendererIndex = -1;
  auto enableTunneling = true;
  for (int i = 0; i < mappedTrackInfo->getRendererCount(); i++) {
    auto rendererType = mappedTrackInfo->getRendererType(i);
    auto const& trackSelection = trackSelections[i];
    if ((
          rendererType == MediaType::kMediaTypeAudio
          || rendererType == MediaType::kMediaTypeVideo) &&
        trackSelection != nullptr) {
      if (rendererSupportsTunneling(rendererFormatSupports[i],
        mappedTrackInfo->getTrackGroups(i),
        trackSelection)) {
        if (rendererType == MediaType::kMediaTypeAudio) {
          if (tunnelingAudioRendererIndex != -1) {
            enableTunneling = false;
            break;
          } else {
            tunnelingAudioRendererIndex = i;
          }
        } else {
          if (tunnelingVideoRendererIndex != -1) {
            enableTunneling = false;
            break;
          } else {
            tunnelingVideoRendererIndex = i;
          }
        }
      }
    }
  }
  enableTunneling &=
    tunnelingAudioRendererIndex != -1 && tunnelingVideoRendererIndex != -1;
  if (enableTunneling) {
    auto const& configuration =
      std::make_shared<Configuration>(tunnelingAudioSessionId);
    rendererConfigurations[tunnelingAudioRendererIndex] = configuration;
    rendererConfigurations[tunnelingVideoRendererIndex] = configuration;
  }
}

bool DefaultTrackSelector::rendererSupportsTunneling(
  const std::vector<std::vector<int>>& formatSupport,
  const std::shared_ptr<TrackGroupArray>& trackGroups,
  const std::shared_ptr<TrackSelection>& selection) {
  if (selection == nullptr || trackGroups == nullptr) {
    return false;
  }
  int trackGroupIndex = trackGroups->indexOf(selection->getTrackGroup());
  if (trackGroupIndex == Definitions::kIndexUnset) {
    // The selection does not belong to this renderer, so it cannot tunnel through it.
    return false;
  }
  for (auto i = 0; i < selection->length(); ++i) {
    auto trackFormatSupport =
      formatSupport[trackGroupIndex][selection->getIndexInTrackGroup(i)];
    if (SelectionCapabilities::getTunnelingSupport(
          (SelectionCapabilities::Capabilities) trackFormatSupport) !=
        SelectionCapabilities::TUNNELING_SUPPORTED) {
      return false;
    }
  }
  return true;
}

int DefaultTrackSelector::getFormatLanguageScore(
  const std::shared_ptr<MediaFormat>& format, std::string language,
  bool allowUndeterminedFormatLanguage) {
  if (!language.empty() && language == format->getLanguage()) {
    // Full literal match of non-empty languages, including matches of an
    // explicit "und" query.
    return 4;
  }
  language = normalizeUndeterminedLanguageToNull(language);
  auto formatLanguage =
    normalizeUndeterminedLanguageToNull(format->getLanguage());
  if (formatLanguage.empty() || language.empty()) {
    // At least one of the languages is undetermined.
    return allowUndeterminedFormatLanguage && formatLanguage.empty() ? 1 : 0;
  }
  if (startWith(formatLanguage, language) ||
      startWith(language, formatLanguage)) {
    // Partial match where one language is a subset of the other (e.g. "zh-hans"
    // and "zh-hans-hk")
    return 3;
  }
  auto formatMainLanguage = splitAtFirst(formatLanguage, "-")[0];
  auto queryMainLanguage = splitAtFirst(language, "-")[0];
  if (formatMainLanguage == queryMainLanguage) {
    // Partial match where only the main language tag is the same (e.g. "fr-fr"
    // and "fr-ca")
    return 2;
  }
  return 0;
}

std::vector<int> DefaultTrackSelector::getViewportFilteredTrackIndices(
  const std::shared_ptr<TrackGroup>& group, int viewportWidth,
  int viewportHeight, bool orientationMayChange) {
  // Initially include all indices, in order.
  auto selectedTrackIndices = std::vector<int>(static_cast<size_t>(group->length()));
  for (auto i = 0; i < group->length(); ++i) {
    selectedTrackIndices[static_cast<size_t>(i)] = i;
  }

  if (viewportWidth == INT_MAX || viewportHeight == INT_MAX) {
    // Viewport dimensions not set. Return the full set of indices.
    return selectedTrackIndices;
  }

  int maxVideoPixelsToRetain = INT_MAX;
  for (auto i = 0; i < group->length(); ++i) {
    auto const& format = group->getFormat(i);
    // Keep track of the number of pixels of the selected format whose
    // resolution is the smallest to exceed the maximum size at which it can be
    // displayed within the viewport. We'll discard formats of higher
    // resolution.
    if (format->getWidth() > 0 && format->getHeight() > 0) {
      auto maxVideoSizeInViewport = getMaxVideoSizeInViewport(
        orientationMayChange, viewportWidth, viewportHeight,
        format->getWidth(), format->getHeight());
      int videoPixels = format->getWidth() * format->getHeight();
      if (format->getWidth() >= (int) (
            maxVideoSizeInViewport.first *
            FRACTION_TO_CONSIDER_FULLSCREEN) &&
          format->getHeight() >= (int) (
            maxVideoSizeInViewport.second *
            FRACTION_TO_CONSIDER_FULLSCREEN)
          && videoPixels < maxVideoPixelsToRetain) {
        maxVideoPixelsToRetain = videoPixels;
      }
    }
  }

  // Filter out formats that exceed maxVideoPixelsToRetain. These formats have
  // an unnecessarily high resolution given the size at which the video will be
  // displayed within the viewport. Also filter out formats with unknown
  // dimensions, since we have some whose dimensions are known.
  if (maxVideoPixelsToRetain != INT_MAX) {
    auto position = selectedTrackIndices.begin();
    while (position != selectedTrackIndices.end()) {
      auto const& format = group->getFormat(*position);
      int pixelCount = format->getPixelCount();
      if (pixelCount == MediaFormat::NO_VALUE ||
          pixelCount > maxVideoPixelsToRetain) {
        position = selectedTrackIndices.erase(position);
      } else {
        position++;
      }
    }
  }

  return selectedTrackIndices;
}

std::pair<int, int>
DefaultTrackSelector::getMaxVideoSizeInViewport(bool orientationMayChange,
  int viewportWidth,
  int viewportHeight,
  int videoWidth,
  int videoHeight) {
  if (orientationMayChange &&
      (videoWidth > videoHeight) != (viewportWidth > viewportHeight)) {
    // Rotation is allowed, and the video will be larger in the rotated
    // viewport.
    int tempViewportWidth = viewportWidth;
    viewportWidth = viewportHeight;
    viewportHeight = tempViewportWidth;
  }

  if (videoWidth * viewportHeight >= videoHeight * viewportWidth) {
    // Horizontal letter-boxing along top and bottom.
    return { viewportWidth, ceilDivide(viewportWidth * videoHeight, videoWidth) };
  } else {
    // Vertical letter-boxing along edges.
    return { ceilDivide(viewportHeight * videoWidth, videoHeight), viewportHeight };
  }
}

} // namespace coral
