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

#ifndef CORAL_PLAYBACK_DETAILS_IMAGE_RENDERER_H
#define CORAL_PLAYBACK_DETAILS_IMAGE_RENDERER_H

#include "BaseRenderer.h"

#include <memory>

namespace coral {

class EffectEngine;
class VideoEffectGraph;

/**
 * A {@link Renderer} implementation for rendering images.
 *
 * <p>An image track is one encoded sample. The renderer decodes it with the graphics ImageCodec,
 * hands the RGBA frame to the player's effect graph, and leaves the input node repeating the last
 * frame. The graph's layout/pass/output chain then presents it exactly like a video frame.
 */
class ImageRenderer final : public BaseRenderer {
public:
  ImageRenderer();

  ~ImageRenderer() override;

  /** Switches the renderer to the player's effect graph. */
  void setVideoEffectGraph(std::shared_ptr<VideoEffectGraph> graph);

  Capabilities supportsFormat(const std::shared_ptr<MediaFormat>& format) override;

  void render(int64_t positionUs, int64_t elapsedRealtimeUs) override;

  void handleMessage(int type, const Message* msg) override;

  bool isReady() override;

  bool isEnded() override;

  [[nodiscard]] const char* getName() const override {
    return "ImageRenderer";
  }

protected:
  void onPositionReset(int64_t positionUs, bool joining) override;

  void onDisabled() override;

private:
  void resetDecodedFrame();

  std::shared_ptr<VideoEffectGraph> mVideoEffectGraph;
  std::shared_ptr<AVPacketBuffer> mEncodedBuffer;
  std::shared_ptr<MediaFormat> mFormat;
  bool mDecoded = false;
};

} // namespace coral

#endif //CORAL_PLAYBACK_DETAILS_IMAGE_RENDERER_H
