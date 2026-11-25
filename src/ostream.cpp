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

#include <coral/playback/Player.h>
#include <media/Definitions.h>

using namespace coral;

template<>
const char* convertToText<Player::State>(Player::State state) {
  switch (state) {
    case Player::State::STATE_IDLE: {
      return "idle";
    }
    case Player::State::STATE_BUFFERING: {
      return "buffering";
    }
    case Player::State::STATE_READY: {
      return "ready";
    }
    case Player::State::STATE_ENDED: {
      return "ended";
    }
    default: {
      return &"unknown state: "[(int)state];
    }
  }
}
