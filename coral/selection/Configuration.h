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

#ifndef CORAL_PLAYBACK_SELECTION_CONFIGURATION_H
#define CORAL_PLAYBACK_SELECTION_CONFIGURATION_H

namespace coral {

/**
 * The configuration of a {@link TrackSelector}.
 */
class Configuration final {
public:
  /**
   * Represents an unset audio session identifier.
   */
  static constexpr int AUDIO_SESSION_ID_UNSET = 0;

  /**
   * The audio session id to use for tunneling, or {@link
   * #AUDIO_SESSION_ID_UNSET} if tunneling should not be enabled.
   */
  const int tunnelingAudioSessionId;

  Configuration() : Configuration(AUDIO_SESSION_ID_UNSET) {}

  ~Configuration() = default;

  explicit Configuration(const int tunnelingAudioSessionId) noexcept
      : tunnelingAudioSessionId(tunnelingAudioSessionId) {}

  Configuration(const Configuration& other) = delete;
  Configuration& operator=(const Configuration& other) = delete;
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_CONFIGURATION_H
