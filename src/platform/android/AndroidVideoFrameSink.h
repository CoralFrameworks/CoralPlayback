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

#ifndef CORAL_PLAYBACK_ANDROID_VIDEO_FRAME_SINK_H
#define CORAL_PLAYBACK_ANDROID_VIDEO_FRAME_SINK_H

#include <coral/playback/VideoFrameSink.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * The pull-model sink for a decoder that renders into a `SurfaceTexture`.
 *
 * <p>Android's MediaCodec writes the frame into the SurfaceTexture and the codec buffer is only a
 * ticket for it; `AndroidSurfaceTextureInputNode` takes the pixels from the texture on the render
 * thread. There is therefore no per-frame payload to publish here: `submit` releases the ticket and
 * returns, and the input node pulls the image. `isPullModel()` tells the renderer it may do that
 * before the presentation time, which is what keeps the SurfaceTexture queue from starving the
 * decoder.
 */
class AndroidSurfaceTextureFrameSink final : public VideoFrameSink {
public:
  AndroidSurfaceTextureFrameSink() = default;

  ~AndroidSurfaceTextureFrameSink() override = default;

  [[nodiscard]] bool isPullModel() const override;

  bool submit(std::shared_ptr<AVPixelBuffer> frame, int64_t positionUs) override;

  void discard(std::shared_ptr<AVPixelBuffer> frame) override;

  void flush() override;

  [[nodiscard]] bool isZeroCopy() const override;
};

} // namespace coral

#endif // CORAL_PLAYBACK_ANDROID_VIDEO_FRAME_SINK_H
