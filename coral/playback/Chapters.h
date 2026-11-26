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

#ifndef CORAL_PLAYBACK_CHAPTERS_H
#define CORAL_PLAYBACK_CHAPTERS_H

#include <cstdint>
#include <string>

namespace coral {

/** One chapter of the current media item. */
struct Chapter {
  /** The start of the chapter on the media timeline, in microseconds. */
  int64_t startUs = 0;

  /** The end of the chapter, or the media duration when it is the last one. */
  int64_t endUs = 0;

  /** The chapter title as the container declared it; may be empty. */
  std::string title;
};

} // namespace coral

#endif // CORAL_PLAYBACK_CHAPTERS_H
