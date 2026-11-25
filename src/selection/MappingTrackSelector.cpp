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

#include <coral/selection/MappingTrackSelector.h>

#include <minilog/log.h>

#include <media/Types.h>

namespace coral {

MappedTrackInfo::MappedTrackInfo(
    const std::vector<const char*>& rendererNames,
    const std::vector<MediaType>& rendererTrackTypes,
    const std::vector<std::shared_ptr<TrackGroupArray>>& rendererTrackGroups,
    const std::vector<int>& rendererMixedMimeTypeAdaptiveSupports,
    const std::vector<std::vector<std::vector<int>>>& rendererFormatSupports,
    const std::shared_ptr<TrackGroupArray>& unmappedTrackGroups) noexcept
    : rendererNames(rendererNames),
      rendererTrackTypes(rendererTrackTypes),
      rendererTrackGroups(rendererTrackGroups),
      rendererMixedMimeTypeAdaptiveSupports(rendererMixedMimeTypeAdaptiveSupports),
      rendererFormatSupports(rendererFormatSupports),
      unmappedTrackGroups(unmappedTrackGroups) {}

MappedTrackInfo::~MappedTrackInfo() = default;

int MappedTrackInfo::getRendererSupport(int rendererIndex) {
  auto bestRendererSupport = RENDERER_SUPPORT_NO_TRACKS;
  auto const& rendererFormatSupport = rendererFormatSupports[rendererIndex];
  for (auto const& trackGroupFormatSupport: rendererFormatSupport) {
    for (auto const& trackFormatSupport: trackGroupFormatSupport) {
      // Initialised so that a support value that the switch does not enumerate cannot make the
      // result depend on whatever the stack held.
      int trackRendererSupport = RENDERER_SUPPORT_UNSUPPORTED_TRACKS;
      switch (SelectionCapabilities::getFormatSupport(
          (SelectionCapabilities::Capabilities)trackFormatSupport)) {
        case SelectionCapabilities::FORMAT_HANDLED: {
          return RENDERER_SUPPORT_PLAYABLE_TRACKS;
        }
        case SelectionCapabilities::FORMAT_EXCEEDS_CAPABILITIES: {
          trackRendererSupport = RENDERER_SUPPORT_EXCEEDS_CAPABILITIES_TRACKS;
          break;
        }
        case SelectionCapabilities::FORMAT_UNSUPPORTED_TYPE:
        case SelectionCapabilities::FORMAT_UNSUPPORTED_SUBTYPE:
        case SelectionCapabilities::FORMAT_UNSUPPORTED_DRM:
        default: {
          trackRendererSupport = RENDERER_SUPPORT_UNSUPPORTED_TRACKS;
          break;
        }
      }
      bestRendererSupport = std::max(bestRendererSupport, trackRendererSupport);
    }
  }
  return bestRendererSupport;
}

int MappedTrackInfo::getTypeSupport(MediaType trackType) {
  auto bestRendererSupport = RENDERER_SUPPORT_NO_TRACKS;
  for (auto i = 0; i < getRendererCount(); ++i) {
    if (rendererTrackTypes[i] == trackType) {
      bestRendererSupport =
          std::max(bestRendererSupport, getRendererSupport(i));
    }
  }
  return bestRendererSupport;
}

SelectionCapabilities::FormatSupport
MappedTrackInfo::getTrackSupport(int rendererIndex, int groupIndex,
    int trackIndex) {
  return SelectionCapabilities::getFormatSupport(
      (SelectionCapabilities::Capabilities)
          rendererFormatSupports[rendererIndex][groupIndex][trackIndex]);
}

SelectionCapabilities::AdaptiveSupport
MappedTrackInfo::getAdaptiveSupport(int rendererIndex, int groupIndex,
    bool includeCapabilitiesExceededTracks) {
  auto trackCount =
      rendererTrackGroups[rendererIndex]->get(groupIndex)->length();
  // Iterate over the tracks in the group, recording the indices of those to
  // consider.
  auto trackIndices = std::vector<int>(trackCount);
  auto trackIndexCount = 0;
  for (auto i = 0; i < trackCount; ++i) {
    auto fixedSupport = getTrackSupport(rendererIndex, groupIndex, i);
    if (fixedSupport == SelectionCapabilities::FORMAT_HANDLED ||
        (
            includeCapabilitiesExceededTracks &&
            fixedSupport == SelectionCapabilities::FORMAT_EXCEEDS_CAPABILITIES)) {
      trackIndices[trackIndexCount++] = i;
    }
  }
  trackIndices.resize(trackIndexCount);
  return getAdaptiveSupport(rendererIndex, groupIndex, trackIndices);
}

SelectionCapabilities::AdaptiveSupport
MappedTrackInfo::getAdaptiveSupport(int rendererIndex, int groupIndex,
    std::vector<int> trackIndices) {
  int handledTrackCount = 0;
  auto adaptiveSupport = SelectionCapabilities::ADAPTIVE_SEAMLESS;
  auto multipleMimeTypes = false;
  std::string firstSampleMimeType = "";
  for (size_t i = 0; i < trackIndices.size(); ++i) {
    auto const& trackIndex = trackIndices[i];
    auto sampleMimeType = rendererTrackGroups[rendererIndex]
        ->get(groupIndex)
        ->getFormat(trackIndex)
        ->getMimeType();
    if (handledTrackCount++ == 0) {
      firstSampleMimeType = sampleMimeType;
    } else {
      multipleMimeTypes |= firstSampleMimeType != sampleMimeType;
    }
    adaptiveSupport =
        std::min(adaptiveSupport,
            SelectionCapabilities::getAdaptiveSupport(
                (SelectionCapabilities::Capabilities)
                    rendererFormatSupports[rendererIndex][groupIndex][trackIndex]));
  }
  return multipleMimeTypes
         ? std::min(adaptiveSupport,
          (SelectionCapabilities::AdaptiveSupport)
              rendererMixedMimeTypeAdaptiveSupports[rendererIndex])
         : adaptiveSupport;
}

//------------------------------------------------------------------------------

std::shared_ptr<TrackSelectorResult> MappingTrackSelector::selectTracks(
    const std::vector<std::shared_ptr<SelectionCapabilities>>
    & rendererCapabilities,
    const std::shared_ptr<TrackGroupArray>& trackGroups) {

  // Structures into which data will be written during the selection. The extra
  // item at the end of each array is to store data associated with track groups
  // that cannot be associated with any renderer.
  auto trackSize = rendererCapabilities.size();
  auto rendererTrackGroupCounts = std::vector<int>(trackSize + 1);
  auto rendererTrackGroups =
      std::vector<std::vector<std::shared_ptr<TrackGroup>>>(trackSize + 1);
  auto rendererFormatSupports =
      std::vector<std::vector<std::vector<int>>>(trackSize + 1);
  for (size_t i = 0; i < rendererTrackGroupCounts.size(); ++i) {
    rendererTrackGroups[i] =
        std::vector<std::shared_ptr<TrackGroup>>(trackGroups->length());
    rendererFormatSupports[i] =
        std::vector<std::vector<int>>(trackGroups->length());
  }

  // Determine the extent to which each renderer supports mixed mimeType
  // adaptation.
  auto rendererMixedMimeTypeAdaptationSupports =
      getMixedMimeTypeAdaptationSupports(rendererCapabilities);

  // Associate each track group to a preferred renderer, and evaluate the
  // support that the renderer provides for each track in the group.
  for (auto groupIndex = 0; groupIndex < trackGroups->length(); ++groupIndex) {
    auto const& group = trackGroups->get(groupIndex);
    // Associate the group to a preferred renderer.
    auto preferUnassociatedRenderer =
        MimeTypes::getMediaType(group->getFormat(0)->getMimeType()) ==
        MediaType::kMediaTypeMetadata;
    auto rendererIndex =
        findRenderer(rendererCapabilities, group, rendererTrackGroupCounts,
            preferUnassociatedRenderer);
    // Evaluate the support that the renderer provides for each track in the
    // group.
    auto rendererFormatSupport =
        (rendererIndex == rendererCapabilities.size())
        ? std::vector<int>(group->length())
        : getFormatSupport(rendererCapabilities[rendererIndex], group);
    // Stash the results.
    int rendererTrackGroupCount = rendererTrackGroupCounts[rendererIndex];

    rendererTrackGroups[rendererIndex][rendererTrackGroupCount] = group;
    rendererFormatSupports[rendererIndex][rendererTrackGroupCount] =
        rendererFormatSupport;
    rendererTrackGroupCounts[rendererIndex]++;
  }

  // Create a track group array for each renderer, and trim each
  // rendererFormatSupports entry.
  auto rendererTrackGroupArrays = std::vector<std::shared_ptr<TrackGroupArray>>(
      rendererCapabilities.size());
  std::vector<const char*> rendererNames =
      std::vector<const char*>(rendererCapabilities.size());
  auto rendererTrackTypes = std::vector<MediaType>(rendererCapabilities.size());
  for (size_t i = 0; i < rendererCapabilities.size(); ++i) {
    auto rendererTrackGroupCount = rendererTrackGroupCounts[i];
    auto groupCount =
        std::min(rendererTrackGroupCount, (int)rendererTrackGroups[i].size());
    auto groups = std::vector<std::shared_ptr<TrackGroup>>(groupCount);
    for (auto groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
      groups[groupIndex] = rendererTrackGroups[i][groupIndex];
    }
    rendererTrackGroupArrays[i] = std::make_shared<TrackGroupArray>(groups);

    auto formatCount = std::min(rendererTrackGroupCount,
        (int)rendererFormatSupports[i].size());
    auto formatSupports = std::vector<std::vector<int>>(formatCount);
    for (auto formatIndex = 0; formatIndex < formatCount; ++formatIndex) {
      formatSupports[formatIndex] = rendererFormatSupports[i][formatIndex];
    }
    rendererFormatSupports[i] = formatSupports;
    rendererNames[i] = rendererCapabilities[i]->getName();
    rendererTrackTypes[i] = rendererCapabilities[i]->getMediaType();
  }

  // Create a track group array for track groups not mapped to a renderer.
  int unmappedTrackGroupCount =
      rendererTrackGroupCounts[rendererCapabilities.size()];
  auto const& trackGroup = rendererTrackGroups[rendererCapabilities.size()];
  auto unmappedGroupCount =
      std::min(unmappedTrackGroupCount, (int)trackGroup.size());
  auto unmappedGroups =
      std::vector<std::shared_ptr<TrackGroup>>(unmappedGroupCount);
  for (auto groupIndex = 0; groupIndex < unmappedGroupCount; ++groupIndex) {
    unmappedGroups[groupIndex] = trackGroup[groupIndex];
  }
  auto unmappedTrackGroupArray =
      std::make_shared<TrackGroupArray>(unmappedGroups);

  // Package up the track information and selections.
  auto mappedTrackInfo = std::make_shared<MappedTrackInfo>(
      rendererNames, rendererTrackTypes, rendererTrackGroupArrays,
      rendererMixedMimeTypeAdaptationSupports, rendererFormatSupports,
      unmappedTrackGroupArray);

  auto result = selectTracks(mappedTrackInfo, rendererFormatSupports,
      rendererMixedMimeTypeAdaptationSupports);
  validateSelections(mappedTrackInfo, result.second);
  return std::make_shared<TrackSelectorResult>(result.first, result.second,
      mappedTrackInfo);
}

void MappingTrackSelector::validateSelections(
    const std::shared_ptr<MappedTrackInfo>& mappedTrackInfo,
    std::vector<std::shared_ptr<TrackSelection>>& selections) {
  for (size_t rendererIndex = 0; rendererIndex < selections.size(); ++rendererIndex) {
    auto const& selection = selections[rendererIndex];
    if (selection == nullptr) {
      continue;
    }
    auto const& rendererTrackGroups = mappedTrackInfo->getTrackGroups(
        static_cast<int>(rendererIndex));
    const auto groupIndex = rendererTrackGroups->indexOf(selection->getTrackGroup());
    auto isValid = groupIndex != Definitions::kIndexUnset;
    if (isValid) {
      const auto trackCount = rendererTrackGroups->get(groupIndex)->length();
      for (auto i = 0; i < selection->length() && isValid; ++i) {
        const auto trackIndex = selection->getIndexInTrackGroup(i);
        isValid = trackIndex >= 0 && trackIndex < trackCount;
      }
    }
    if (!isValid) {
      MINILOG_ERROR(
          "The track selection of renderer {} does not belong to its mapped track info: the "
          "renderer is deselected", rendererIndex);
      selections[rendererIndex] = nullptr;
    }
  }
}

int MappingTrackSelector::findRenderer(
    const std::vector<std::shared_ptr<SelectionCapabilities>>& trackCapabilities,
    const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& rendererTrackGroupCounts,
    bool preferUnassociatedRenderer) {
  auto bestRendererIndex = trackCapabilities.size();
  int bestFormatSupportLevel = SelectionCapabilities::FORMAT_UNSUPPORTED_TYPE;
  auto bestRendererIsUnassociated = true;
  for (size_t rendererIndex = 0; rendererIndex < trackCapabilities.size();
       ++rendererIndex) {
    auto const& rendererCapability = trackCapabilities[rendererIndex];
    auto formatSupportLevel = SelectionCapabilities::FORMAT_UNSUPPORTED_TYPE;
    for (auto trackIndex = 0; trackIndex < group->length(); ++trackIndex) {
      auto trackFormatSupportLevel = SelectionCapabilities::getFormatSupport(
          rendererCapability->supportsFormat(group->getFormat(trackIndex)));
      formatSupportLevel =
          std::max(formatSupportLevel, trackFormatSupportLevel);
    }
    auto rendererIsUnassociated =
        (rendererTrackGroupCounts[rendererIndex] == 0);
    if (formatSupportLevel > bestFormatSupportLevel ||
        (
            formatSupportLevel == bestFormatSupportLevel &&
            preferUnassociatedRenderer && !bestRendererIsUnassociated &&
            rendererIsUnassociated)) {
      bestRendererIndex = rendererIndex;
      bestFormatSupportLevel = formatSupportLevel;
      bestRendererIsUnassociated = rendererIsUnassociated;
    }
  }
  return bestRendererIndex;
}

std::vector<int> MappingTrackSelector::getFormatSupport(
    const std::shared_ptr<SelectionCapabilities>& rendererCapabilities,
    const std::shared_ptr<TrackGroup>& group) {
  auto formatSupport = std::vector<int>(group->length());
  for (auto i = 0; i < group->length(); ++i) {
    formatSupport[i] =
        rendererCapabilities->supportsFormat(group->getFormat(i));
  }
  return formatSupport;
}

std::vector<int> MappingTrackSelector::getMixedMimeTypeAdaptationSupports(
    const std::vector<std::shared_ptr<SelectionCapabilities>>& rendererCapabilities) {
  auto mixedMimeTypeAdaptationSupport =
      std::vector<int>(rendererCapabilities.size());
  for (size_t i = 0; i < mixedMimeTypeAdaptationSupport.size(); ++i) {
    mixedMimeTypeAdaptationSupport[i] =
        rendererCapabilities[i]->supportsMixedMimeTypeAdaptation();
  }
  return mixedMimeTypeAdaptationSupport;
}

} // namespace coral
