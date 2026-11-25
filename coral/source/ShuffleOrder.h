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
#ifndef CORAL_PLAYBACK_SOURCE_SHUFFLE_ORDER_H
#define CORAL_PLAYBACK_SOURCE_SHUFFLE_ORDER_H

#include <memory>
#include <vector>

namespace coral {

/**
 * Shuffled order of indices.
 *
 * The shuffle order must be immutable to ensure thread safety.
 */
class ShuffleOrder {
public:
  virtual ~ShuffleOrder() = default;

  static constexpr int INDEX_UNSET = -1;

  /**
   * Returns length of shuffle order.
   */
  virtual int getLength() const = 0;

  /**
   * Returns the next index in the shuffle order.
   *
   * @param index An index.
   * @return The index after {@code index}, or {@link #INDEX_UNSET} if {@code index} is the last
   *     element.
   */
  virtual int getNextIndex(int index) = 0;

  /**
   * Returns the previous index in the shuffle order.
   *
   * @param index An index.
   * @return The index before {@code index}, or {@link #INDEX_UNSET} if {@code index} is the first
   *     element.
   */
  virtual int getPreviousIndex(int index) = 0;

  /**
   * Returns the last index in the shuffle order, or {@link #INDEX_UNSET} if the shuffle order is
   * empty.
   */
  virtual int getLastIndex() = 0;

  /**
   * Returns the first index in the shuffle order, or {@link #INDEX_UNSET} if the shuffle order is
   * empty.
   */
  virtual int getFirstIndex() = 0;

  /**
   * Returns a copy of the shuffle order with newly inserted elements.
   *
   * @param insertionIndex The index in the unshuffled order at which elements are inserted.
   * @param insertionCount The number of elements inserted at {@code insertionIndex}.
   * @return A copy of this {@link ShuffleOrder} with newly inserted elements.
   */
  virtual std::shared_ptr<ShuffleOrder> cloneAndInsert(int insertionIndex, int insertionCount) = 0;

  /**
   * Returns a copy of the shuffle order with a range of elements removed.
   *
   * @param indexFrom The starting index in the unshuffled order of the range to remove.
   * @param indexToExclusive The smallest index (must be greater or equal to {@code indexFrom}) that
   *     will not be removed.
   * @return A copy of this {@link ShuffleOrder} without the elements in the removed range.
   */
  virtual std::shared_ptr<ShuffleOrder> cloneAndRemove(int indexFrom, int indexToExclusive) = 0;

  /** Returns a copy of the shuffle order with all elements removed. */
  virtual std::shared_ptr<ShuffleOrder> cloneAndClear() = 0;
};

/**
 * The default {@link ShuffleOrder} implementation for random shuffle order.
 */
class DefaultShuffleOrder final : public ShuffleOrder {
private:
  std::vector<int> shuffled;
  std::vector<int> indexInShuffled;

public:
  explicit DefaultShuffleOrder(int length) noexcept;

  explicit DefaultShuffleOrder(const std::vector<int>& shuffledIndices) noexcept;

  ~DefaultShuffleOrder() override = default;

  int getLength() const override;

  int getNextIndex(int index) override;

  int getPreviousIndex(int index) override;

  int getLastIndex() override;

  int getFirstIndex() override;

  std::shared_ptr<ShuffleOrder> cloneAndInsert(int insertionIndex, int insertionCount) override;

  std::shared_ptr<ShuffleOrder> cloneAndRemove(int indexFrom, int indexToExclusive) override;

  std::shared_ptr<ShuffleOrder> cloneAndClear() override;
};

/**
 * A {@link ShuffleOrder} implementation which does not shuffle.
 */
class UnShuffledShuffleOrder final : public ShuffleOrder {
private:
  int length;

public:
  explicit UnShuffledShuffleOrder(int length) noexcept
      : length(length) {}

  ~UnShuffledShuffleOrder() override = default;

  [[nodiscard]] int getLength() const override;

  int getNextIndex(int index) override;

  int getPreviousIndex(int index) override;

  int getLastIndex() override;

  int getFirstIndex() override;

  std::shared_ptr<ShuffleOrder> cloneAndInsert(int insertionIndex, int insertionCount) override;

  std::shared_ptr<ShuffleOrder> cloneAndRemove(int indexFrom, int indexToExclusive) override;

  std::shared_ptr<ShuffleOrder> cloneAndClear() override;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_SHUFFLE_ORDER_H
