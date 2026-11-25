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

#include "BaseRenderer.h"

#include <cassert>

namespace coral {

BaseRenderer::BaseRenderer(const MediaType trackType) noexcept
    : mediaType(trackType),
      index(Definitions::kIndexUnset),
      status(Status::kStatusDisabled),
      streamOffsetUs(0),
      lastResetPositionUs(0),
      readingPositionUs(Definitions::kTimeEndOfSource),
      streamIsFinal(false) {}

void BaseRenderer::enable(
    const std::shared_ptr<Configuration>& newConfiguration,
    const std::shared_ptr<TrackSelection>& trackSelection,
    const std::shared_ptr<SampleStream>& newStream,
    int64_t positionUs,
    bool joining,
    bool mayRenderStartOfStream,
    int64_t startPositionUs,
    int64_t offsetUs) {
  assert(status == Status::kStatusDisabled);
  configuration = newConfiguration;
  status = Status::kStatusEnabled;
  lastResetPositionUs = positionUs;
  onEnabled(joining, mayRenderStartOfStream);
  replaceStream(getFormats(trackSelection), newStream, startPositionUs, offsetUs);
  onPositionReset(positionUs, joining);
}

void BaseRenderer::start() {
  if (status != Status::kStatusEnabled) {
    return;
  }
  status = Status::kStatusRendering;
  onStarted();
}

void BaseRenderer::replaceStream(const std::vector<std::shared_ptr<MediaFormat>>& formats,
    const std::shared_ptr<SampleStream>& newStream,
    const int64_t startPositionUs,
    const int64_t offsetUs) {
  assert(!streamIsFinal);
  stream = newStream;
  readingPositionUs = offsetUs;
  streamFormats = formats;
  streamOffsetUs = offsetUs;
  onStreamChanged(formats, startPositionUs, offsetUs);
}

void BaseRenderer::maybeThrowStreamError() {
  assert(stream != nullptr);
}

void BaseRenderer::resetPosition(const int64_t positionUs) {
  streamIsFinal = false;
  lastResetPositionUs = positionUs;
  readingPositionUs = positionUs;
  onPositionReset(positionUs, false);
}

void BaseRenderer::stop() {
  if (status == Status::kStatusRendering) {
    status = Status::kStatusEnabled;
    onStopped();
  }
}

void BaseRenderer::disable() {
  assert(status == Status::kStatusEnabled);
  formatHolder.clear();
  status = Status::kStatusDisabled;
  std::vector<std::shared_ptr<MediaFormat>> emptyFormats{};
  streamFormats.swap(emptyFormats);
  streamIsFinal = false;
  onDisabled();
  // The stream belongs to the media period of the renderer, and it holds a pointer back to it. A
  // disabled renderer must not keep it: the period can be released while the renderer stays alive,
  // and reading from a stream whose period is gone is a use after free.
  stream = nullptr;
}

void BaseRenderer::reset() {
  assert(status == Status::kStatusDisabled);
  formatHolder.clear();
  onReset();
  stream = nullptr;
}

std::shared_ptr<SelectionCapabilities> BaseRenderer::getCapabilities() {
  return shared_from_this();
}

FormatHolder& BaseRenderer::getFormatHolder() {
  formatHolder.clear();
  return formatHolder;
}

SampleStream::ReadDataResult
BaseRenderer::readSource(FormatHolder& newFormatHolder, AVPacketBuffer* buffer, bool formatRequired) {
  if (stream == nullptr) {
    return ReadDataResult::kResultNothingRead;
  }
  auto result = stream->readData(newFormatHolder, buffer, formatRequired);
  if (result == ReadDataResult::kResultBufferRead) {
    if (buffer->isEndOfStream()) {
      readingPositionUs = Definitions::kTimeEndOfSource;
      return streamIsFinal ? ReadDataResult::kResultBufferRead : ReadDataResult::kResultNothingRead;
    }
    if (streamOffsetUs != 0) {
      auto newTimeUs = buffer->timeUs() + streamOffsetUs;
      buffer->timeUs(newTimeUs);
      readingPositionUs = std::max(readingPositionUs, newTimeUs);
    } else {
      readingPositionUs = std::max(readingPositionUs, int64_t(0));
    }
  }
  return result;
}

int BaseRenderer::skipSource(int64_t positionUs) const {
  if (stream != nullptr) {
    return getStream()->skipData(positionUs - streamOffsetUs);
  }
  return 0;
}

bool BaseRenderer::isSourceReady() const {
  assert(getStream() != nullptr);
  return hasReadStreamToEnd() ? streamIsFinal : getStream()->isReady();
}

std::vector<std::shared_ptr<MediaFormat>>
BaseRenderer::getFormats(const std::shared_ptr<TrackSelection>& newSelection) {
  auto length = newSelection != nullptr ? newSelection->length() : 0;
  std::vector<std::shared_ptr<MediaFormat>> formats;
  if (length > 0) {
    formats.resize(length);
    for (int i = 0; i < length; ++i) {
      formats[i] = newSelection->getFormat(i);
    }
  }
  return formats;
}

} // namespace coral
