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

#ifndef CORAL_PLAYBACK_UPSTREAM_DEFAULT_ALLOCATOR_H
#define CORAL_PLAYBACK_UPSTREAM_DEFAULT_ALLOCATOR_H

#include <coral/upstream/Allocator.h>

#include <vector>

namespace coral {

/**
 * Default implementation of {@link Allocator}.
 */
class DefaultAllocator final : public Allocator {
public:
  /**
   * Constructs an instance without creating any {@link Allocation}s up front.
   *
   * @param trimOnReset Whether memory is freed when the allocator is reset.
   * Should be true unless the allocator will be re-used by multiple player
   * instances.
   * @param individualAllocationSize The length of each individual {@link
   * Allocation}.
   */
  explicit DefaultAllocator(bool trimOnReset, int individualAllocationSize) noexcept;

  /**
   * Constructs an instance without creating any {@link Allocation}s up front.
   *
   * @param trimOnReset Whether memory is freed when the allocator is reset. Should be true unless
   * the allocator will be re-used by multiple player instances.
   * @param individualAllocationSize The length of each individual {@link Allocation}.
   * @param initialAllocationCount The number of allocations to create up front.
   */
  explicit DefaultAllocator(
      bool trimOnReset, int individualAllocationSize, int initialAllocationCount) noexcept;

  ~DefaultAllocator() override;

  void reset() override;

  void setTargetBufferSize(int64_t bufferSize) override;

  std::shared_ptr<Allocation> allocate() override;

  void release(const std::shared_ptr<Allocation>& sampleData) override;

  void release(const std::vector<std::shared_ptr<Allocation>>& allocations) override;

  void release(const std::shared_ptr<Node>& allocations) override;

  void trim() override;

  int getTotalBytesAllocated() override;

  int getCapacityPerAllocation() const override;

private:
  static int ceilDivide(const int numerator, const int denominator) {
    return (numerator + denominator - 1) / denominator;
  }

  /**
   * Grows the list of available allocations so that it can hold {@code requiredCapacity} entries.
   *
   * Every release writes at the index of the free list, so the list has to be grown before the
   * write: an allocation that is released when the list is full would otherwise write past its end.
   */
  void ensureAvailableCapacity(int requiredCapacity);

  static constexpr int AVAILABLE_EXTRA_CAPACITY = 100;

  bool trimOnReset;
  int individualAllocationSize;

  /**
   * The block that backs the allocations created up front, or null when none were. It is declared
   * before {@link #availableAllocations} so that it outlives the allocations that borrow it.
   */
  std::unique_ptr<uint8_t[]> initialAllocationBlock;

  int targetBufferSize;
  int allocatedCount;
  int availableCount;
  std::vector<std::shared_ptr<Allocation>> availableAllocations;
};

} // namespace coral

#endif // CORAL_PLAYBACK_UPSTREAM_DEFAULT_ALLOCATOR_H
