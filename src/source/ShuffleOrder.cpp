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

#include <coral/source/ShuffleOrder.h>

#include <algorithm>
#include <random>

namespace coral {

namespace internal {

/**
 * Returns a random number in [0, max), like ExoPlayer's `Random.nextInt(max)`.
 *
 * The interval is half open on purpose: the shuffle algorithms below read and write
 * `values[swapIndex]` with `swapIndex` taken from `randomNumber(i + 1)`, so an inclusive interval
 * would let them touch the element after the one they are shuffling, which writes out of bounds.
 */
static int randomNumber(int max) {
  if (max <= 0) {
    return 0;
  }
  static std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<> dis(0, max - 1);
  return dis(gen);
}

static std::vector<int> createShuffledList(int length) {
  if (length <= 0) {
    return {};
  }
  auto shuffled = std::vector<int>(length);
  for (auto i = 0; i < length; ++i) {
    auto swapIndex = randomNumber(i + 1);
    shuffled[i] = shuffled[swapIndex];
    shuffled[swapIndex] = i;
  }
  return shuffled;
}

} // namespace internal

DefaultShuffleOrder::DefaultShuffleOrder(int length) noexcept
    : shuffled(internal::createShuffledList(length)) {
  if (length > 0) {
    indexInShuffled.resize(length);
    for (auto i = 0; i < length; ++i) {
      indexInShuffled[shuffled[i]] = i;
    }
  }
}

DefaultShuffleOrder::DefaultShuffleOrder(const std::vector<int>& shuffledIndices) noexcept
    : shuffled(shuffledIndices) {
  indexInShuffled.resize(shuffledIndices.size());
  for (size_t i = 0; i < shuffled.size(); ++i) {
    indexInShuffled[shuffled[i]] = i;
  }
}

int DefaultShuffleOrder::getLength() const {
  return shuffled.size();
}

int DefaultShuffleOrder::getNextIndex(int index) {
  auto shuffledIndex = indexInShuffled[index];
  return (++shuffledIndex < (int)shuffled.size()) ? shuffled[shuffledIndex] : INDEX_UNSET;
}

int DefaultShuffleOrder::getPreviousIndex(int index) {
  auto shuffledIndex = indexInShuffled[index];
  return --shuffledIndex >= 0 ? shuffled[shuffledIndex] : INDEX_UNSET;
}

int DefaultShuffleOrder::getLastIndex() {
  return shuffled.size() > 0 ? shuffled[shuffled.size() - 1] : INDEX_UNSET;
}

int DefaultShuffleOrder::getFirstIndex() {
  return shuffled.size() > 0 ? shuffled[0] : INDEX_UNSET;
}

std::shared_ptr<ShuffleOrder>
DefaultShuffleOrder::cloneAndInsert(int insertionIndex, int insertionCount) {
  auto insertionPoints = std::vector<int>(insertionCount);
  auto insertionValues = std::vector<int>(insertionCount);
  for (int i = 0; i < insertionCount; i++) {
    insertionPoints[i] = internal::randomNumber(shuffled.size() + 1);
    auto swapIndex = internal::randomNumber(i + 1);
    insertionValues[i] = insertionValues[swapIndex];
    insertionValues[swapIndex] = i + insertionIndex;
  }
  std::sort(insertionPoints.begin(), insertionPoints.end());
  auto newShuffled = std::vector<int>(shuffled.size() + insertionCount);
  auto indexInOldShuffled = 0;
  auto indexInInsertionList = 0;
  for (size_t i = 0; i < shuffled.size() + insertionCount; ++i) {
    if (indexInInsertionList < insertionCount
        && indexInOldShuffled == insertionPoints[indexInInsertionList]) {
      newShuffled[i] = insertionValues[indexInInsertionList++];
    } else {
      if (shuffled.empty()) {
        newShuffled[i] = 0;
      } else {
        newShuffled[i] = shuffled[indexInOldShuffled++];
      }
      if (newShuffled[i] >= insertionIndex) {
        newShuffled[i] += insertionCount;
      }
    }
  }
  return std::make_shared<DefaultShuffleOrder>(newShuffled);
}

std::shared_ptr<ShuffleOrder>
DefaultShuffleOrder::cloneAndRemove(int indexFrom, int indexToExclusive) {
  auto numberOfElementsToRemove = indexToExclusive - indexFrom;
  auto newShuffled = std::vector<int>(shuffled.size() - numberOfElementsToRemove);
  auto foundElementsCount = 0;
  for (auto i = 0; i < shuffled.size(); ++i) {
    if (shuffled[i] >= indexFrom && shuffled[i] < indexToExclusive) {
      foundElementsCount++;
    } else {
      newShuffled[i - foundElementsCount] =
          shuffled[i] >= indexFrom ? shuffled[i] - numberOfElementsToRemove : shuffled[i];
    }
  }
  return std::make_shared<DefaultShuffleOrder>(newShuffled);
}

std::shared_ptr<ShuffleOrder> DefaultShuffleOrder::cloneAndClear() {
  return std::make_shared<DefaultShuffleOrder>(0);
}

//--------------------------------------------------------------------------------------------------

int UnShuffledShuffleOrder::getLength() const {
  return length;
}

int UnShuffledShuffleOrder::getNextIndex(int index) {
  return ++index < length ? index : INDEX_UNSET;
}

int UnShuffledShuffleOrder::getPreviousIndex(int index) {
  return --index >= 0 ? index : INDEX_UNSET;
}

int UnShuffledShuffleOrder::getLastIndex() {
  return length > 0 ? length - 1 : INDEX_UNSET;
}

int UnShuffledShuffleOrder::getFirstIndex() {
  return length > 0 ? 0 : INDEX_UNSET;
}

std::shared_ptr<ShuffleOrder>
UnShuffledShuffleOrder::cloneAndInsert(int insertionIndex, int insertionCount) {
  return std::make_shared<UnShuffledShuffleOrder>(length + insertionCount);
}

std::shared_ptr<ShuffleOrder>
UnShuffledShuffleOrder::cloneAndRemove(int indexFrom, int indexToExclusive) {
  return std::make_shared<UnShuffledShuffleOrder>(length - indexToExclusive + indexFrom);
}

std::shared_ptr<ShuffleOrder> UnShuffledShuffleOrder::cloneAndClear() {
  return std::make_shared<UnShuffledShuffleOrder>(0);
}

} // namespace coral
