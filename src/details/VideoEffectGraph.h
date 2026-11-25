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

#ifndef CORAL_PLAYBACK_DETAILS_VIDEO_EFFECT_GRAPH_H
#define CORAL_PLAYBACK_DETAILS_VIDEO_EFFECT_GRAPH_H

#include <effect/EffectOutputNode.h>
#include <effect/EffectTypes.h>
#include <effect/PixelBufferView.h>

#include "LottieOverlayNode.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace coral {

class EffectEngine;
class EffectInputNode;
class EffectOutputNode;
class VideoLayoutPass;
class SubtitleOverlayNode;
struct VideoFrameDesc;
class AVCueGroup;

/**
 * The player's effect graph: one input, the layout pass, and one output.
 *
 * <p>This is the skeleton the rest of LivePlayer's video path is built on. It deliberately owns no
 * platform type: the input is a `EffectInputNode` the platform factory created (SurfaceTexture,
 * AHB, or CPU frames), and the output is the node a `VideoOutputNodeFactory` created from the
 * application's surface. The graph owns the connections and the layout pass, while the player owns
 * the engine and both endpoints.
 *
 * <p>Later milestones insert subtitle/Lottie passes between the layout pass and the output. The
 * linear shape here is what they extend; keeping it separate from `AvLivePlayer` means the graph
 * can be built and validated before the player's renderer refactor.
 */
class VideoEffectGraph final {
public:
  /**
   * Builds and connects the graph. The engine is not started.
   *
   * @return the graph, or null when an argument is null or the graph could not be connected.
   */
  static std::shared_ptr<VideoEffectGraph> create(
      const std::shared_ptr<EffectEngine>& engine,
      const std::shared_ptr<EffectInputNode>& input,
      const std::shared_ptr<EffectOutputNode>& output);

  ~VideoEffectGraph();

  VideoEffectGraph(const VideoEffectGraph&) = delete;
  VideoEffectGraph& operator=(const VideoEffectGraph&) = delete;

  /** Starts the engine; the graph must already be built. */
  EffectResult start();

  /** Stops the engine. Nodes and connections survive, so `start()` may be called again. */
  EffectResult stop();

  /**
   * Replaces the primary output while the graph is built.
   *
   * <p>The layout pass is reconnected to @p output and the old output is removed from the engine,
   * so it is no longer consumed. This is the late-surface path: a headless `NullOutputNode` is
   * replaced with the platform node once the application surface exists.
   */
  EffectResult setOutput(const std::shared_ptr<EffectOutputNode>& output);

  /** Sets the engine's output size (the layout pass target). */
  EffectResult setRenderSize(uint32_t width, uint32_t height);

  /** Enables or disables the GPU subtitle overlay. */
  void setSubtitleEnabled(bool enabled);

  /** Publishes the cues the subtitle overlay should draw. */
  void setCueGroup(const AVCueGroup& cues);

  /** Adds a pre-rendered Lottie overlay before the subtitle pass. */
  std::shared_ptr<LottieOverlayNode> addLottieOverlay(const LottieOverlayConfig& config);

  void removeLottieOverlay(const std::shared_ptr<LottieOverlayNode>& overlay);

  void clearLottieOverlays();

  /** Drives all Lottie overlays from the media (or independent) timeline. */
  void setPositionUs(int64_t positionUs);

  /** Submits a CPU or platform view to the input node. */
  EffectResult submit(const PixelBufferView& view);

  /** Submits a decoded CPU frame to the input node. */
  EffectResult submit(const VideoFrameDesc& frame);

  /** Renders one frame in Manual mode; in Continuous mode this requests an extra frame. */
  EffectResult execute();

  EffectResult setScaleMode(EffectScaleMode mode);

  /** Rotates the video clockwise before it is fitted into the output. */
  EffectResult setVideoRotation(int degrees);

  /** Mirrors the fitted video. */
  EffectResult setVideoMirror(bool horizontal, bool vertical);

  [[nodiscard]] const std::shared_ptr<EffectEngine>& engine() const noexcept { return mEngine; }

  [[nodiscard]] const std::shared_ptr<EffectInputNode>& input() const noexcept { return mInput; }

  [[nodiscard]] const std::shared_ptr<EffectOutputNode>& output() const noexcept { return mOutput; }

  [[nodiscard]] const std::shared_ptr<VideoLayoutPass>& layout() const noexcept { return mLayout; }

  [[nodiscard]] const std::shared_ptr<SubtitleOverlayNode>& subtitle() const noexcept {
    return mSubtitle;
  }

  [[nodiscard]] const std::vector<std::shared_ptr<LottieOverlayNode>>& lottieOverlays() const noexcept {
    return mLottieOverlays;
  }

  [[nodiscard]] bool isBuilt() const noexcept { return mBuilt; }

private:
  VideoEffectGraph(std::shared_ptr<EffectEngine> engine, std::shared_ptr<EffectInputNode> input,
                   std::shared_ptr<EffectOutputNode> output,
                   std::shared_ptr<VideoLayoutPass> layout,
                   std::shared_ptr<SubtitleOverlayNode> subtitle) noexcept;

  EffectResult build();

  EffectResult disconnectChain();

  EffectResult connectChain();

  void rebuildChain();

  std::shared_ptr<EffectEngine> mEngine;
  std::shared_ptr<EffectInputNode> mInput;
  std::shared_ptr<EffectOutputNode> mOutput;
  std::shared_ptr<VideoLayoutPass> mLayout;
  std::shared_ptr<SubtitleOverlayNode> mSubtitle;
  std::vector<std::shared_ptr<LottieOverlayNode>> mLottieOverlays;
  uint32_t mRenderWidth = 2;
  uint32_t mRenderHeight = 2;
  /** `layout -> lottie... -> subtitle`; input feeds `mChain.front()`. */
  std::vector<std::shared_ptr<EffectNode>> mChain;
  bool mBuilt = false;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_VIDEO_EFFECT_GRAPH_H
