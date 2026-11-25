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

#ifndef CORAL_PLAYBACK_MEDIA_CLOCK_H
#define CORAL_PLAYBACK_MEDIA_CLOCK_H

#include <coral/playback/PlaybackInfo.h>

namespace coral {

/**
 * The sync clock of the Renderer
 */
class MediaClock {
public:
  virtual ~MediaClock() = default;

  /**
   * Returns the position of the clock
   */
  virtual int64_t getPositionUs() = 0;

  /**
   * Attempts to set the playback parameters. The media clock may override the
   * speed if changing the playback parameters is not supported.
   *
   * @param playbackParameters The playback parameters to attempt to set.
   */
  virtual void setPlaybackParameters(const Parameters& playbackParameters) = 0;

  /**
   * Returns the active playback parameters.
   */
  virtual const Parameters& getPlaybackParameters() const = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_MEDIA_CLOCK_H
