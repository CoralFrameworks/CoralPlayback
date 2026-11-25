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

#ifndef CORAL_PLAYBACK_SOURCE_SAMPLE_STREAM_H
#define CORAL_PLAYBACK_SOURCE_SAMPLE_STREAM_H

#include <coral/source/FormatHolder.h>

#include <media/PacketBuffer.h>
#include <media/Definitions.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * A stream of media samples (and associated format information).
 */
class SampleStream {
public:
  virtual ~SampleStream() = default;

  enum class ReadDataResult : int8_t {
    //!< A value for methods where nothing was read.
    kResultNothingRead = Definitions::kResultNothingRead,
    //!< A value for methods where a buffer was read.
    kResultBufferRead = Definitions::kResultBufferRead,
    //!< A value for methods where a format was read.
    kResultFormatRead = Definitions::kResultFormatRead,
  };

  /**
   * Returns the media type of the SampleStream
   */
  virtual MediaType getMediaType() const = 0;

  /**
   * Returns whether data is available to be read.
   *
   * @return Whether data is available to be read.
   */
  virtual bool isReady() const = 0;

  /**
   * Attempts to read from the stream.
   *
   * @return The status of read, one of {@link ReadDataResult}.
   */
  ReadDataResult readData(FormatHolder& formatHolder,
      const std::unique_ptr<AVPacketBuffer>& packetBuffer,
      bool formatRequired) {
    return readData(formatHolder, packetBuffer.get(), formatRequired);
  }

  ReadDataResult readData(FormatHolder& formatHolder,
      const std::shared_ptr<AVPacketBuffer>& packetBuffer,
      bool formatRequired) {
    return readData(formatHolder, packetBuffer.get(), formatRequired);
  }

  virtual ReadDataResult
  readData(FormatHolder& formatHolder, AVPacketBuffer* packetBuffer, bool formatRequired) = 0;

  /**
   * Attempts to skip to the keyframe before the specified position, or to the
   * end of the stream if {@code positionUs} is beyond it.
   *
   * @param positionUs The specified time.
   * @return The number of samples that were skipped.
   */
  virtual int skipData(int64_t positionUs) = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_SAMPLE_STREAM_H
