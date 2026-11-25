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

#include <coral/source/EmptySampleStream.h>

namespace coral {

bool EmptySampleStream::isReady() const { return true; }

SampleStream::ReadDataResult
EmptySampleStream::readData(FormatHolder& formatHolder, AVPacketBuffer* buffer, bool formatRequired) {
  buffer->clear();
  buffer->setFlags(AVBufferFlags::kEndOfStream);
  return ReadDataResult::kResultBufferRead;
}

int EmptySampleStream::skipData(int64_t positionUs) { return 0; }

MediaType EmptySampleStream::getMediaType() const { return mediaType; }

} // namespace coral
