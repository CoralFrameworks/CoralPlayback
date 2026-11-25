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

#ifndef CORAL_PLAYBACK_VOLUME_H
#define CORAL_PLAYBACK_VOLUME_H

namespace coral {

/**
 * The volume structure
 */
struct Volume {
  //!< the left channel volume
  float leftVolume;
  //!< the right channel volume
  float rightVolume;

  Volume() : Volume(1.0) {
  }

  explicit Volume(const float volume) noexcept
    : Volume(volume, volume) {
  }

  constexpr Volume(const float leftVolume, const float rightVolume) noexcept
    : leftVolume(leftVolume),
      rightVolume(rightVolume) {
  }

  /**
   * Returns the average volume
   */
  [[nodiscard]] float value() const {
    return (leftVolume + rightVolume) / 2.0f;
  }

  bool operator==(const Volume& rhs) const {
    return leftVolume == rhs.leftVolume && rightVolume == rhs.rightVolume;
  }

  bool operator!=(const Volume& rhs) const { return !(*this == rhs); }
};

} // namespace coral

#endif //CORAL_PLAYBACK_VOLUME_H
