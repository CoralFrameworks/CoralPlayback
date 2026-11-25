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

#ifndef CORAL_PLAYBACK_SOURCE_EMPTY_SAMPLE_STREAM_H
#define CORAL_PLAYBACK_SOURCE_EMPTY_SAMPLE_STREAM_H

#include <coral/source/SampleStream.h>

namespace coral {

/**
 * An empty {@link SampleStream}.
 */
class EmptySampleStream final : public SampleStream {
public:
  explicit EmptySampleStream(MediaType mediaType) noexcept
      : mediaType(mediaType) {}

  MediaType getMediaType() const override;

  bool isReady() const override;

  ReadDataResult
  readData(FormatHolder& formatHolder, AVPacketBuffer* packetBuffer, bool formatRequired) override;

  int skipData(int64_t positionUs) override;

private:
  MediaType mediaType;
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_EMPTY_SAMPLE_STREAM_H
