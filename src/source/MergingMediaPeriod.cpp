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

#include <coral/source/MergingMediaPeriod.h>

#include <algorithm>
#include <limits>

namespace coral {

class MergingMediaPeriod::ChildCallback final : public MediaPeriod::Callback {
public:
  ChildCallback(std::shared_ptr<MergingMediaPeriod> owner) : owner(std::move(owner)) {}

  void onPrepared(const std::shared_ptr<MediaPeriod>&) override {
    if (auto period = owner.lock()) period->onChildPrepared();
  }

  void onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>&) override {
    if (auto period = owner.lock(); period != nullptr && period->callback != nullptr) {
      period->callback->onContinueLoadingRequested(period);
    }
  }

private:
  std::weak_ptr<MergingMediaPeriod> owner;
};

MergingMediaPeriod::MergingMediaPeriod(std::vector<std::shared_ptr<MediaPeriod>> periods,
    std::shared_ptr<TrackGroupArray> trackGroups, std::vector<SubtitleMetadata> metadata)
    : periods(std::move(periods)), metadata(std::move(metadata)),
      trackGroups(std::move(trackGroups)) {
}

void MergingMediaPeriod::prepare(Callback* callback, int64_t positionUs) {
  this->callback = callback;
  preparePositionUs = positionUs;
  preparedCount = 0;
  childCallbacks.clear();
  for (const auto& period: periods) {
    auto child = std::make_shared<ChildCallback>(shared_from_this());
    childCallbacks.push_back(child);
    period->prepare(child.get(), positionUs);
  }
}

void MergingMediaPeriod::onChildPrepared() {
  if (++preparedCount < static_cast<int>(periods.size())) {
    return;
  }
  std::vector<std::shared_ptr<TrackGroup>> groups;
  for (size_t index = 0; index < periods.size(); ++index) {
    const auto& childGroups = periods[index]->getTrackGroups();
    if (childGroups == nullptr) continue;
    const bool hasMetadata = index < metadata.size() && metadata[index].present;
    for (int i = 0; i < childGroups->length(); ++i) {
      const auto& group = childGroups->get(i);
      if (!hasMetadata || group == nullptr ||
          group->mediaType() != MediaType::kMediaTypeSubtitle) {
        groups.push_back(group);
        continue;
      }
      // The group pointer is what a TrackSelection is matched against, so the formats are
      // replaced in place rather than wrapped in a new group: a selection created from a copied
      // group would not be found by the child period.
      auto& formats = const_cast<std::vector<std::shared_ptr<MediaFormat>>&>(group->getFormats());
      for (auto& format: formats) {
        auto updated = std::make_shared<MediaFormat>(*format);
        if (!metadata[index].mimeType.empty()) updated->setMimeType(metadata[index].mimeType);
        if (!metadata[index].language.empty()) updated->setLanguage(metadata[index].language);
        if (!metadata[index].label.empty()) updated->setLabel(metadata[index].label);
        if (metadata[index].roleFlags != 0) {
          updated->setRoleFlags(metadata[index].roleFlags);
        }
        if (metadata[index].selectionFlags != 0) {
          updated->setSelectionFlags(static_cast<MediaSelectionFlags>(
            metadata[index].selectionFlags));
        }
        if (!metadata[index].id.empty()) {
          // Do not use std::stoi here: the Android build disables C++ exceptions, so a non-numeric
          // id such as "styled-ass" would abort the process instead of throwing a catchable error.
          int id = 0;
          bool valid = true;
          for (const char c: metadata[index].id) {
            if (c < '0' || c > '9') {
              valid = false;
              break;
            }
            id = id * 10 + (c - '0');
          }
          updated->setId(valid ? id : 0);
        }
        format = std::move(updated);
      }
      groups.push_back(group);
    }
  }
  trackGroups = std::make_shared<TrackGroupArray>(groups);
  if (callback != nullptr) callback->onPrepared(shared_from_this());
}

void MergingMediaPeriod::maybeThrowPrepareError() {
  for (const auto& period: periods) period->maybeThrowPrepareError();
}

std::string MergingMediaPeriod::getPrepareError() const {
  for (const auto& period: periods) {
    if (period == nullptr) {
      continue;
    }
    auto error = period->getPrepareError();
    if (!error.empty()) {
      return error;
    }
  }
  return {};
}

const std::shared_ptr<TrackGroupArray>& MergingMediaPeriod::getTrackGroups() const {
  return trackGroups;
}

MergingMediaPeriod::~MergingMediaPeriod() {
  // The child periods announce their results through the ChildCallback objects this period owns.
  // The children may still be loading when this period is dropped, and the loading task keeps a
  // child alive; releasing the children here stops them and clears the callback they hold, before
  // childCallbacks is destroyed with the other members.
  releaseChildPeriods();
}

void MergingMediaPeriod::release() {
  releaseChildPeriods();
}

void MergingMediaPeriod::releaseChildPeriods() {
  // The child periods belong to the child sources, but this period is their callback owner: a child
  // that is still loading would report into this period after it was destroyed. Releasing them
  // stops their loaders and clears the callback before the reference is dropped.
  for (const auto& period: periods) {
    if (period != nullptr) {
      period->release();
    }
  }
  periods.clear();
  streamOwners.clear();
}
int64_t MergingMediaPeriod::selectTracks(
    const std::vector<std::shared_ptr<TrackSelection>>& selections,
    const std::vector<bool>& mayRetainStreamFlags,
    std::vector<std::shared_ptr<SampleStream>>& streams,
    std::vector<bool>& streamResetFlags, int64_t positionUs) {
  const size_t count = selections.size();
  if (periods.empty()) {
    for (auto& stream: streams) stream = nullptr;
    streamOwners.assign(count, -1);
    return positionUs;
  }
  const auto oldStreams = streams;
  const auto oldOwners = streamOwners;
  if (streamOwners.size() < count) streamOwners.resize(count, -1);

  std::vector<int> selectionChild(count, -1);
  std::vector<std::vector<std::shared_ptr<SampleStream>>> childResults(
      periods.size(), std::vector<std::shared_ptr<SampleStream>>(count));
  std::vector<std::vector<bool>> childResets(periods.size(), std::vector<bool>(count, false));

  int64_t result = positionUs;
  size_t childIndex = 0;
  for (const auto& period: periods) {
    const auto& groups = period->getTrackGroups();
    std::vector<std::shared_ptr<TrackSelection>> childSelections(count);
    std::vector<std::shared_ptr<SampleStream>> childStreams(count);
    std::vector<bool> childResetFlags(count, false);
    for (size_t i = 0; i < count; ++i) {
      const int oldOwner = i < oldOwners.size() ? oldOwners[i] : -1;
      if (selections[i] != nullptr && groups != nullptr &&
          groups->indexOf(selections[i]->getTrackGroup()) >= 0) {
        selectionChild[i] = static_cast<int>(childIndex);
        childSelections[i] = selections[i];
        // The stream only belongs to this child when it was created here. A stream that moved
        // from another child is deselected there and created fresh here.
        if (oldOwner < 0 || oldOwner == static_cast<int>(childIndex)) {
          childStreams[i] = i < oldStreams.size() ? oldStreams[i] : nullptr;
        }
      } else if (oldOwner == static_cast<int>(childIndex)) {
        // The selection moved away; this child still has to deselect its old stream.
        childStreams[i] = i < oldStreams.size() ? oldStreams[i] : nullptr;
      }
    }
    const int64_t childResult = period->selectTracks(childSelections, mayRetainStreamFlags,
      childStreams, childResetFlags, positionUs);
    if (&period == &periods.front()) result = childResult;
    for (size_t i = 0; i < count; ++i) {
      childResults[childIndex][i] = childStreams[i];
      childResets[childIndex][i] = childResetFlags[i];
    }
    ++childIndex;
  }

  for (size_t i = 0; i < count; ++i) {
    const int oldOwner = i < oldOwners.size() ? oldOwners[i] : -1;
    if (selectionChild[i] >= 0) {
      const auto selectedChild = static_cast<size_t>(selectionChild[i]);
      streams[i] = childResults[selectedChild][i];
      streamResetFlags[i] = childResets[selectedChild][i];
      streamOwners[i] = streams[i] != nullptr ? static_cast<int>(selectedChild) : -1;
    } else if (oldOwner >= 0) {
      streams[i] = nullptr;
      streamResetFlags[i] = childResets[static_cast<size_t>(oldOwner)][i];
      streamOwners[i] = -1;
    } else {
      streams[i] = nullptr;
      streamResetFlags[i] = false;
      streamOwners[i] = -1;
    }
  }
  return result;
}
void MergingMediaPeriod::discardBuffer(int64_t positionUs, bool toKeyframe) {
  for (const auto& period: periods) period->discardBuffer(positionUs, toKeyframe);
}

int64_t MergingMediaPeriod::readDiscontinuity() {
  for (const auto& period: periods) {
    const int64_t result = period->readDiscontinuity();
    if (result != Definitions::kTimeUnset) return result;
  }
  return Definitions::kTimeUnset;
}

int64_t MergingMediaPeriod::seekToUs(int64_t positionUs) {
  int64_t result = positionUs;
  for (size_t i = 0; i < periods.size(); ++i) {
    const int64_t childResult = periods[i]->seekToUs(positionUs);
    if (i == 0) result = childResult;
  }
  return result;
}

int64_t MergingMediaPeriod::getAdjustedSeekPositionUs(int64_t positionUs,
    const SeekParameters& seekParameters) {
  if (periods.empty()) return positionUs;
  return periods.front()->getAdjustedSeekPositionUs(positionUs, seekParameters);
}

int64_t MergingMediaPeriod::getBufferedPositionUs() {
  int64_t result = Definitions::kTimeEndOfSource;
  for (const auto& period: periods) {
    result = std::min(result, period->getBufferedPositionUs());
  }
  return result;
}

int64_t MergingMediaPeriod::getNextLoadPositionUs() {
  int64_t result = Definitions::kTimeEndOfSource;
  for (const auto& period: periods) {
    result = std::min(result, period->getNextLoadPositionUs());
  }
  return result;
}

bool MergingMediaPeriod::continueLoading(int64_t positionUs) {
  bool progressed = false;
  for (const auto& period: periods) progressed |= period->continueLoading(positionUs);
  return progressed;
}

bool MergingMediaPeriod::isLoading() {
  for (const auto& period: periods) {
    if (period->isLoading()) return true;
  }
  return false;
}

void MergingMediaPeriod::reevaluateBuffer(int64_t positionUs) {
  for (const auto& period: periods) period->reevaluateBuffer(positionUs);
}

} // namespace coral