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

#ifndef CORAL_PLAYBACK_DETAILS_SUBTITLE_OVERLAY_NODE_H
#define CORAL_PLAYBACK_DETAILS_SUBTITLE_OVERLAY_NODE_H

#include <extractor/text/CueGroup.h>

#include <subtitle/SubtitleRenderer.h>
#include <subtitle/SubtitleStyle.h>

#include <effect/EffectTypes.h>

#include <cstdint>
#include <memory>

namespace coral {

class EffectEngine;
class EffectPassNode;

/**
 * Composites the subtitle cues into the graph with one GPU pass.
 *
 * <p>The cue group is rasterised by `SubtitleRenderer` only when it changes (or when the output
 * size changes); the result is uploaded once as an `EffectImage` and sampled by the pass. An empty
 * group disables the pass, which the effect graph turns into a pass-through, so a video without
 * subtitles costs no extra sampling.
 *
 * <p>The overlay is rendered at the graph's output size, after the layout pass, so center-crop
 * cannot cut cues off and their coordinates are the same as the displayed frame's.
 */
class SubtitleOverlayNode final {
public:
  /** Creates and attaches the pass on @p engine. */
  static std::shared_ptr<SubtitleOverlayNode> create(const std::shared_ptr<EffectEngine>& engine);

  ~SubtitleOverlayNode();

  SubtitleOverlayNode(const SubtitleOverlayNode&) = delete;
  SubtitleOverlayNode& operator=(const SubtitleOverlayNode&) = delete;

  /** Sets the cues to draw, or clears them with an empty group. */
  void setCueGroup(const AVCueGroup& cues);

  /** Sets the output size used to rasterise the cues. */
  void setOutputSize(uint32_t width, uint32_t height);

  void setEnabled(bool enabled);

  [[nodiscard]] const std::shared_ptr<EffectPassNode>& node() const noexcept { return mPass; }

  [[nodiscard]] bool isEnabled() const noexcept;

private:
  explicit SubtitleOverlayNode(std::shared_ptr<EffectPassNode> pass);

  void renderCues();

  std::shared_ptr<EffectPassNode> mPass;
  SubtitleRenderer mRenderer;
  AVCueGroup mCues;
  SubtitleStyle mStyle;
  TextDisplayOptions mOptions;
  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
  bool mEnabled = false;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_SUBTITLE_OVERLAY_NODE_H
