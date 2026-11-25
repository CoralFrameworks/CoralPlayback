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
#include <coral/source/SilenceSampleStream.h>

#include <media/Utilities.h>
#include <media/Types.h>

namespace coral {

SilenceSampleStream::SilenceSampleStream(int64_t durationUs) noexcept
  : sentFormat(false),
    positionBytes(0) {
  silenceSample.resize(MediaUtils::getBytesPerFrame(PCM_ENCODING, CHANNEL_COUNT) * FRAME_SIZE);
  silenceFormat = MediaFormat::CreateAudioFormat(MimeTypes::AUDIO_RAW, SAMPLE_RATE_HZ, CHANNEL_COUNT);
  durationBytes = getAudioByteCount(durationUs);
  seekTo(0);
}

MediaType SilenceSampleStream::getMediaType() const {
  return MediaType::kMediaTypeAudio;
}

void SilenceSampleStream::seekTo(long positionUs) {
  positionBytes = std::max(int64_t(0), std::min(getAudioByteCount(positionUs), durationBytes));
}

bool SilenceSampleStream::isReady() const {
  return true;
}

SampleStream::ReadDataResult
SilenceSampleStream::readData(FormatHolder& formatHolder,
  AVPacketBuffer* packetBuffer,
  const bool formatRequired) {
  if (!sentFormat || formatRequired) {
    formatHolder.format = silenceFormat;
    sentFormat = true;
    return ReadDataResult::kResultFormatRead;
  }
  auto bytesRemaining = durationBytes - positionBytes;
  if (bytesRemaining == 0) {
    packetBuffer->addFlag(AVBufferFlags::kEndOfStream);
    return ReadDataResult::kResultBufferRead;
  }

  auto bytesToWrite = (int) std::min(int64_t(silenceSample.size()), bytesRemaining);
  packetBuffer->ensureSpaceForWrite(bytesToWrite);
  packetBuffer->timeUs(getAudioPositionUs(positionBytes));
  packetBuffer->addFlag(AVBufferFlags::kKeyFrame);
  auto outputData = packetBuffer->data();
  outputData->position(0);
  outputData->put(silenceSample.data(), bytesToWrite);
  outputData->flip();
  positionBytes += bytesToWrite;
  return ReadDataResult::kResultBufferRead;
}

int SilenceSampleStream::skipData(int64_t positionUs) {
  auto oldPositionBytes = positionBytes;
  seekTo(positionUs);
  return (int) ((positionBytes - oldPositionBytes) / silenceSample.size());
}

int64_t SilenceSampleStream::getAudioByteCount(int64_t durationUs) {
  auto audioSampleCount =
    durationUs * SAMPLE_RATE_HZ / Definitions::kMicrosPerSecond;
  return MediaUtils::getBytesPerFrame(PCM_ENCODING, CHANNEL_COUNT) *
         audioSampleCount;
}

int64_t SilenceSampleStream::getAudioPositionUs(int64_t bytes) {
  auto audioSampleCount = bytes / MediaUtils::getBytesPerFrame(PCM_ENCODING, CHANNEL_COUNT);
  return audioSampleCount * Definitions::kMicrosPerSecond / SAMPLE_RATE_HZ;
}

} // namespace coral
