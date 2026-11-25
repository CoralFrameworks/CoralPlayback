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

#ifndef CORAL_PLAYBACK_UID_GENERATOR_H
#define CORAL_PLAYBACK_UID_GENERATOR_H

#include <random>
#include <sstream>

namespace coral {

inline int getRandomUid() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 255);
  return dis(gen);
}

inline std::string newUidString() {
  std::stringstream ss;
  for (auto i = 0; i < 16; ++i) {
    const auto rc = getRandomUid();
    std::stringstream hexStream;
    hexStream << std::hex << rc;
    auto hex = hexStream.str();
    ss << (hex.length() < 2 ? '0' + hex : hex);
  }
  return ss.str();
}

} // namespace coral

#endif //CORAL_PLAYBACK_UID_GENERATOR_H
