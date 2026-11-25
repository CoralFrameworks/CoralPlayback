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

#ifndef CORAL_PLAYBACK_DETAILS_CPU_VIDEO_FRAME_SINK_H
#define CORAL_PLAYBACK_DETAILS_CPU_VIDEO_FRAME_SINK_H

#include "VideoFrameConverter.h"

#include <coral/playback/VideoFrameSink.h>

#include <effect/VideoFrameInputNode.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * The default sink: decoded I420/NV12 planes are copied into a `VideoFrameInputNode`.
 *
 * <p>This is the platform-neutral fallback every host can use. The converter crops the decoder's
 * macroblock padding to the display size reported by `setInputFormat`, and the input node copies the
 * planes into its queue and uploads them on the render thread.
 */
class CpuVideoFrameSink final : public VideoFrameSink {
public:
  explicit CpuVideoFrameSink(std::shared_ptr<VideoFrameInputNode> input);

  ~CpuVideoFrameSink() override;

  void setInputFormat(const std::shared_ptr<MediaFormat>& format) override;

  bool submit(std::shared_ptr<AVPixelBuffer> frame, int64_t positionUs) override;

  void discard(std::shared_ptr<AVPixelBuffer> frame) override;

  void flush() override;

private:
  std::shared_ptr<VideoFrameInputNode> mInput;
  VideoFrameConverter mConverter;
  int mDisplayWidth = 0;
  int mDisplayHeight = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_CPU_VIDEO_FRAME_SINK_H
