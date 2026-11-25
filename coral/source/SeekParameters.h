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

#ifndef CORAL_PLAYBACK_SOURCE_SEEK_PARAMETERS_H
#define CORAL_PLAYBACK_SOURCE_SEEK_PARAMETERS_H

#include <cmath>
#include <cstdint>

namespace coral {

struct SeekParameters {
  /**
   * The maximum time that the actual position seeked to may precede the
   * requested seek position, in microseconds.
   */
  int64_t toleranceBeforeUs;

  /**
   * The maximum time that the actual position seeked to may exceed the
   * requested seek position, in microseconds.
   */
  int64_t toleranceAfterUs;

  explicit SeekParameters(int64_t toleranceBeforeUs,
      int64_t toleranceAfterUs) noexcept
      : toleranceBeforeUs(toleranceBeforeUs),
        toleranceAfterUs(toleranceAfterUs) {}

  SeekParameters(const SeekParameters& other)
      : toleranceBeforeUs(other.toleranceBeforeUs),
        toleranceAfterUs(other.toleranceAfterUs) {}

  SeekParameters& operator=(const SeekParameters& other) {
    this->toleranceBeforeUs = other.toleranceBeforeUs;
    this->toleranceAfterUs = other.toleranceAfterUs;
    return *this;
  }

  friend bool operator==(const SeekParameters& lhs, const SeekParameters& rhs) {
    return lhs.toleranceBeforeUs == rhs.toleranceBeforeUs &&
           lhs.toleranceAfterUs == rhs.toleranceAfterUs;
  }

  friend bool operator!=(const SeekParameters& lhs, const SeekParameters& rhs) {
    return !(lhs == rhs);
  }

  /**
   * Resolves a seek based on the parameters, given the requested seek position
   * and two candidate sync points.
   *
   * @param positionUs The requested seek position, in microseocnds.
   * @param firstSyncUs The first candidate seek point, in micrseconds.
   * @param secondSyncUs The second candidate seek point, in microseconds. May
   * equal {@code firstSyncUs} if there's only one candidate.
   * @return The resolved seek position, in microseconds.
   */
  int64_t resolveSeekPositionUs(int64_t positionUs, int64_t firstSyncUs,
      int64_t secondSyncUs) const {
    if (toleranceBeforeUs == 0 && toleranceAfterUs == 0) {
      return positionUs;
    }
    auto minPositionUs =
        subtractWithOverflowDefault(positionUs, toleranceBeforeUs, INT64_MIN);
    auto maxPositionUs =
        addWithOverflowDefault(positionUs, toleranceAfterUs, INT64_MAX);
    auto firstSyncPositionValid =
        minPositionUs <= firstSyncUs && firstSyncUs <= maxPositionUs;
    auto secondSyncPositionValid =
        minPositionUs <= secondSyncUs && secondSyncUs <= maxPositionUs;
    if (firstSyncPositionValid && secondSyncPositionValid) {
      if (std::abs(firstSyncUs - positionUs) <=
          std::abs(secondSyncUs - positionUs)) {
        return firstSyncUs;
      } else {
        return secondSyncUs;
      }
    } else if (firstSyncPositionValid) {
      return firstSyncUs;
    } else if (secondSyncPositionValid) {
      return secondSyncUs;
    } else {
      return minPositionUs;
    }
  }

private:
  static int64_t subtractWithOverflowDefault(int64_t x, int64_t y,
      int64_t overflowResult) {
    int64_t result = x - y;
    // See Hacker's Delight 2-13 (H. Warren Jr).
    if (((x ^ y) & (x ^ result)) < 0) {
      return overflowResult;
    }
    return result;
  }

  static int64_t addWithOverflowDefault(int64_t x, int64_t y,
      int64_t overflowResult) {
    int64_t result = x + y;
    // See Hacker's Delight 2-13 (H. Warren Jr).
    if (((x ^ result) & (y ^ result)) < 0) {
      return overflowResult;
    }
    return result;
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_SEEK_PARAMETERS_H
