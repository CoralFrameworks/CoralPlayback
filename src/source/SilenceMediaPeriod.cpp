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

#include <coral/source/SilenceMediaPeriod.h>
#include <coral/source/SilenceSampleStream.h>

#include <media/Types.h>

#include <algorithm>
#include <vector>

namespace coral {

SilenceMediaPeriod::SilenceMediaPeriod(int64_t durationUs) noexcept
  : durationUs(durationUs) {
  // The period exposes the track its stream generates, so that a renderer can be given a stream and so
  // that the player has something to select. ExoPlayer's silence period exposes a track the same way.
  tracks = std::make_shared<TrackGroupArray>(std::vector<std::shared_ptr<TrackGroup>>{
    std::make_shared<TrackGroup>(MediaType::kMediaTypeAudio,
      MediaFormat::CreateAudioFormat(MimeTypes::AUDIO_RAW,
        SilenceSampleStream::SAMPLE_RATE_HZ,
        SilenceSampleStream::CHANNEL_COUNT))
  });
}

void SilenceMediaPeriod::release() {
}

void SilenceMediaPeriod::prepare(MediaPeriod::Callback* callback,
  int64_t positionUs) {
  if (callback) {
    callback->onPrepared(shared_from_this());
  }
}

const std::shared_ptr<TrackGroupArray>&
SilenceMediaPeriod::getTrackGroups() const {
  return tracks;
}

int64_t SilenceMediaPeriod::selectTracks(
  const std::vector<std::shared_ptr<TrackSelection>>& selections,
  const std::vector<bool>& mayRetainStreamFlags,
  std::vector<std::shared_ptr<SampleStream>>& streams,
  std::vector<bool>& streamResetFlags, int64_t positionUs) {
  positionUs = constrainSeekPosition(positionUs);
  for (size_t index = 0; index < selections.size(); ++index) {
    if (streams[index] != nullptr &&
        (selections[index] == nullptr || !mayRetainStreamFlags[index])) {
      auto iterator =
        std::find(sampleStreams.begin(), sampleStreams.end(), streams[index]);
      if (sampleStreams.end() != iterator) {
        sampleStreams.erase(iterator);
      }
      streams[index] = nullptr;
    }
    if (streams[index] == nullptr && selections[index] != nullptr) {
      auto stream = std::make_shared<SilenceSampleStream>(durationUs);
      stream->seekTo(positionUs);
      sampleStreams.push_back(stream);
      streams[index] = stream;
      streamResetFlags[index] = true;
    }
  }
  return positionUs;
}

int64_t SilenceMediaPeriod::getAdjustedSeekPositionUs(
  int64_t positionUs, const SeekParameters& seekParameters) {
  return constrainSeekPosition(positionUs);
}

int64_t SilenceMediaPeriod::getBufferedPositionUs() {
  return Definitions::kTimeEndOfSource;
}

int64_t SilenceMediaPeriod::getNextLoadPositionUs() {
  return Definitions::kTimeEndOfSource;
}

bool SilenceMediaPeriod::continueLoading(int64_t positionUs) { return false; }

bool SilenceMediaPeriod::isLoading() { return false; }

void SilenceMediaPeriod::discardBuffer(int64_t positionUs, bool toKeyframe) {
  // do nothing
}

int64_t SilenceMediaPeriod::readDiscontinuity() {
  return Definitions::kTimeUnset;
}

int64_t SilenceMediaPeriod::seekToUs(int64_t positionUs) {
  positionUs = constrainSeekPosition(positionUs);
  for (size_t i = 0; i < sampleStreams.size(); ++i) {
    auto const& stream =
      std::dynamic_pointer_cast<SilenceSampleStream>(sampleStreams[i]);
    if (stream != nullptr) {
      stream->seekTo(positionUs);
    }
  }
  return positionUs;
}

void SilenceMediaPeriod::reevaluateBuffer(int64_t positionUs) {
  // do nothing
}

int64_t SilenceMediaPeriod::constrainSeekPosition(int64_t positionUs) {
  return std::max(int64_t(0), std::min(positionUs, durationUs));
}

} // namespace coral
