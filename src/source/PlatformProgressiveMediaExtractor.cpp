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

#include <coral/source/PlatformProgressiveMediaExtractor.h>

#include <codec/MediaExtractor.h>

#include <minilog/log.h>

#include <utility>

namespace coral {

PlatformProgressiveMediaExtractor::PlatformProgressiveMediaExtractor(std::string url) noexcept
  : url_(std::move(url)) {
}

PlatformProgressiveMediaExtractor::~PlatformProgressiveMediaExtractor() { release(); }

void PlatformProgressiveMediaExtractor::init(AVExtractorOutput& output) {
  release();
  output_ = &output;
}

int PlatformProgressiveMediaExtractor::sniff(AVExtractorInput& input) {
  if (output_ == nullptr) {
    return AVExtractorsFactory::kSniffResultNone;
  }
  if (extractor_ == nullptr) {
    // The extractor that reads the input of the reader is preferred: the bytes it consumes are then the
    // bytes the reader measures, so the period sees the progress of the extractor of the platform, and
    // the loading is bounded by the load control exactly as it is for a built-in extractor.
    extractor_ = CreateBuiltinExtractorFactory()->createInputExtractor(url_);
    if (extractor_ != nullptr) {
      // The media is opened from the input by this sniff, and the tracks the media holds are declared by
      // the initialization that follows: a container the platform cannot read is reported as not
      // recognized rather than as a container that was read.
      const int confidence = extractor_->sniff(input);
      if (confidence <= AVExtractorsFactory::kSniffResultNone) {
        extractor_.reset();
        return AVExtractorsFactory::kSniffResultNone;
      }
      extractor_->init(*this);
      return confidence;
    }
    // The platform has no extractor that reads the input of the reader, so the media is opened by its url
    // and the extractor reads it itself. It is created here rather than in `init`, because the fallback
    // of a period is initialized even when a built-in extractor reads the media.
    extractor_ = CreateBuiltinExtractorFactory()->createExtractor(url_, /* platform= */ true);
    if (extractor_ == nullptr) {
      return AVExtractorsFactory::kSniffResultNone;
    }
    // The extractor of the platform declares its tracks, and its seek map, while it is initialized.
    extractor_->init(*this);
  }
  // The media of the platform is already open, so the confidence is the one of the extractor itself.
  return extractor_->sniff(input);
}

void PlatformProgressiveMediaExtractor::release() {
  extractor_.reset();
  output_ = nullptr;
  trackCount_ = 0;
}

std::shared_ptr<AVSeekMap> PlatformProgressiveMediaExtractor::getSeekMap() {
  // The extractor of the platform is the seek map of the media it reads.
  return extractor_;
}

AVExtractor* PlatformProgressiveMediaExtractor::getChosenExtractor() const { return extractor_.get(); }

int PlatformProgressiveMediaExtractor::getTrackCount() { return trackCount_; }

bool PlatformProgressiveMediaExtractor::seek(const long position, const int64_t timeUs) {
  // The extractor of the platform reads the media itself, so the position of the input of the reader is
  // not part of the media it demuxes: it is seeked by time.
  return extractor_ != nullptr && extractor_->seek(position, timeUs);
}

AVExtractor::Result PlatformProgressiveMediaExtractor::read(AVExtractorInput& input) {
  if (extractor_ == nullptr) {
    return AVExtractor::kResultEndOfInput;
  }
  return extractor_->read(input);
}

// ==================================================================================================
// AVExtractorOutput
// ==================================================================================================

std::shared_ptr<AVTrackOutput> PlatformProgressiveMediaExtractor::track(
    const int id, const MediaType type, AVTrackOutput::SampleConsumer consumer) {
  if (output_ == nullptr) {
    return nullptr;
  }
  auto trackOutput = output_->track(id, type, std::move(consumer));
  if (trackOutput != nullptr) {
    ++trackCount_;
  }
  return trackOutput;
}

std::shared_ptr<AVTrackOutput> PlatformProgressiveMediaExtractor::track(
    const int id, AVTrackOutput::SampleConsumer consumer) {
  if (output_ == nullptr) {
    return nullptr;
  }
  auto trackOutput = output_->track(id, std::move(consumer));
  if (trackOutput != nullptr) {
    ++trackCount_;
  }
  return trackOutput;
}

void PlatformProgressiveMediaExtractor::seekMap(std::shared_ptr<AVSeekMap> seekMap) {
  if (output_ != nullptr) {
    output_->seekMap(std::move(seekMap));
  }
}

void PlatformProgressiveMediaExtractor::endTracks() {
  if (output_ != nullptr) {
    output_->endTracks();
  }
}

} // namespace coral
