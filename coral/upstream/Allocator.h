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

#ifndef CORAL_PLAYBACK_UPSTREAM_ALLOCATOR_H
#define CORAL_PLAYBACK_UPSTREAM_ALLOCATOR_H

#include <coral/upstream/Allocation.h>

#include <memory>
#include <vector>

namespace coral {

/**
 * A source of allocations.
 */
class Allocator {
public:
  virtual ~Allocator() = default;

  /** A node in a chain of {@link Allocation Allocations}. */
  struct Node {
    virtual ~Node() = default;

    /** Returns the {@link Allocation} associated to this chain node. */
    [[nodiscard]] virtual const std::shared_ptr<Allocation>& getAllocation() const = 0;

    /** Returns the next chain node, or {@code null} if this is the last node in the chain. */
    virtual std::shared_ptr<Node> getNext() = 0;
  };

  virtual void reset() {}

  virtual void setTargetBufferSize(int64_t bufferSize) {}

  /**
   * Obtain a {@link Allocation}
   */
  virtual std::shared_ptr<Allocation> allocate() = 0;

  /**
   * Releases an {@link Allocation} back to the allocator.
   *
   * @param sampleData The {@link Allocation} being released.
   */
  virtual void release(const std::shared_ptr<Allocation>& sampleData) = 0;

  /**
   * Releases an array of {@link Allocation}s back to the allocator.
   *
   * @param sampleDataList The array of {@link Allocation}s being released.
   */
  virtual void release(const std::vector<std::shared_ptr<Allocation>>& sampleDataList) = 0;

  /**
   * Releases all {@link Allocation Allocations} in the chain starting at the given {@link
   * AllocationNode}.
   *
   * <p>An implementation may grow its own bookkeeping, but must not allocate a media buffer: this
   * runs on the thread that released the samples, and the point of the overload is to return a whole
   * chain at once without a per-allocation call.
   */
  virtual void release(const std::shared_ptr<Node>& allocationNode) = 0;

  /**
   * Hints to the allocator that it should make a best effort to release any excess {@link Allocation}s.
   */
  virtual void trim() = 0;

  /**
   * Returns the total number of bytes currently allocated.
   */
  virtual int getTotalBytesAllocated() = 0;

  /**
   * Returns the length of each individual {@link Allocation}.
   */
  [[nodiscard]] virtual int getCapacityPerAllocation() const = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_UPSTREAM_ALLOCATOR_H
