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

#include <coral/upstream/SlidingPercentile.h>

#include <algorithm>
#include <limits>

namespace coral {

SlidingPercentile::SlidingPercentile(int maxWeight) noexcept
    : maxWeight(maxWeight) {
}

void SlidingPercentile::reset() {
  samples.clear();
  currentSortOrder = SORT_ORDER_NONE;
  nextSampleIndex = 0;
  totalWeight = 0;
  // The recycled samples are scratch space, but the count has to go back to zero so that the next
  // `addSample` does not reuse an entry of a window that was cleared.
  recycledSampleCount = 0;
}

void SlidingPercentile::addSample(int weight, float value) {
  ensureSortedByIndex();

  auto newSample =
      recycledSampleCount > 0 ? recycledSamples[--recycledSampleCount] : Sample();
  newSample.index = nextSampleIndex++;
  newSample.weight = weight;
  newSample.value = value;
  samples.push_back(newSample);
  totalWeight += weight;

  while (totalWeight > maxWeight) {
    int excessWeight = totalWeight - maxWeight;
    // A reference, not a copy: reducing the weight of the oldest sample is what keeps the sum of the
    // weights of the window equal to the total weight ExoPlayer tracks.
    auto& oldestSample = samples[0];
    if (oldestSample.weight <= excessWeight) {
      totalWeight -= oldestSample.weight;
      samples.erase(samples.begin());
      if (recycledSampleCount < MAX_RECYCLED_SAMPLES) {
        recycledSamples[recycledSampleCount++] = oldestSample;
      }
    } else {
      oldestSample.weight -= excessWeight;
      totalWeight -= excessWeight;
    }
  }
}

float SlidingPercentile::getPercentile(float percentile) {
  ensureSortedByValue();
  float desiredWeight = percentile * totalWeight;
  int accumulatedWeight = 0;
  for (int i = 0; i < samples.size(); i++) {
    auto const& currentSample = samples[i];
    accumulatedWeight += currentSample.weight;
    if (accumulatedWeight >= desiredWeight) {
      return currentSample.value;
    }
  }
  // Clamp to maximum value or NaN if no values.
  return samples.empty() ? std::numeric_limits<float>::quiet_NaN()
                         : samples[samples.size() - 1].value;
}

void SlidingPercentile::ensureSortedByIndex() {
  if (currentSortOrder != SORT_ORDER_BY_INDEX) {
    // Ascending, like ExoPlayer's SAMPLE_INDEX_COMPARATOR: the sample that is evicted when the
    // window is full is the *first* one, i.e. the oldest.
    std::sort(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) {
      return a.index < b.index;
    });
    currentSortOrder = SORT_ORDER_BY_INDEX;
  }
}

void SlidingPercentile::ensureSortedByValue() {
  if (currentSortOrder != SORT_ORDER_BY_VALUE) {
    // Ascending, like ExoPlayer's SAMPLE_VALUE_COMPARATOR: the percentile is accumulated from the
    // smallest value upwards, so that e.g. the 10th percentile is a small value and not a large one.
    std::sort(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) {
      return a.value < b.value;
    });
    currentSortOrder = SORT_ORDER_BY_VALUE;
  }
}

} // namespace coral
