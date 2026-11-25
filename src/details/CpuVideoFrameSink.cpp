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

#include "CpuVideoFrameSink.h"

#include <media/Format.h>
#include <media/PixelBuffer.h>

#include <utility>

namespace coral {

CpuVideoFrameSink::CpuVideoFrameSink(std::shared_ptr<VideoFrameInputNode> input)
    : mInput(std::move(input)) {
}

CpuVideoFrameSink::~CpuVideoFrameSink() = default;

void CpuVideoFrameSink::setInputFormat(const std::shared_ptr<MediaFormat>& format) {
  if (format == nullptr) {
    mDisplayWidth = 0;
    mDisplayHeight = 0;
    return;
  }
  mDisplayWidth = format->getWidth();
  mDisplayHeight = format->getHeight();
}

bool CpuVideoFrameSink::submit(std::shared_ptr<AVPixelBuffer> frame, int64_t positionUs) {
  static_cast<void>(positionUs);
  if (mInput == nullptr || frame == nullptr) {
    if (frame != nullptr) {
      frame->release();
    }
    return false;
  }

  VideoFrameDesc desc;
  if (!mConverter.convert(frame, &desc, mDisplayWidth, mDisplayHeight)) {
    frame->release();
    return false;
  }
  const EffectResult result = mInput->submit(desc);
  // The node copies/converts the planes synchronously, so the decoder buffer can go back now.
  frame->release();
  return result == EffectResult::Success;
}

void CpuVideoFrameSink::discard(std::shared_ptr<AVPixelBuffer> frame) {
  if (frame != nullptr) {
    frame->release();
  }
}

void CpuVideoFrameSink::flush() {
  // The input node owns a bounded queue and follows the engine's DropOldest policy; there is no
  // platform frame to drop here.
}

} // namespace coral
