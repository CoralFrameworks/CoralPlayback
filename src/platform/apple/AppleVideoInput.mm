/*
 * Copyright (C) 2026 The Coral Project
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

#include <coral/playback/apple/AppleVideoInput.h>

#include <effect/VideoFrameInputNode.h>
#include <effect/platform/apple/AppleIOSurfaceInputNode.h>

#include <utility>

namespace coral {

std::shared_ptr<EffectInputNode> AppleVideoInput::createFrameInput(const uint32_t width,
                                                                    const uint32_t height,
                                                                    const uint32_t queueDepth) {
  // The node follows a size change on submit, so a host that does not know the video size yet may
  // hand it a small size and let the first decoded frame settle the real one.
  return VideoFrameInputNode::create(width == 0 ? 2 : width, height == 0 ? 2 : height,
                                     queueDepth == 0 ? 4 : queueDepth);
}

std::shared_ptr<EffectInputNode> AppleVideoInput::createIOSurfaceInput(
    IOSurfaceRef surface, const uint32_t width, const uint32_t height,
    const EffectPixelFormat format, const uint32_t queueDepth) {
  if (surface == nullptr) {
    return nullptr;
  }
  return AppleIOSurfaceInputNode::create(surface, width, height, format,
                                         queueDepth == 0 ? 4 : queueDepth);
}

} // namespace coral
