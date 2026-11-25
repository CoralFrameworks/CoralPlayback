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

#ifndef CORAL_PLAYBACK_STANDALONE_CLOCK_H
#define CORAL_PLAYBACK_STANDALONE_CLOCK_H

#include <core/time/SystemClock.h>
#include <coral/playback/MediaClock.h>

#include <cmath>

namespace coral {

/**
 * A standalone clock whose position advances with real time based on playback
 * rate when started.
 */
class StandaloneClock final : public MediaClock {
private:
  int64_t mBaseTimeUs;
  int64_t baseElapsedMs;
  Parameters parameters;
  bool started;

public:
  StandaloneClock()
      : mBaseTimeUs(0), baseElapsedMs(0), parameters(1.0f),
        started(false) {}

  ~StandaloneClock() override = default;

  /**
   * Starts the clock. Does nothing if the clock is already started.
   */
  inline void start() {
    if (!started) {
      baseElapsedMs = SystemClock::CurrentTimeMillis();
      started = true;
    }
  }

  /**
   * Stops the clock. Does nothing if the clock is already stopped.
   */
  inline void stop() {
    if (started) {
      resetPosition(getPositionUs());
      started = false;
    }
  }

  /**
   * Resets the clock's position.
   */
  inline void resetPosition(int64_t positionUs) {
    mBaseTimeUs = positionUs;
    if (started) {
      baseElapsedMs = SystemClock::CurrentTimeMillis();
    }
  }

  /**
   * Returns the positionUs
   */
  inline int64_t getPositionUs() override {
    auto positionUs = mBaseTimeUs;
    if (started) {
      auto elapsedSinceBaseMs = SystemClock::CurrentTimeMillis() - baseElapsedMs;
      if (parameters.speed == 1.f) {
        positionUs += elapsedSinceBaseMs * Definitions::kMicrosPerMillisecond;
      } else {
        // Add the media time in microseconds that will elapse in
        // elapsedSinceBaseMs milliseconds of wallclock time
        positionUs += parameters.getMediaTimeUsForPlayoutTimeMs(
            elapsedSinceBaseMs);
      }
    }
    return positionUs;
  }

  void setPlaybackParameters(const Parameters& newParameters) override {
    // Store the current position as the new base, in case the playback speed
    // has changed.
    if (started) {
      resetPosition(getPositionUs());
    }
    parameters = newParameters;
  }

  [[nodiscard]] const Parameters& getPlaybackParameters() const override {
    return parameters;
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_STANDALONE_CLOCK_H
