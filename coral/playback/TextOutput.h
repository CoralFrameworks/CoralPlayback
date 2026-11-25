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

#ifndef CORAL_PLAYBACK_TEXT_OUTPUT_H
#define CORAL_PLAYBACK_TEXT_OUTPUT_H

#include <extractor/text/CueGroup.h>

namespace coral {

/**
 * The receiver of the cues that the player renders.
 *
 * <p>This is the counterpart of ExoPlayer's {@code TextOutput}: the player calls {@link #onCues}
 * whenever the set of cues that is shown changes, including the empty group that tells the
 * application to clear the screen. The callback runs on the playback thread.
 */
class TextOutput {
public:
  virtual ~TextOutput() = default;

  /** Called when the cues that are shown change. */
  virtual void onCues(const AVCueGroup& cueGroup) = 0;
};

} // namespace coral

#endif //CORAL_PLAYBACK_TEXT_OUTPUT_H
