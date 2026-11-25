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

#ifndef CORAL_PLAYBACK_DETAILS_VIDEO_LAYOUT_PASS_H
#define CORAL_PLAYBACK_DETAILS_VIDEO_LAYOUT_PASS_H

#include <effect/EffectOutputNode.h>
#include <effect/EffectTypes.h>

#include <memory>
#include <string_view>

namespace coral {

class EffectEngine;
class EffectPassNode;

/**
 * Maps a source-sized video frame into the engine's output-sized frame.
 *
 * <p>The graph's input textures stay at the decoded size. This pass is the one place that turns
 * them into the output composition: `Crop` is a center crop, `Fit` letterboxes, and `Stretch`
 * fills the target. Subtitles and overlays are composited after it, in output coordinates, so a
 * center crop cannot cut them off.
 *
 * <p>The pass receives `uInput0` and uses the framework's `effectSourceSize()` /
 * `effectTargetSize()` values, so no CPU-side resize is needed when the surface changes. The
 * engine is expected to have `renderWidth/renderHeight` set to the output size.
 */
class VideoLayoutPass final {
public:
  /**
   * Creates and attaches the pass on @p engine.
   *
   * @return the pass wrapper, or null when the engine is null or the shader cannot be submitted.
   */
  static std::shared_ptr<VideoLayoutPass> create(
      const std::shared_ptr<EffectEngine>& engine, std::string_view name = {});

  ~VideoLayoutPass();

  VideoLayoutPass(const VideoLayoutPass&) = delete;
  VideoLayoutPass& operator=(const VideoLayoutPass&) = delete;

  [[nodiscard]] const std::shared_ptr<EffectPassNode>& node() const noexcept {
    return mPass;
  }

  /** Sets the mapping from source to target; defaults to `Fit`. */
  EffectResult setScaleMode(EffectScaleMode mode);

  /** Sets the colour written outside a `Fit` image; defaults to opaque black. */
  EffectResult setLetterboxColor(const EffectColor& color);

  [[nodiscard]] EffectScaleMode scaleMode() const noexcept { return mScaleMode; }

private:
  explicit VideoLayoutPass(std::shared_ptr<EffectPassNode> pass) noexcept;

  std::shared_ptr<EffectPassNode> mPass;
  EffectScaleMode mScaleMode = EffectScaleMode::Fit;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_VIDEO_LAYOUT_PASS_H
