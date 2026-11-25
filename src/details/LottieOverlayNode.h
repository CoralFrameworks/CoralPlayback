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

#ifndef CORAL_PLAYBACK_DETAILS_LOTTIE_OVERLAY_NODE_H
#define CORAL_PLAYBACK_DETAILS_LOTTIE_OVERLAY_NODE_H

#include <coral/playback/LottieOverlay.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace coral {

class EffectEngine;
class EffectPassNode;

/**
 * A Lottie animation pre-rendered into a texture sequence and composited by one GPU pass.
 *
 * <p>Lottie is a CPU vector renderer; rendering it at video frame rate would put vector
 * rasterisation on the playback path. This node instead renders the whole short loop once, at the
 * composition's own size (not the video size), and uploads the frames as an
 * `EffectTextureSequenceDesc`. Playing the animation is then `setSequenceTime`, so seek, pause and
 * speed changes are index selection rather than re-rasterisation.
 *
 * <p>The pass is disabled until the sequence is ready. It samples `uOverlay` over `uInput0` and
 * uses the framework's target size for a resolution-independent placement.
 */
class LottieOverlayNode final {
public:
  /** Parses @p config.json and pre-renders its loop. Returns null when it cannot be built. */
  static std::shared_ptr<LottieOverlayNode> create(
      const std::shared_ptr<EffectEngine>& engine, const LottieOverlayConfig& config);

  ~LottieOverlayNode();

  LottieOverlayNode(const LottieOverlayNode&) = delete;
  LottieOverlayNode& operator=(const LottieOverlayNode&) = delete;

  /** Drives the sequence from the media (or independent) timeline. */
  void setTimeUs(int64_t timeUs);

  /** Updates the output size used by the pixel layout; the shader reads the resulting rect. */
  void setOutputSize(uint32_t width, uint32_t height);

  void setEnabled(bool enabled);

  [[nodiscard]] bool isEnabled() const noexcept;

  [[nodiscard]] uint32_t frameCount() const noexcept { return mFrameCount; }

  [[nodiscard]] int zOrder() const noexcept { return mZOrder; }

  [[nodiscard]] const std::shared_ptr<EffectPassNode>& node() const noexcept { return mPass; }

private:
  LottieOverlayNode(std::shared_ptr<EffectPassNode> pass, uint32_t frameCount,
                    int64_t frameDurationUs, int64_t startTimeUs, int zOrder,
                    LottieOverlayConfig config, float aspect) noexcept;

  void updateLayout();

  std::shared_ptr<EffectPassNode> mPass;
  uint32_t mFrameCount = 0;
  int64_t mFrameDurationUs = 0;
  int64_t mStartTimeUs = 0;
  int mZOrder = 0;
  LottieOverlayConfig mConfig;
  float mAspect = 1.0f;
  uint32_t mOutputWidth = 2;
  uint32_t mOutputHeight = 2;
  bool mEnabled = true;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_LOTTIE_OVERLAY_NODE_H
