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

#include <coral/source/DefaultProgressiveMediaExtractor.h>

#include <algorithm>
#include <utility>

namespace coral {

DefaultProgressiveMediaExtractor::DefaultProgressiveMediaExtractor(
    std::unique_ptr<AVExtractorsFactory> extractorsFactory) noexcept
    : extractorsFactory(std::move(extractorsFactory)) {
}

DefaultProgressiveMediaExtractor::~DefaultProgressiveMediaExtractor() {
  release();
}

void DefaultProgressiveMediaExtractor::setFallback(
    std::shared_ptr<ProgressiveMediaExtractor> fallback) noexcept {
  releaseFallback();
  this->fallback = std::move(fallback);
  if (this->fallback != nullptr && output != nullptr) {
    // The period is already initialized, so the fallback that replaces the released one is bound to
    // the output right away: a fallback that is set later than `init` is not left unable to read.
    this->fallback->init(*output);
    fallbackInitialized = true;
  }
}

void DefaultProgressiveMediaExtractor::init(AVExtractorOutput& output) {
  this->output = &output;
  mSeekMap.reset();
  trackIds.clear();
  chosenExtractor.reset();
  fallbackSelected = false;
  // The extractors are created per period, because the factory is free to return extractors that
  // keep state, and because a period is demuxed independently of the other periods.
  extractors = extractorsFactory != nullptr ? extractorsFactory->createExtractors()
                                            : std::vector<std::unique_ptr<AVExtractor>>();
  // The fallback is part of the period for its whole life, so it is initialized with the other
  // extractors rather than when it is selected. It is not asked to recognize the container until
  // every extractor above rejected it, so a fallback that reads the media itself, e.g. the platform
  // extractor, does not start reading while a container of the module is played.
  if (fallback != nullptr) {
    fallback->init(output);
    fallbackInitialized = true;
  }
}

int DefaultProgressiveMediaExtractor::sniff(AVExtractorInput& input) {
  if (output == nullptr) {
    return AVExtractorsFactory::kSniffResultNone;
  }

  int bestConfidence = AVExtractorsFactory::kSniffResultNone;
  for (auto& extractor : extractors) {
    if (extractor == nullptr) {
      continue;
    }
    // A rejected extractor may have moved the input, so every candidate starts from the beginning.
    input.resetPosition(0, true);
    const int confidence = extractor->sniff(input);
    if (confidence > bestConfidence) {
      bestConfidence = confidence;
      chosenExtractor = std::move(extractor);
    }
  }
  input.resetPosition(0, true);

  if (chosenExtractor == nullptr) {
    // The container is not one of the module: the fallback is the last chance to read it, and a
    // container that it does not recognize either is left to the caller to report.
    return sniffFallback(input);
  }

  // The chosen extractor declares its tracks, and possibly its seek map, while it is initialized.
  chosenExtractor->init(*this);
  return bestConfidence;
}

int DefaultProgressiveMediaExtractor::sniffFallback(AVExtractorInput& input) {
  if (fallback == nullptr || !fallbackInitialized) {
    return AVExtractorsFactory::kSniffResultNone;
  }
  const int confidence = fallback->sniff(input);
  if (confidence <= AVExtractorsFactory::kSniffResultNone || !fallback->isContainerRecognized()) {
    return AVExtractorsFactory::kSniffResultNone;
  }
  fallbackSelected = true;
  return confidence;
}

void DefaultProgressiveMediaExtractor::releaseFallback() {
  if (fallback != nullptr && fallbackInitialized) {
    fallback->release();
  }
  fallbackInitialized = false;
  fallbackSelected = false;
}

void DefaultProgressiveMediaExtractor::release() {
  releaseFallback();
  fallback.reset();
  chosenExtractor.reset();
  extractors.clear();
  mSeekMap.reset();
  trackIds.clear();
  output = nullptr;
}

std::shared_ptr<AVSeekMap> DefaultProgressiveMediaExtractor::getSeekMap() {
  if (fallbackSelected) {
    return fallback->getSeekMap();
  }
  return mSeekMap;
}

AVExtractor* DefaultProgressiveMediaExtractor::getChosenExtractor() const {
  if (fallbackSelected) {
    // The fallback demuxes the media with its own infrastructure, so it may have no extractor of the
    // module to report: see `isContainerRecognized`.
    return fallback->getChosenExtractor();
  }
  return chosenExtractor.get();
}

bool DefaultProgressiveMediaExtractor::isContainerRecognized() const {
  return fallbackSelected || chosenExtractor != nullptr;
}

int DefaultProgressiveMediaExtractor::getTrackCount() {
  if (fallbackSelected) {
    return fallback->getTrackCount();
  }
  return static_cast<int>(trackIds.size());
}

bool DefaultProgressiveMediaExtractor::seek(const long position, const int64_t timeUs) {
  if (fallbackSelected) {
    return fallback->seek(position, timeUs);
  }
  if (chosenExtractor == nullptr) {
    return false;
  }
  return chosenExtractor->seek(position, timeUs);
}

AVExtractor::Result DefaultProgressiveMediaExtractor::read(AVExtractorInput& input) {
  if (fallbackSelected) {
    return fallback->read(input);
  }
  if (chosenExtractor == nullptr) {
    return AVExtractor::kResultEndOfInput;
  }
  return chosenExtractor->read(input);
}

// ==================================================================================================
// AVExtractorOutput
// ==================================================================================================

std::shared_ptr<AVTrackOutput> DefaultProgressiveMediaExtractor::track(
    const int id, const MediaType type, AVTrackOutput::SampleConsumer consumer) {
  auto trackOutput = output->track(id, type, std::move(consumer));
  if (std::find(trackIds.begin(), trackIds.end(), id) == trackIds.end()) {
    trackIds.push_back(id);
  }
  return trackOutput;
}

std::shared_ptr<AVTrackOutput> DefaultProgressiveMediaExtractor::track(
    const int id, AVTrackOutput::SampleConsumer consumer) {
  auto trackOutput = output->track(id, std::move(consumer));
  if (std::ranges::find(trackIds, id) == trackIds.end()) {
    trackIds.push_back(id);
  }
  return trackOutput;
}

void DefaultProgressiveMediaExtractor::seekMap(std::shared_ptr<AVSeekMap> seekMap) {
  mSeekMap = std::move(seekMap);
  output->seekMap(mSeekMap);
}

void DefaultProgressiveMediaExtractor::endTracks() {
  output->endTracks();
}

} // namespace coral
