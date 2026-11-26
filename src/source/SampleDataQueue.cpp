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

#include <coral/source/SampleDataQueue.h>
#include <coral/source/SampleQueue.h>

#include <coral/upstream/DefaultAllocator.h>

#include <minilog/log.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace coral {

SampleDataQueue::SampleDataQueue(
    const std::shared_ptr<Allocator>& allocator) noexcept
    : mAllocator(allocator), totalBytesWritten(0) {
  allocationLength = mAllocator->getCapacityPerAllocation();
  firstNode = std::make_shared<AllocationNode>(0, allocationLength);
  readNode = firstNode;
  writeNode = firstNode;
}

SampleDataQueue::~SampleDataQueue() {
  clearAllocationNodes(firstNode);
  readNode = nullptr;
  writeNode = nullptr;
}

void SampleDataQueue::reset() {
  clearAllocationNodes(firstNode);
  // creates new allocation node
  firstNode = std::make_shared<AllocationNode>(0, allocationLength);
  readNode = firstNode;
  writeNode = firstNode;
  totalBytesWritten = 0;
  mAllocator->trim();
}

void SampleDataQueue::discardUpstreamSampleBytes(int64_t totalWritten) {
  totalBytesWritten = totalWritten;
  if (totalBytesWritten == 0 || totalBytesWritten == firstNode->startPosition) {
    clearAllocationNodes(firstNode);
    firstNode =
        std::make_shared<AllocationNode>(totalBytesWritten, allocationLength);
    readNode = firstNode;
    writeNode = firstNode;
  } else {
    // Find the last node containing at least 1 byte of data that we need to
    // keep.
    auto lastNodeToKeep = firstNode;
    while (totalBytesWritten > lastNodeToKeep->endPosition) {
      lastNodeToKeep = lastNodeToKeep->next;
    }

    // Discard all subsequent nodes.
    auto firstNodeToDiscard = lastNodeToKeep->next;
    clearAllocationNodes(firstNodeToDiscard);
    // Reset the successor of the last node to be an uninitialized node.
    lastNodeToKeep->next = std::make_shared<AllocationNode>(
        lastNodeToKeep->endPosition, allocationLength);
    // Update writeSampleNode and readSampleNode as necessary.
    writeNode = totalBytesWritten == lastNodeToKeep->endPosition
                ? lastNodeToKeep->next
                : lastNodeToKeep;
    if (readNode == firstNodeToDiscard) {
      readNode = lastNodeToKeep->next;
    }
  }
}

void SampleDataQueue::readToBuffer(AVPacketBuffer* buffer, SampleExtrasHolder& extrasHolder) {
  // Read sample data, extracting supplemental data into a separate buffer if needed.
  if (buffer->hasSupplementalData()) {
    //        // If there is supplemental data, the sample data is prefixed by
    //        its size. scratch.reset(4); readData(extrasHolder.offset,
    //        scratch.getData(), 4); int sampleSize =
    //        scratch.readUnsignedIntToInt(); extrasHolder.offset += 4;
    //        extrasHolder.size -= 4;
    //
    //        // Write the sample data.
    //        buffer->ensureSpaceForWrite(sampleSize);
    //        readData(extrasHolder.offset, buffer->data(), sampleSize);
    //        extrasHolder.offset += sampleSize;
    //        extrasHolder.size -= sampleSize;
    //
    //        // Write the remaining data as supplemental data.
    //        buffer->resetSupplementalData(extrasHolder.size);
    //        readData(extrasHolder.offset, buffer->supplementalData(),
    //        extrasHolder.size);
  } else {
    // Write the sample data.
    readData(buffer, extrasHolder.offset, extrasHolder.size);
  }
}

void SampleDataQueue::discardDownstreamTo(int64_t absolutePosition) {
  if (absolutePosition == Definitions::kPositionUnset) {
    return;
  }
  auto readStartPosition = readNode->startPosition;
  while (absolutePosition >= firstNode->endPosition) {
    mAllocator->release(firstNode->allocation);
    firstNode = firstNode->clear();
  }
  if (readStartPosition < firstNode->startPosition) {
    // We discarded the node referenced by readSampleNode. We need to advance it
    // to the first remaining node.
    readNode = firstNode;
  }
}

void SampleDataQueue::sampleData(const uint8_t* buffer, int length) {
  if (buffer == nullptr) {
    return;
  }
  auto remaining = length;
  while (remaining > 0) {
    int bytesAppended = preAppend(remaining);
    auto dest = writeNode->allocation->data();
    auto destPos = writeNode->translateOffset(totalBytesWritten);
    std::memcpy(dest + destPos, buffer + (length - remaining), bytesAppended);
    remaining -= bytesAppended;
    postAppend(bytesAppended);
  }
}

void SampleDataQueue::readData(int64_t absolutePosition, uint8_t* target, const int length) {
  advanceReadTo(absolutePosition);
  auto remaining = length;
  while (remaining > 0) {
    const auto toCopy = std::min(remaining, static_cast<int>(readNode->endPosition - absolutePosition));
    const auto src = readNode->allocation->data();
    const auto srcPos = readNode->translateOffset(absolutePosition);
    std::memcpy(target + (length - remaining), src + srcPos, toCopy);
    remaining -= toCopy;
    absolutePosition += toCopy;
    if (absolutePosition == readNode->endPosition) {
      readNode = readNode->next;
    }
  }
}

void SampleDataQueue::readData(AVPacketBuffer* packetBuffer, int64_t absolutePosition, const int length) {
  advanceReadTo(absolutePosition);
  packetBuffer->ensureSpaceForWrite(length);
  const auto byteBuffer = packetBuffer->data();
  byteBuffer->position(0);
  auto remaining = length;
  while (remaining > 0) {
    const auto toCopy = std::min(remaining, static_cast<int>(readNode->endPosition - absolutePosition));
    const auto src = readNode->allocation->data();
    byteBuffer->put(src, readNode->translateOffset(absolutePosition), toCopy);
    remaining -= toCopy;
    absolutePosition += toCopy;
    if (absolutePosition == readNode->endPosition) {
      readNode = readNode->next;
    }
  }
  byteBuffer->flip();
}

void SampleDataQueue::advanceReadTo(const int64_t absolutePosition) {
  while (absolutePosition >= readNode->endPosition) {
    readNode = readNode->next;
  }
}

void SampleDataQueue::clearAllocationNodes(const std::shared_ptr<AllocationNode>& fromNode) {
  if (fromNode->allocation == nullptr) {
    return;
  }
  // Bulk release allocations for performance (it's significantly faster when
  // using DefaultAllocator because the allocator's lock only needs to be
  // acquired and released once) [Internal: See b/29542039].
  mAllocator->release(fromNode);
  fromNode->clear();
}

int SampleDataQueue::preAppend(int length) {
  if (writeNode->allocation == nullptr) {
    auto allocation = mAllocator->allocate();
    if (allocation == nullptr) {
      // The allocator can refuse an allocation after it was released. Writing into a null
      // allocation would take the process down; report a load error instead, which the loader turns
      // into a retry or a playback error.
      MINILOG_ERROR("the sample queue allocator returned no allocation");
      throw std::runtime_error("the sample queue allocator returned no allocation");
    }
    writeNode->initialize(allocation,
        std::make_shared<AllocationNode>(writeNode->endPosition, allocationLength));
  }
  return std::min(length, (int)(writeNode->endPosition - totalBytesWritten));
}

void SampleDataQueue::postAppend(int length) {
  totalBytesWritten += length;
  if (totalBytesWritten == writeNode->endPosition) {
    writeNode = writeNode->next;
  }
}

} // namespace coral
