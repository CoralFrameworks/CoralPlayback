/*
 * Copyright (C) 2022 The Coral Project
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
#ifndef CORAL_PLAYBACK_PARAMETERS_H
#define CORAL_PLAYBACK_PARAMETERS_H

#include <cstdint>

namespace coral {

/**
 * Parameters that apply to playback, including speed setting.
 */
class Parameters final {
private:
  int scaledUsPerMs;

public:
  /**
   * The factor by which playback will be sped up.
   */
  float speed;

  /**
   * The factor by which pitch will be shifted.
   */
  float pitch;

  Parameters() : Parameters(1.f) {}

  explicit Parameters(float speed) noexcept
      : Parameters(speed, 1.f) {}

  explicit Parameters(float speed, float pitch) noexcept
      : scaledUsPerMs((int)(speed * 1000)),
        speed(speed),
        pitch(pitch) {}

  Parameters(const Parameters& other) = default;
  Parameters& operator=(const Parameters& other) = default;

  friend bool operator==(const Parameters& lhs, const Parameters& rhs) {
    return lhs.speed == rhs.speed && lhs.pitch == rhs.pitch;
  }

  friend bool operator!=(const Parameters& lhs, const Parameters& rhs) {
    return !(lhs == rhs);
  }

  /**
   * Returns the media time in microseconds that will elapse in {@code timeMs}
   * milliseconds of wallclock time.
   *
   * @param timeMs The time to scale, in milliseconds.
   * @return The scaled time, in microseconds.
   */
  [[nodiscard]] int64_t getMediaTimeUsForPlayoutTimeMs(int64_t timeMs) const {
    return timeMs * scaledUsPerMs;
  }
};

} // namespace coral

#endif //CORAL_PLAYBACK_PARAMETERS_H
