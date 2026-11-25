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

#include <coral/selection/AdaptiveTrackSelection.h>
#include <coral/selection/FixedTrackSelection.h>

#include <core/time/SystemClock.h>
#include <coral/upstream/DefaultBandwidthMeter.h>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace coral {

/**
 * Returns the track indices ordered by increasing bitrate.
 *
 * <p>This is the order the ideal selection walks the tracks in: the first track whose bitrate fits the
 * bandwidth is then the highest one that fits. The order of the tracks of a group follows the
 * container, which is not necessarily by increasing bitrate, so it is established here rather than
 * assumed.
 *
 * @param group The group the indices belong to.
 * @param tracks The indices in the group, in any order.
 */
static std::vector<int> sortTracksByBitrate(const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& tracks) {
  auto sorted = tracks;
  std::stable_sort(sorted.begin(), sorted.end(), [&group](const int lhs, const int rhs) {
    const auto lhsBitrate = AdaptiveTrackSelection::getFormatBitrate(group->getFormat(lhs));
    const auto rhsBitrate = AdaptiveTrackSelection::getFormatBitrate(group->getFormat(rhs));
    if (lhsBitrate == rhsBitrate) {
      return lhs < rhs;
    }
    // A track whose bitrate is unknown cannot be ordered, so it goes last: it can still be selected
    // when nothing else fits, but it never hides a track whose bitrate is known.
    if (lhsBitrate == Definitions::kUnspecified) {
      return false;
    }
    if (rhsBitrate == Definitions::kUnspecified) {
      return true;
    }
    return lhsBitrate < rhsBitrate;
  });
  return sorted;
}

namespace internal {

/**
 * Converts all input values to Math.log(value).
 */
static std::vector<std::vector<double>>
getLogArrayValues(const std::vector<std::vector<int64_t>>& values) {
  auto logValues = std::vector<std::vector<double>>(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    logValues[i].resize(values[i].size());
    for (size_t j = 0; j < values[i].size(); ++j) {
      logValues[i][j] = values[i][j] == Definitions::kInvalidValue
                        ? 0
                        : std::log(values[i][j]);
    }
  }
  return logValues;
}

/**
 * Returns idealized switch points for each switch between consecutive track
 * selection bitrates.
 *
 * @param logBitrates Log bitrates with [selectionCount][formatCount].
 * @return Linearly distributed switch points in the range of [0.0-1.0].
 */
static std::vector<std::vector<double>>
getSwitchPoints(const std::vector<std::vector<double>>& logBitrates) {
  auto switchPoints = std::vector<std::vector<double>>(logBitrates.size());
  for (size_t i = 0; i < logBitrates.size(); ++i) {
    switchPoints[i].resize(logBitrates[i].size() - 1);
    if (switchPoints[i].empty()) {
      continue;
    }
    auto totalBitrateDiff =
        logBitrates[i][logBitrates[i].size() - 1] - logBitrates[i][0];
    for (size_t j = 0; j < logBitrates[i].size() - 1; ++j) {
      auto switchBitrate = 0.5 * (logBitrates[i][j] + logBitrates[i][j + 1]);
      switchPoints[i][j] =
          totalBitrateDiff == 0.0
          ? 1.0
          : (switchBitrate - logBitrates[i][0]) / totalBitrateDiff;
    }
  }
  return switchPoints;
}

/**
 * Returns total number of elements in a 2D array.
 */
static int countArrayElements(const std::vector<std::vector<double>>& array) {
  int count = 0;
  for (auto const& subArray: array) {
    count += (int)subArray.size();
  }
  return count;
}

/**
 * Sets checkpoint bitrates.
 *
 * @param checkpoints Output checkpoints with
 * [selectionIndex][checkpointIndex][2] where [0]=Total bitrate and
 * [1]=Allocated bitrate.
 * @param checkpointIndex The checkpoint index.
 * @param trackBitrates The track bitrates with [selectionIndex][trackIndex].
 * @param selectedTracks The indices of selected tracks for each selection for
 * this checkpoint.
 */
static void
setCheckpointValues(std::vector<std::vector<std::vector<int64_t>>>& checkpoints,
    int checkpointIndex,
    const std::vector<std::vector<int64_t>>& trackBitrates,
    const std::vector<int>& selectedTracks) {
  auto totalBitrate = 0;
  for (size_t i = 0; i < checkpoints.size(); ++i) {
    checkpoints[i][checkpointIndex][1] = trackBitrates[i][selectedTracks[i]];
    totalBitrate += checkpoints[i][checkpointIndex][1];
  }
  for (auto& points: checkpoints) {
    points[checkpointIndex][0] = totalBitrate;
  }
}

static std::vector<std::vector<std::vector<int64_t>>>
getAllocationCheckpoints(const std::vector<std::vector<int64_t>>& trackBitrates) {
  // Algorithm:
  //  1. Use log bitrates to treat all resolution update steps equally.
  //  2. Distribute switch points for each selection equally in the same
  //  [0.0-1.0] range.
  //  3. Switch up one format at a time in the order of the switch points.
  auto const& logBitrates = getLogArrayValues(trackBitrates);
  auto const& switchPoints = getSwitchPoints(logBitrates);

  // There will be (count(switch point) + 3) checkpoints:
  // [0] = all zero, [1] = minimum bitrates, [2-(end-1)] = up-switch points,
  // [end] = extra point to set slope for additional bitrate.
  int checkpointCount = countArrayElements(switchPoints) + 3;
  auto checkpoints =
      std::vector<std::vector<std::vector<int64_t>>>(
          logBitrates.size(),
          std::vector<std::vector<int64_t>>(checkpointCount, std::vector<int64_t>(2)));
  auto currentSelection = std::vector<int>(logBitrates.size());
  setCheckpointValues(checkpoints, /* checkpointIndex= */ 1, trackBitrates, currentSelection);
  for (auto checkpointIndex = 2; checkpointIndex < checkpointCount - 1; ++checkpointIndex) {
    auto nextUpdateIndex = 0;
    auto nextUpdateSwitchPoint = INT64_MAX;
    for (size_t i = 0; i < logBitrates.size(); i++) {
      if (currentSelection[i] + size_t(1) == logBitrates[i].size()) {
        continue;
      }
      auto switchPoint = switchPoints[i][currentSelection[i]];
      if (switchPoint < nextUpdateSwitchPoint) {
        nextUpdateSwitchPoint = switchPoint;
        nextUpdateIndex = i;
      }
    }
    currentSelection[nextUpdateIndex]++;
    setCheckpointValues(checkpoints, checkpointIndex, trackBitrates, currentSelection);
  }
  for (auto& points: checkpoints) {
    points[checkpointCount - 1][0] = 2 * points[checkpointCount - 2][0];
    points[checkpointCount - 1][1] = 2 * points[checkpointCount - 2][1];
  }
  return checkpoints;
}

} // namespace internal

std::vector<std::shared_ptr<TrackSelection>>
AdaptiveTrackSelection::Factory::createTrackSelections(
    const std::vector<std::unique_ptr<Definition>>& definitions,
    const std::shared_ptr<BandwidthMeter>& bandwidthMeter) {

  auto selections = std::vector<std::shared_ptr<TrackSelection>>(definitions.size());
  int totalFixedBandwidth = 0;
  for (size_t i = 0; i < definitions.size(); ++i) {
    auto const& definition = definitions[i];
    if (definition != nullptr && definition->tracks.size() == 1) {
      // Make fixed selections first to know their total bandwidth.
      selections[i] =
          std::make_shared<FixedTrackSelection>(definition->group, definition->tracks[0]);
      int trackBitrate = definition->group->getFormat(definition->tracks[0])->getBitrate();
      if (trackBitrate != Definitions::kInvalidValue) {
        totalFixedBandwidth += trackBitrate;
      }
    }
  }

  auto adaptiveSelections = std::vector<std::shared_ptr<AdaptiveTrackSelection>>();
  for (size_t i = 0; i < definitions.size(); ++i) {
    auto const& definition = definitions[i];
    if (definition != nullptr && definition->tracks.size() > 1) {
      auto const& adaptiveSelection =
          createAdaptiveTrackSelection(
              definition->group, bandwidthMeter, definition->tracks, totalFixedBandwidth);
      adaptiveSelections.push_back(adaptiveSelection);
      selections[i] = adaptiveSelection;
    }
  }

  if (adaptiveSelections.size() > 1) {
    auto adaptiveTrackBitrates = std::vector<std::vector<int64_t>>(adaptiveSelections.size());
    for (size_t i = 0; i < adaptiveSelections.size(); ++i) {
      auto const& adaptiveSelection = adaptiveSelections[i];
      adaptiveTrackBitrates[i].resize(adaptiveSelection->length());
      for (auto j = 0; j < adaptiveSelection->length(); ++j) {
        adaptiveTrackBitrates[i][j] =
            adaptiveSelection->getFormat(adaptiveSelection->length() - j - 1)->getBitrate();
      }
    }
    auto bandwidthCheckpoints =
        internal::getAllocationCheckpoints(adaptiveTrackBitrates);
    for (auto i = 0; i < adaptiveSelections.size(); ++i) {
      adaptiveSelections[i]->experimentalSetBandwidthAllocationCheckpoints(bandwidthCheckpoints[i]);
    }
  }
  return selections;
}

std::shared_ptr<AdaptiveTrackSelection>
AdaptiveTrackSelection::Factory::createAdaptiveTrackSelection(
    const std::shared_ptr<TrackGroup>& group,
    const std::shared_ptr<BandwidthMeter>& bandwidthMeter,
    const std::vector<int>& tracks, int totalFixedTrackBandwidth) {
  return std::make_shared<AdaptiveTrackSelection>(
      group, tracks,
      std::make_unique<DefaultBandwidthProvider>(
          bandwidthMeter, bandwidthFraction, totalFixedTrackBandwidth),
      minDurationForQualityIncreaseMs, maxDurationForQualityDecreaseMs,
      minDurationToRetainAfterDiscardMs,
      bufferedFractionToLiveEdgeForQualityIncrease);
}

// -------------------------------------------------------------------------------------------------

AdaptiveTrackSelection::AdaptiveTrackSelection(
    const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& tracks,
    const std::shared_ptr<BandwidthMeter>& meter) noexcept
    : AdaptiveTrackSelection(
    group, sortTracksByBitrate(group, tracks), meter, 0,
    DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS,
    DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS,
    DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS,
    DEFAULT_BANDWIDTH_FRACTION,
    DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE) {}

AdaptiveTrackSelection::AdaptiveTrackSelection(
    const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& tracks,
    const std::shared_ptr<BandwidthMeter>& meter,
    int64_t reservedBandwidth,
    int64_t minDurationForQualityIncreaseMs,
    int64_t maxDurationForQualityDecreaseMs,
    int64_t minDurationToRetainAfterDiscardMs,
    float bandwidthFraction,
    float bufferedFraction) noexcept
    : AdaptiveTrackSelection(
    group,
    sortTracksByBitrate(group, tracks),
    std::make_unique<DefaultBandwidthProvider>(meter, bandwidthFraction, reservedBandwidth),
    minDurationForQualityIncreaseMs,
    maxDurationForQualityDecreaseMs,
    minDurationToRetainAfterDiscardMs,
    bufferedFraction) {}

AdaptiveTrackSelection::AdaptiveTrackSelection(
    const std::shared_ptr<TrackGroup>& group,
    const std::vector<int>& tracks,
    std::unique_ptr<DefaultBandwidthProvider> provider,
    int64_t minDurationForQualityIncreaseMs,
    int64_t maxDurationForQualityDecreaseMs,
    int64_t minDurationToRetainAfterDiscardMs,
    float bufferedFraction) noexcept
    : BaseTrackSelection(group, tracks),
      bandwidthProvider(std::move(provider)),
      minDurationForQualityIncreaseUs(minDurationForQualityIncreaseMs * 1000),
      maxDurationForQualityDecreaseUs(maxDurationForQualityDecreaseMs * 1000),
      minDurationToRetainAfterDiscardUs(minDurationToRetainAfterDiscardMs * 1000),
      bufferedFractionToLiveEdgeForQualityIncrease(bufferedFraction),
      playbackSpeed(1.0f),
      selectedIndex(0),
      lastBufferEvaluationMs(Definitions::kTimeUnset) {
  // The tracks are ordered by increasing bitrate and the selection starts at the one the current
  // bandwidth estimate allows, so a selection that is never updated still reports a sensible track
  // instead of always the first one.
  selectedIndex = determineIdealSelectedIndex(Definitions::kTimeUnset,
      /* enableBackBuffer= */ false);
}

void AdaptiveTrackSelection::experimentalSetBandwidthAllocationCheckpoints(
    const std::vector<std::vector<int64_t>>& allocationCheckpoints) {
  bandwidthProvider->experimentalSetBandwidthAllocationCheckpoints(allocationCheckpoints);
}

void AdaptiveTrackSelection::enable() {
  lastBufferEvaluationMs = Definitions::kTimeUnset;
  // A selection that is enabled again evaluates its queue at once, like the one that was just
  // created.
  pendingInitialEvaluation = true;
}

void AdaptiveTrackSelection::disable() {
  BaseTrackSelection::disable();
}

int AdaptiveTrackSelection::getSelectedIndex() const {
  return selectedIndex;
}

void AdaptiveTrackSelection::onPlaybackSpeed(float speed) {
  this->playbackSpeed = speed;
}

void AdaptiveTrackSelection::onPlayWhenReadyChanged(const bool playWhenReady) {
  this->playWhenReady = playWhenReady;
}

bool AdaptiveTrackSelection::canSelectFormat(const std::shared_ptr<MediaFormat>& format,
    const int64_t formatBitrate, const int64_t effectiveBitrate) {
  static_cast<void>(format);
  if (formatBitrate == Definitions::kUnspecified) {
    // A track whose bitrate is unknown cannot be ruled out, so it is allowed.
    return true;
  }
  return formatBitrate <= effectiveBitrate;
}

int64_t AdaptiveTrackSelection::getFormatBitrate(const std::shared_ptr<MediaFormat>& format) {
  if (format == nullptr) {
    return Definitions::kUnspecified;
  }
  if (format->getAverageBitrate() > 0) {
    return format->getAverageBitrate();
  }
  if (format->getPeakBitrate() > 0) {
    return format->getPeakBitrate();
  }
  return Definitions::kUnspecified;
}

int AdaptiveTrackSelection::determineIdealSelectedIndex(const int64_t nowMs,
    const bool enableBackBuffer) {
  static_cast<void>(enableBackBuffer);
  const auto effectiveBitrate = bandwidthProvider->getAllocatedBandwidth();
  // The tracks are ordered by increasing bitrate, so walking them from the smallest keeps the last one
  // that fits, which is the highest one that fits, and the first one that does not fit means that no
  // larger one fits either.
  auto bestIndex = 0;
  for (auto i = 0; i < length(); ++i) {
    if (isTrackExcluded(i, nowMs)) {
      // A track whose media could not be read is not selected while it is excluded. It is skipped
      // rather than treated as too large, so the walk still considers the larger tracks.
      continue;
    }
    const auto& format = getFormat(i);
    const auto formatBitrate = getFormatBitrate(format);
    if (formatBitrate == Definitions::kUnspecified) {
      // A track whose bitrate is unknown cannot be ruled out, so it is allowed.
      bestIndex = i;
      continue;
    }
    // A playback speed above 1 reads the media that much faster, so a track of a given bitrate needs
    // that many times the bandwidth: the requirement of the track is scaled, not its bitrate.
    const auto requiredBitrate =
        static_cast<int64_t>(static_cast<float>(formatBitrate) * std::max(1.0f, playbackSpeed));
    if (!canSelectFormat(format, requiredBitrate, effectiveBitrate)) {
      break;
    }
    bestIndex = i;
  }
  // A track the bandwidth cannot sustain is still better than no track at all, so the smallest one is
  // returned when nothing fits: the renderers can only be given a format.
  return bestIndex;
}

bool AdaptiveTrackSelection::isTrackExcluded(const int index, const int64_t nowMs) const {
  if (index < 0 || index >= static_cast<int>(blacklistUntilMs.size())) {
    return false;
  }
  return blacklistUntilMs[static_cast<size_t>(index)] > nowMs;
}

void AdaptiveTrackSelection::maybeBlacklistTrack(const int index, const int64_t exclusionDurationMs,
    const int64_t nowMs) {
  if (index < 0 || index >= length() || exclusionDurationMs <= 0) {
    return;
  }
  if (blacklistUntilMs.size() < static_cast<size_t>(length())) {
    blacklistUntilMs.resize(static_cast<size_t>(length()), 0);
  }
  // A report never shortens an exclusion that is already in force.
  blacklistUntilMs[static_cast<size_t>(index)] =
      std::max(blacklistUntilMs[static_cast<size_t>(index)], nowMs + exclusionDurationMs);
}

bool AdaptiveTrackSelection::evaluateQueueSize(const int64_t playbackPositionUs,
    const int64_t bufferedDurationUs) {
  static_cast<void>(playbackPositionUs);
  const auto nowMs = SystemClock::CurrentTimeMillis();
  // The first evaluation is not rate limited: `lastBufferEvaluationMs` starts unset, and subtracting
  // it would overflow, so the initial evaluation is tracked by its own flag like ExoPlayer's
  // `pendingInitialEvaluation`.
  if (!pendingInitialEvaluation &&
      nowMs - lastBufferEvaluationMs <
        minDurationForQualityIncreaseUs / Definitions::kMicrosPerMillisecond) {
    // The queue is evaluated at most once per `minDurationForQualityIncreaseMs`, so that a burst of
    // requests does not discard media the selection may still need.
    return false;
  }
  lastBufferEvaluationMs = nowMs;
  pendingInitialEvaluation = false;
  // Nothing is discarded while the buffer is shorter than the media that has to be kept behind the
  // playback position.
  return bufferedDurationUs >= minDurationToRetainAfterDiscardUs;
}

void AdaptiveTrackSelection::updateSelectedTrack(const int64_t playbackPositionUs,
    const int64_t bufferedDurationUs, const int64_t availableDurationUs, const int queueSize) {
  static_cast<void>(playbackPositionUs);
  static_cast<void>(queueSize);
  const auto nowMs = SystemClock::CurrentTimeMillis();
  const auto newSelectedIndex = determineIdealSelectedIndex(nowMs, /* enableBackBuffer= */ true);
  if (newSelectedIndex == selectedIndex) {
    return;
  }
  if (newSelectedIndex > selectedIndex) {
    // Switching up needs enough media buffered to absorb the switch: a larger track is read more
    // slowly than the current one, so switching too early would starve the renderers. The duration
    // that is required is the part of the media that is ahead of the buffer that the player does not
    // intend to keep, which is what ExoPlayer uses.
    auto minDurationForQualityIncreaseUs = this->minDurationForQualityIncreaseUs;
    if (availableDurationUs != Definitions::kTimeEndOfSource &&
        availableDurationUs != Definitions::kTimeUnset &&
        bufferedFractionToLiveEdgeForQualityIncrease < 1.0f) {
      const auto durationToLiveEdgeUs = availableDurationUs - bufferedDurationUs;
      minDurationForQualityIncreaseUs = static_cast<int64_t>(
        static_cast<float>(durationToLiveEdgeUs) * bufferedFractionToLiveEdgeForQualityIncrease);
    }
    if (bufferedDurationUs < minDurationForQualityIncreaseUs) {
      return;
    }
  }
  selectedIndex = newSelectedIndex;
  lastBufferEvaluationMs = nowMs;
}

// -------------------------------------------------------------------------------------------------

AdaptiveTrackSelection::DefaultBandwidthProvider::DefaultBandwidthProvider(
    const std::shared_ptr<BandwidthMeter>& bandwidthMeter,
    const float bandwidthFraction,
    const int64_t reservedBandwidth) noexcept
    : bandwidthMeter(bandwidthMeter),
      bandwidthFraction(bandwidthFraction),
      reservedBandwidth(reservedBandwidth) {}

int64_t AdaptiveTrackSelection::DefaultBandwidthProvider::getAllocatedBandwidth() {
  // A selection that was created without a meter cannot learn how much bandwidth it may use, so it
  // allocates the default estimate rather than dereferencing a null meter.
  const auto bitrateEstimate = bandwidthMeter != nullptr
                               ? bandwidthMeter->getBitrateEstimate()
                               : int64_t(DefaultBandwidthMeter::DEFAULT_INITIAL_BITRATE_ESTIMATE);
  auto totalBandwidth = (int64_t)(bitrateEstimate * bandwidthFraction);
  auto allocatableBandwidth = std::max(int64_t(0), totalBandwidth - reservedBandwidth);
  if (allocationCheckpoints.empty()) {
    return allocatableBandwidth;
  }
  size_t nextIndex = 1;
  while (nextIndex < allocationCheckpoints.size() - 1
         && allocationCheckpoints[nextIndex][0] < allocatableBandwidth) {
    nextIndex++;
  }
  auto previous = allocationCheckpoints[nextIndex - 1];
  auto next = allocationCheckpoints[nextIndex];
  float fractionBetweenCheckpoints =
      (float)(allocatableBandwidth - previous[0]) / (float)(next[0] - previous[0]);
  return previous[1] + (int64_t)(fractionBetweenCheckpoints * (float)(next[1] - previous[1]));
}

void AdaptiveTrackSelection::DefaultBandwidthProvider::
experimentalSetBandwidthAllocationCheckpoints(
    const std::vector<std::vector<int64_t>>& checkpoints) {
  assert(checkpoints.size() >= 2);
  this->allocationCheckpoints = checkpoints;
}

} // namespace coral
