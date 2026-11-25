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

#ifndef CORAL_PLAYBACK_VIDEO_FRAME_OUTPUT_H
#define CORAL_PLAYBACK_VIDEO_FRAME_OUTPUT_H

#include <codec/MediaCodec.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * The latest picture the player wrote into its output surface.
 *
 * <p>A UI bridge polls this value on its render tick and imports {@link surface} into its own GPU
 * device when {@link sequence} changes. It is the hand-off between the player's output node and the
 * UI's content host; the player does not depend on the UI toolkit.
 *
 * <p>The surface belongs to the application and stays valid until the application replaces or
 * releases it. The sequence increases once for each frame the effect graph renders, including
 * repeated frames, so a bridge that only wants new decoded pictures should also compare
 * {@link positionUs}. A future revision will carry a frame fence; until then the bridge should use
 * its own in-flight slots or wait for the engine before sampling.
 */
struct VideoFrameOutput {
  /** The application-provided surface the effect graph wrote, or null before the first frame. */
  std::shared_ptr<VideoSurface> surface;

  /** The media position of the frame that was rendered, in microseconds. */
  int64_t positionUs = 0;

  /** Monotonically increasing render counter; zero means no frame has been rendered yet. */
  uint64_t sequence = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_VIDEO_FRAME_OUTPUT_H
