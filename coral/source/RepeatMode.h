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
#ifndef CORAL_PLAYBACK_SOURCE_REPEAT_MODE_H
#define CORAL_PLAYBACK_SOURCE_REPEAT_MODE_H

#include <cstdint>

namespace coral {

/**
 * Repeat modes for playback. One of {@link #REPEAT_MODE_OFF}, {@link #REPEAT_MODE_ONE} or {@link
 * #REPEAT_MODE_ALL}.
 */
enum class RepeatMode : uint8_t {
  /**
   * Normal playback without repetition. "Previous" and "Next" actions move to the previous and
   * next {@link MediaItem} respectively, and do nothing when there is no previous or next {@link
   * MediaItem} to move to.
   */
  REPEAT_MODE_OFF = 0,
  /**
   * Repeats the currently playing {@link MediaItem} infinitely during ongoing playback. "Previous"
   * and "Next" actions behave as they do in {@link #REPEAT_MODE_OFF}, moving to the previous and
   * next {@link MediaItem} respectively, and doing nothing when there is no previous or next
   * {@link MediaItem} to move to.
   */
  REPEAT_MODE_ONE = 1,
  /**
   * Repeats the entire timeline infinitely. "Previous" and "Next" actions behave as they do in
   * {@link #REPEAT_MODE_OFF}, but with looping at the ends so that "Previous" when playing the
   * first {@link MediaItem} will move to the last {@link MediaItem}, and "Next" when playing the
   * last {@link MediaItem} will move to the first {@link MediaItem}.
   */
  REPEAT_MODE_ALL = 2
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_REPEAT_MODE_H
