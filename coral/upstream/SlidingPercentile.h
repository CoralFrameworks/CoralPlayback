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
#ifndef CORAL_PLAYBACK_UPSTREAM_SLIDING_PERCENTILE_H
#define CORAL_PLAYBACK_UPSTREAM_SLIDING_PERCENTILE_H

#include <vector>

namespace coral {

/**
 * Calculate any percentile over a sliding window of weighted values. A maximum weight is
 * configured. Once the total weight of the values reaches the maximum weight, the oldest value is
 * reduced in weight until it reaches zero and is removed. This maintains a constant total weight,
 * equal to the maximum allowed, at the steady state.
 *
 * <p>This class can be used for bandwidth estimation based on a sliding window of past transfer
 * rate observations. This is an alternative to sliding mean and exponential averaging which suffer
 * from susceptibility to outliers and slow adaptation to step functions.
 *
 * <p>See the following Wikipedia articles:
 *
 * <ul>
 *   <li><a href="http://en.wikipedia.org/wiki/Moving_average">Moving average</a>
 *   <li><a href="http://en.wikipedia.org/wiki/Selection_algorithm">Selection algorithm</a>
 * </ul>
 */
class SlidingPercentile final {
private:
  struct Sample {
    int index;
    int weight;
    float value;

    Sample() : index(0), weight(0), value(0) {}
  };

  static constexpr int SORT_ORDER_NONE = -1;
  static constexpr int SORT_ORDER_BY_VALUE = 0;
  static constexpr int SORT_ORDER_BY_INDEX = 1;

  static constexpr int MAX_RECYCLED_SAMPLES = 5;

  int maxWeight;

  std::vector<Sample> samples;

  Sample recycledSamples[MAX_RECYCLED_SAMPLES];

  /**
   * The sort order of {@link #samples}, one of the {@code SORT_ORDER_*} constants.
   *
   * <p>All the state below is initialised here rather than in the constructor so that no code path
   * can read an indeterminate value: an uninitialised {@link #recycledSampleCount} alone would make
   * {@link #addSample} read outside {@link #recycledSamples}.
   */
  int currentSortOrder = SORT_ORDER_NONE;
  int nextSampleIndex = 0;
  int totalWeight = 0;
  int recycledSampleCount = 0;

public:
  explicit SlidingPercentile(int maxWeight) noexcept;

  /** Resets the sliding percentile. */
  void reset();

  /**
   * Adds a new weighted value.
   *
   * @param weight The weight of the new observation.
   * @param value The value of the new observation.
   */
  void addSample(int weight, float value);

  /**
   * Computes a percentile by integration.
   *
   * @param percentile The desired percentile, expressed as a fraction in the range (0,1].
   * @return The requested percentile value or {@link Float#NaN} if no samples have been added.
   */
  float getPercentile(float percentile);

private:
  /** Sorts the samples by index. */
  void ensureSortedByIndex();

  /** Sorts the samples by value. */
  void ensureSortedByValue();
};

} // namespace coral

#endif //CORAL_PLAYBACK_UPSTREAM_SLIDING_PERCENTILE_H
