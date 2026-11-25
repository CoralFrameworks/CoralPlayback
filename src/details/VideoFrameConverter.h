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

#ifndef CORAL_PLAYBACK_DETAILS_VIDEO_FRAME_CONVERTER_H
#define CORAL_PLAYBACK_DETAILS_VIDEO_FRAME_CONVERTER_H

#include <effect/VideoFrameInputNode.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace coral {

class AVPixelBuffer;

/**
 * Converts an `AVPixelBuffer` the decoder produced into the effect input's frame description.
 *
 * <p>The player's CPU fallback path is fed by I420 and NV12 frames. I420 is handed to the input node
 * without a conversion; NV12 is converted into an internal scratch buffer because the effect input
 * consumes the usual three-plane I420 layout. The pointers in the returned description are only
 * valid until the next `convert` call; `VideoFrameInputNode::submit` copies them synchronously.
 */
class VideoFrameConverter final {
public:
  VideoFrameConverter();
  ~VideoFrameConverter();

  VideoFrameConverter(const VideoFrameConverter&) = delete;
  VideoFrameConverter& operator=(const VideoFrameConverter&) = delete;

  /**
   * Fills @p frame from @p buffer.
   *
   * @return false when the buffer is null, is a surface format, or has an unsupported pixel layout.
   */
  /**
   * @param displayWidth/displayHeight The container's display size. Decoders may align their output above
   * it (1080 becomes 1088), and the padding on the right/bottom is not picture; passing the display size
   * crops it instead of leaving a green strip. Pass 0 to use the buffer's own size.
   */
  bool convert(const std::shared_ptr<AVPixelBuffer>& buffer, VideoFrameDesc* frame,
    int displayWidth = 0, int displayHeight = 0);

private:
  /** Scratch storage for an NV12 -> I420 conversion. */
  std::vector<uint8_t> mNv12ToI420;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_VIDEO_FRAME_CONVERTER_H
