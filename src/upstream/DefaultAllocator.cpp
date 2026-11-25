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

#include <coral/upstream/DefaultAllocator.h>

#include <algorithm>
#include <cassert>

namespace coral {

DefaultAllocator::DefaultAllocator(const bool trimOnReset, const int individualAllocationSize) noexcept
  : DefaultAllocator(trimOnReset, individualAllocationSize, 0) {
}

DefaultAllocator::DefaultAllocator(const bool trimOnReset,
  int individualAllocationSize,
  const int initialAllocationCount) noexcept
  : trimOnReset(trimOnReset),
    individualAllocationSize(individualAllocationSize),
    targetBufferSize(0),
    allocatedCount(0),
    availableCount(initialAllocationCount) {
  availableAllocations.resize(initialAllocationCount + AVAILABLE_EXTRA_CAPACITY);

  if (initialAllocationCount > 0) {
    const auto initialBlockSize = initialAllocationCount * individualAllocationSize;
    initialAllocationBlock = std::make_unique<uint8_t[]>(initialBlockSize);
    for (int i = 0; i < initialAllocationCount; ++i) {
      const auto allocationOffset = static_cast<size_t>(i * individualAllocationSize);
      // Every allocation of the block borrows its range: the block is owned by the allocator and
      // outlives all of them.
      availableAllocations[static_cast<size_t>(i)] =
        std::make_shared<Allocation>(initialAllocationBlock.get(),
          allocationOffset,
          static_cast<size_t>(individualAllocationSize));
    }
  }
}

DefaultAllocator::~DefaultAllocator() {
  reset();
}

void DefaultAllocator::reset() {
  if (trimOnReset) {
    setTargetBufferSize(0);
  }
}

void DefaultAllocator::setTargetBufferSize(const int64_t bufferSize) {
  const auto targetBufferSizeReduced = bufferSize < targetBufferSize;
  targetBufferSize = static_cast<int>(bufferSize);
  if (targetBufferSizeReduced) {
    trim();
  }
}

std::shared_ptr<Allocation> DefaultAllocator::allocate() {
  allocatedCount++;
  if (availableCount > 0) {
    // The slots below `availableCount` are never null: `allocate` clears the slot it took, `trim`
    // lowers `availableCount` past the slots it cleared, and `release` fills the slots it claims.
    auto allocation = availableAllocations[static_cast<size_t>(--availableCount)];
    availableAllocations[static_cast<size_t>(availableCount)] = nullptr;
    assert(allocation != nullptr);
    return allocation;
  }
  // The buffer is owned by a `std::unique_ptr` until the allocation has taken it, so a failure
  // while the allocation is created cannot leak it.
  auto buffer = std::make_unique<uint8_t[]>(static_cast<size_t>(individualAllocationSize));
  auto allocation =
    std::make_shared<Allocation>(std::move(buffer), static_cast<size_t>(individualAllocationSize));
  ensureAvailableCapacity(availableCount + 1);
  return allocation;
}

void DefaultAllocator::release(const std::shared_ptr<Allocation>& allocation) {
  if (allocation == nullptr) {
    return;
  }
  ensureAvailableCapacity(availableCount + 1);
  availableAllocations[static_cast<size_t>(availableCount++)] = allocation;
  allocatedCount--;
}

void DefaultAllocator::release(const std::shared_ptr<Node>& allocationNode) {
  auto node = allocationNode;
  while (node != nullptr) {
    const auto& allocation = node->getAllocation();
    if (allocation != nullptr) {
      ensureAvailableCapacity(availableCount + 1);
      availableAllocations[static_cast<size_t>(availableCount++)] = allocation;
      allocatedCount--;
    }
    auto next = node->getNext();
    if (next == node) {
      // Defensive: a chain that points at itself would never end.
      break;
    }
    node = std::move(next);
  }
}

void DefaultAllocator::release(const std::vector<std::shared_ptr<Allocation>>& allocations) {
  const auto recycleSize = static_cast<int>(allocations.size());
  if (recycleSize == 0) {
    return;
  }
  ensureAvailableCapacity(availableCount + recycleSize);
  for (const auto& allocation : allocations) {
    if (allocation != nullptr) {
      availableAllocations[static_cast<size_t>(availableCount++)] = allocation;
    }
  }
  allocatedCount -= recycleSize;
}

void DefaultAllocator::ensureAvailableCapacity(const int requiredCapacity) {
  if (requiredCapacity <= static_cast<int>(availableAllocations.size())) {
    return;
  }
  const auto grownCapacity =
    std::max(availableAllocations.size() * 2, static_cast<size_t>(requiredCapacity));
  availableAllocations.resize(grownCapacity);
}


void DefaultAllocator::trim() {
  const auto targetAllocationCount = ceilDivide(targetBufferSize, individualAllocationSize);
  auto targetAvailableCount = std::max(0, targetAllocationCount - allocatedCount);
  if (targetAvailableCount >= availableCount) {
    // We're already at or below the target.
    return;
  }

  if (initialAllocationBlock != nullptr) {
    // Some allocations are backed by an initial block. We need to make sure
    // that we hold onto all such allocations. Re-order the available
    // allocations so that the ones backed by the initial block come first.
    int lowIndex = 0;
    int highIndex = availableCount - 1;
    while (lowIndex <= highIndex) {
      if (availableAllocations[static_cast<size_t>(lowIndex)]->data() ==
          initialAllocationBlock.get()) {
        lowIndex++;
      } else {
        if (availableAllocations[static_cast<size_t>(highIndex)]->data() !=
            initialAllocationBlock.get()) {
          highIndex--;
        } else {
          // swap the value
          auto lowAllocation = std::move(availableAllocations[static_cast<size_t>(lowIndex)]);
          auto highAllocation = std::move(availableAllocations[static_cast<size_t>(highIndex)]);
          availableAllocations[static_cast<size_t>(lowIndex++)] = std::move(highAllocation);
          availableAllocations[static_cast<size_t>(highIndex--)] = std::move(lowAllocation);
        }
      }
    }
    // lowIndex is the index of the first allocation not backed by an initial
    // block.
    targetAvailableCount = std::max(targetAvailableCount, lowIndex);
    if (targetAvailableCount >= availableCount) {
      // We're already at or below the target.
      return;
    }
  }

  // Discard allocations beyond the target.
  for (int i = targetAvailableCount; i < availableCount; ++i) {
    availableAllocations[static_cast<size_t>(i)] = nullptr;
  }
  availableCount = targetAvailableCount;
}

int DefaultAllocator::getTotalBytesAllocated() {
  return allocatedCount * individualAllocationSize;
}

int DefaultAllocator::getCapacityPerAllocation() const {
  return individualAllocationSize;
}

} // namespace coral
