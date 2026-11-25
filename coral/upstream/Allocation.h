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

#ifndef CORAL_PLAYBACK_UPSTREAM_ALLOCATION_H
#define CORAL_PLAYBACK_UPSTREAM_ALLOCATION_H

#include <cstdint>
#include <memory>

namespace coral {

/**
 * An allocation within a byte array.
 *
 * <p>The allocation either owns its buffer or borrows it:
 *
 * <ul>
 *   <li>the {@link Allocation(std::unique_ptr<uint8_t[]>, size_t)} constructor takes ownership, and
 *       the buffer is freed when the allocation is destroyed;
 *   <li>the {@link Allocation(uint8_t*, size_t, size_t)} constructor borrows a range of a buffer
 *       that somebody else owns, e.g. one shared block that backs several allocations of an
 *       allocator, and frees nothing.
 * </ul>
 *
 * <p>The allocation is neither copyable nor movable: the buffers of a sample queue are handed
 * around by {@code std::shared_ptr}, which is what makes the ownership unambiguous.
 */
class Allocation final {
public:
  Allocation(const Allocation&) = delete;
  Allocation& operator=(const Allocation&) = delete;
  Allocation(Allocation&&) = delete;
  Allocation& operator=(Allocation&&) = delete;

  /**
   * Creates an allocation that owns {@code data}.
   *
   * @param data The buffer of the allocation, which must have been allocated with
   *             {@code new uint8_t[]}. Must not be null.
   * @param size The size of the buffer.
   */
  explicit Allocation(std::unique_ptr<uint8_t[]> data, size_t size) noexcept;

  /**
   * Creates an allocation that borrows a range of a buffer.
   *
   * @param data The buffer, which is owned by somebody else and must outlive the allocation.
   * @param offset The offset of the range within the buffer.
   * @param size The size of the range.
   */
  explicit Allocation(uint8_t* data, size_t offset, size_t size) noexcept;

  ~Allocation() = default;

  [[nodiscard]] const uint8_t* data() const { return mData; }

  uint8_t* data() { return mData; }

  [[nodiscard]] bool empty() const { return mDataByteSize == 0; }

  [[nodiscard]] size_t offset() const { return mOffset; }

  [[nodiscard]] size_t size() const { return mDataByteSize; }

  /** Whether this allocation owns the buffer it points into. */
  [[nodiscard]] bool ownsData() const { return mOwnedData != nullptr; }

private:
  /** The owned buffer, or null when the buffer is borrowed. */
  std::unique_ptr<uint8_t[]> mOwnedData;

  /** The start of the range this allocation describes, owned or borrowed. */
  uint8_t* mData;

  size_t mOffset;
  size_t mDataByteSize;
};

} // namespace coral

#endif // CORAL_PLAYBACK_UPSTREAM_ALLOCATION_H
