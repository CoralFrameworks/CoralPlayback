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
#ifndef CORAL_PLAYBACK_SOURCE_SILENCE_SAMPLE_STREAM_H
#define CORAL_PLAYBACK_SOURCE_SILENCE_SAMPLE_STREAM_H

#include <coral/source/SampleStream.h>

#include <memory>
#include <vector>

namespace coral {

class SilenceSampleStream final : public SampleStream {
private:
  std::vector<uint8_t> silenceSample;
  std::shared_ptr<MediaFormat> silenceFormat;
  int64_t durationBytes;
  bool sentFormat;
  int64_t positionBytes;

public:
  /**
   * The channel count of the silence, which is also the one of the format of the track group that
   * {@link SilenceMediaPeriod} exposes.
   */
  static constexpr int CHANNEL_COUNT = 2;

  /** The sample rate of the silence, in hertz. */
  static constexpr int SAMPLE_RATE_HZ = 44100;

  /** The number of frames of every buffer of silence. */
  static constexpr int FRAME_SIZE = 1024;

  /**
   * The encoding of the samples of the silence.
   */
  static constexpr AudioSampleFormat PCM_ENCODING = AudioSampleFormat::kInt16;

  explicit SilenceSampleStream(int64_t durationUs) noexcept;

  [[nodiscard]] MediaType getMediaType() const override;

  void seekTo(long positionUs);

  [[nodiscard]] bool isReady() const override;

  ReadDataResult
  readData(FormatHolder& formatHolder, AVPacketBuffer* packetBuffer, bool formatRequired) override;

  int skipData(int64_t positionUs) override;

private:
  static int64_t getAudioByteCount(int64_t durationUs);
  static int64_t getAudioPositionUs(int64_t bytes);
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_SILENCE_SAMPLE_STREAM_H
