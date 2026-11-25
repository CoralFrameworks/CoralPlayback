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

#include "AndroidVideoFrameSink.h"

#include <media/PixelBuffer.h>

namespace coral {

bool AndroidSurfaceTextureFrameSink::isPullModel() const {
  return true;
}

bool AndroidSurfaceTextureFrameSink::submit(std::shared_ptr<AVPixelBuffer> frame, int64_t positionUs) {
  static_cast<void>(positionUs);
  // The sampled-surface decoder already rendered the frame and released the MediaCodec index; the
  // ticket only has to return to the decoder's pool. The SurfaceTexture input node owns the pixels.
  if (frame != nullptr) {
    frame->release();
  }
  return true;
}

void AndroidSurfaceTextureFrameSink::discard(std::shared_ptr<AVPixelBuffer> frame) {
  if (frame != nullptr) {
    frame->release();
  }
}

void AndroidSurfaceTextureFrameSink::flush() {
  // The SurfaceTexture queue belongs to the input node and is drained by the engine's render thread;
  // there is no sink-side frame to drop.
}

bool AndroidSurfaceTextureFrameSink::isZeroCopy() const {
  return true;
}

} // namespace coral
