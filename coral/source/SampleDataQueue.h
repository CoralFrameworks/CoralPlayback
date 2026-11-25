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

#ifndef CORAL_PLAYBACK_SOURCE_SAMPLE_DATA_QUEUE_H
#define CORAL_PLAYBACK_SOURCE_SAMPLE_DATA_QUEUE_H

#include <coral/upstream/Allocation.h>
#include <coral/upstream/Allocator.h>
#include <media/PacketBuffer.h>

#include <atomic>
#include <memory>

namespace coral {

struct SampleExtrasHolder;

/**
 * A queue of media sample data. Only used for {@link SampleQueue}
 */
class SampleDataQueue final {
public:
  /**
   * The queue owns the allocation chain it holds and returns the allocations to its allocator, so a
   * copy would release the same allocations twice.
   */
  SampleDataQueue(const SampleDataQueue& other) = delete;
  SampleDataQueue& operator=(const SampleDataQueue& other) = delete;
  SampleDataQueue(SampleDataQueue&& other) = delete;
  SampleDataQueue& operator=(SampleDataQueue&& other) = delete;

private:
  struct AllocationNode final : Allocator::Node {
    /** The absolute position of the start of the data (inclusive). */
    int64_t startPosition;

    /** The absolute position of the end of the data (exclusive). */
    int64_t endPosition;

    /** The {@link Allocation}, or nullptr if the node is not initialized. */
    std::shared_ptr<Allocation> allocation;
    /**
     * The next {@link AllocationNode} in the list, or nullptr if the node has
     * not been initialized. Remains set after {@link clear()}.
     */
    std::shared_ptr<AllocationNode> next;

    explicit AllocationNode(const int64_t startPosition, const int64_t allocationLength) noexcept
        : startPosition(startPosition),
          endPosition(startPosition + allocationLength),
          allocation(nullptr),
          next(nullptr) {}

    /**
     * Initializes the node.
     *
     * @param newAllocation The node's {@link Allocation}.
     * @param nexNode The next {@link AllocationNode}.
     */
    void initialize(const std::shared_ptr<Allocation>& newAllocation,
        const std::shared_ptr<AllocationNode>& nexNode) {
      this->allocation = newAllocation;
      this->next = nexNode;
    }

    /**
     * Gets the offset into the {@link allocation}'s {@link Allocation#data()}
     * that corresponds to the specified absolute position.
     *
     * @param absolutePosition The absolute position.
     * @return The corresponding offset into the Allocation's data.
     */
    [[nodiscard]] int translateOffset(const int64_t absolutePosition) const {
      return static_cast<int>(absolutePosition - startPosition + allocation->offset());
    }

    /**
     * Clears {@link allocation} and {@link next}.
     *
     * @return The cleared next {@link AllocationNode}.
     */
    std::shared_ptr<AllocationNode> clear() {
      allocation = nullptr;
      auto temp = next;
      next = nullptr;
      return temp;
    }

    [[nodiscard]] const std::shared_ptr<Allocation>& getAllocation() const override {
      return allocation;
    }

    /**
     * Returns the next node of the chain, or null when this is its last node.
     *
     * @discussion
     * The node is returned by value: a reference to the node that is held here could not be handed
     * out, because the caller walks the chain as it releases it.
     */
    std::shared_ptr<Node> getNext() override {
      if (next == nullptr || next->allocation == nullptr) {
        return nullptr;
      }
      return next;
    }
  };

  size_t allocationLength;

  std::shared_ptr<Allocator> mAllocator;

  // References into the linked list of allocations.
  std::shared_ptr<AllocationNode> firstNode;
  std::shared_ptr<AllocationNode> readNode;
  std::shared_ptr<AllocationNode> writeNode;

  // Accessed only by the loading thread (or the consuming thread when there is no loading thread).
  int64_t totalBytesWritten;

public:
  explicit SampleDataQueue(const std::shared_ptr<Allocator>& allocator) noexcept;

  ~SampleDataQueue();

  /**
   * Clears all sample data.
   */
  void reset();

  /**
   * Discards sample data bytes from the write side of the queue.
   *
   * @param totalWritten The reduced total number of bytes written after the
   * samples have been discarded, or 0 if the queue is now empty.
   */
  void discardUpstreamSampleBytes(int64_t totalWritten);

  /**
   * Rewinds the read position to the first sample in the queue.
   */
  void rewind() { readNode = firstNode; }

  /**
   * Reads data from the rolling buffer to populate a decoder input buffer.
   *
   * @param buffer The buffer to populate.
   * @param extrasHolder The extras holder whose offset should be read and
   * subsequently adjusted.
   */
  void readToBuffer(AVPacketBuffer* buffer, SampleExtrasHolder& extrasHolder);

  /**
   * Advances the read position to the specified absolute position.
   *
   * @param absolutePosition The new absolute read position. May be {@link
   *     Definitions::kPositionUnset}, in which case calling this method is a
   * no-op.
   */
  void discardDownstreamTo(int64_t absolutePosition);

  [[nodiscard]] int64_t getTotalBytesWritten() const { return totalBytesWritten; }

  void sampleData(const uint8_t* buffer, int length);

private:
  /**
   * Reads data from the front of the rolling buffer.
   *
   * @param absolutePosition The absolute position from which data should be
   * read.
   * @param target The array into which data should be written.
   * @param length The number of bytes to read.
   */
  void readData(int64_t absolutePosition, uint8_t* target, int length);

  void readData(AVPacketBuffer* buffer, int64_t absolutePosition, int length);

  /**
   * Advances the read position to the specified absolute position.
   *
   * @param absolutePosition The position to which {@link #readAllocationNode}
   * should be advanced.
   */
  void advanceReadTo(int64_t absolutePosition);

  /**
   * Clears allocation nodes starting from {@code fromNode}.
   *
   * @param fromNode The node from which to clear.
   */
  void clearAllocationNodes(const std::shared_ptr<AllocationNode>& fromNode);

  /**
   * Called before writing sample data to {@link #writeAllocationNode}. May
   * cause {@link #writeAllocationNode} to be initialized.
   *
   * @param length The number of bytes that the caller wishes to write.
   * @return The number of bytes that the caller is permitted to write, which
   * may be less than
   *     {@code length}.
   */
  int preAppend(int length);

  /**
   * Called after writing sample data. May cause {@link #writeAllocationNode} to
   * be advanced.
   *
   * @param length The number of bytes that were written.
   */
  void postAppend(int length);
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_SAMPLE_DATA_QUEUE_H
