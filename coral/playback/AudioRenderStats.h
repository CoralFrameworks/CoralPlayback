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

#ifndef CORAL_PLAYBACK_AUDIO_RENDER_STATS_H
#define CORAL_PLAYBACK_AUDIO_RENDER_STATS_H

#include <cstdint>

namespace coral {

/** Diagnostics for one audio renderer. */
struct AudioRendererStats {
  /** The renderer index in the player's renderer list. */
  int rendererIndex = 0;

  /** Whether the renderer is currently enabled. */
  bool enabled = false;

  int decoderInitCount = 0;
  int decoderReleaseCount = 0;
  int inputBufferCount = 0;
  int renderedOutputBufferCount = 0;
  int skippedOutputBufferCount = 0;
  int droppedBufferCount = 0;

  /**
   * The number of times the audio sink had no room for a chunk. A high value together with a
   * non-advancing position is the first sign of playback starvation; it is a backpressure count,
   * not a platform XRun count.
   */
  uint64_t sinkBackpressureCount = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_AUDIO_RENDER_STATS_H
