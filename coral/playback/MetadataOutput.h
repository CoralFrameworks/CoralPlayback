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

#ifndef CORAL_PLAYBACK_METADATA_OUTPUT_H
#define CORAL_PLAYBACK_METADATA_OUTPUT_H

#include <media/Metadata.h>

namespace coral {

/**
 * The receiver of the timed metadata that the player renders.
 *
 * <p>This is the counterpart of ExoPlayer's {@code MetadataOutput}: every decoded metadata sample is
 * handed to {@link #onMetadata} when the playback position reaches its time. The callback runs on
 * the playback thread.
 */
class MetadataOutput {
public:
  virtual ~MetadataOutput() = default;

  /** Called with the metadata of one sample. */
  virtual void onMetadata(const Metadata& metadata) = 0;
};

} // namespace coral

#endif //CORAL_PLAYBACK_METADATA_OUTPUT_H
