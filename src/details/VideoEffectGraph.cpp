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

#include "VideoEffectGraph.h"

#include "SubtitleOverlayNode.h"
#include "VideoLayoutPass.h"

#include <extractor/text/CueGroup.h>

#include <effect/EffectEngine.h>
#include <effect/EffectInputNode.h>
#include <effect/EffectOutputNode.h>
#include <effect/EffectPassNode.h>
#include <effect/VideoFrameInputNode.h>

#include <algorithm>
#include <utility>

namespace coral {
namespace {

bool isUsable(const std::shared_ptr<EffectNode>& node) {
  return node != nullptr;
}

} // namespace

VideoEffectGraph::VideoEffectGraph(std::shared_ptr<EffectEngine> engine,
                                   std::shared_ptr<EffectInputNode> input,
                                   std::shared_ptr<EffectOutputNode> output,
                                   std::shared_ptr<VideoLayoutPass> layout,
                                   std::shared_ptr<SubtitleOverlayNode> subtitle) noexcept
    : mEngine(std::move(engine)),
      mInput(std::move(input)),
      mOutput(std::move(output)),
      mLayout(std::move(layout)),
      mSubtitle(std::move(subtitle)) {
}

std::shared_ptr<VideoEffectGraph> VideoEffectGraph::create(
    const std::shared_ptr<EffectEngine>& engine,
    const std::shared_ptr<EffectInputNode>& input,
    const std::shared_ptr<EffectOutputNode>& output) {
  if (engine == nullptr || input == nullptr || output == nullptr) {
    return nullptr;
  }

  const std::shared_ptr<VideoLayoutPass> layout = VideoLayoutPass::create(engine);
  if (layout == nullptr) {
    return nullptr;
  }
  const std::shared_ptr<SubtitleOverlayNode> subtitle = SubtitleOverlayNode::create(engine);
  if (subtitle == nullptr) {
    return nullptr;
  }

  auto graph = std::shared_ptr<VideoEffectGraph>(
      new VideoEffectGraph(engine, input, output, layout, subtitle));
  if (graph->build() != EffectResult::Success) {
    return nullptr;
  }
  return graph;
}

VideoEffectGraph::~VideoEffectGraph() = default;

EffectResult VideoEffectGraph::build() {
  if (mBuilt) {
    return EffectResult::Success;
  }
  if (!isUsable(mInput) || !isUsable(mOutput) || mLayout == nullptr || mLayout->node() == nullptr) {
    return EffectResult::InvalidArgument;
  }
  if (mSubtitle == nullptr || mSubtitle->node() == nullptr) {
    return EffectResult::InvalidArgument;
  }

  // Nodes can be attached by their platform factory; attaching twice is an error rather than a
  // no-op, so only attach what is not already in a graph.
  if (!mInput->isAttached()) {
    if (mEngine->attach(mInput) != EffectResult::Success) {
      return EffectResult::InvalidArgument;
    }
  }
  if (!mLayout->node()->isAttached()) {
    if (mEngine->attach(mLayout->node()) != EffectResult::Success) {
      return EffectResult::InvalidArgument;
    }
  }
  if (!mSubtitle->node()->isAttached()) {
    if (mEngine->attach(mSubtitle->node()) != EffectResult::Success) {
      return EffectResult::InvalidArgument;
    }
  }
  if (!mOutput->isAttached()) {
    if (mEngine->attach(mOutput) != EffectResult::Success) {
      return EffectResult::InvalidArgument;
    }
  }

  // The layout pass is the only consumer of the input. The rest of the chain is rebuilt whenever
  // a Lottie overlay is added or removed.
  if (mEngine->connect(mInput, mLayout->node()) != EffectResult::Success) {
    return EffectResult::InvalidArgument;
  }

  rebuildChain();
  if (connectChain() != EffectResult::Success) {
    return EffectResult::InvalidArgument;
  }
  if (mEngine->connect(mChain.back(), mOutput) != EffectResult::Success) {
    return EffectResult::InvalidArgument;
  }
  if (mEngine->setOutput(mOutput) != EffectResult::Success) {
    return EffectResult::InvalidArgument;
  }

  mSubtitle->setOutputSize(2, 2);
  mBuilt = true;
  return EffectResult::Success;
}

EffectResult VideoEffectGraph::disconnectChain() {
  if (mEngine == nullptr) {
    return EffectResult::NotAttached;
  }
  for (size_t index = 0; index + 1 < mChain.size(); ++index) {
    mEngine->disconnect(mChain[index], mChain[index + 1]);
  }
  return EffectResult::Success;
}

EffectResult VideoEffectGraph::connectChain() {
  if (mEngine == nullptr || mChain.empty()) {
    return EffectResult::NotAttached;
  }
  for (size_t index = 0; index + 1 < mChain.size(); ++index) {
    if (mEngine->connect(mChain[index], mChain[index + 1]) != EffectResult::Success) {
      return EffectResult::InvalidArgument;
    }
  }
  return EffectResult::Success;
}

void VideoEffectGraph::rebuildChain() {
  mChain.clear();
  mChain.push_back(mLayout->node());
  for (const auto& overlay : mLottieOverlays) {
    if (overlay != nullptr && overlay->node() != nullptr) {
      mChain.push_back(overlay->node());
    }
  }
  if (mSubtitle != nullptr && mSubtitle->node() != nullptr) {
    mChain.push_back(mSubtitle->node());
  }
}

EffectResult VideoEffectGraph::start() {
  if (!mBuilt) {
    return EffectResult::NotAttached;
  }
  return mEngine->start();
}

EffectResult VideoEffectGraph::stop() {
  if (mEngine == nullptr) {
    return EffectResult::NotAttached;
  }
  return mEngine->stop();
}

EffectResult VideoEffectGraph::setOutput(const std::shared_ptr<EffectOutputNode>& output) {
  if (output == nullptr) {
    return EffectResult::InvalidArgument;
  }
  if (!mBuilt || mLayout == nullptr || mEngine == nullptr) {
    return EffectResult::NotAttached;
  }
  if (mOutput == output) {
    return EffectResult::Success;
  }

  if (!output->isAttached()) {
    if (mEngine->attach(output) != EffectResult::Success) {
      return EffectResult::InvalidArgument;
    }
  }
  if (mChain.empty() || mEngine->connect(mChain.back(), output) != EffectResult::Success) {
    return EffectResult::InvalidArgument;
  }
  if (mEngine->setOutput(output) != EffectResult::Success) {
    return EffectResult::InvalidArgument;
  }

  // `setOutput` keeps the previous primary as a secondary terminal. Remove it so its old window
  // cannot keep receiving frames; the caller owns the node and may keep it for a later re-attach.
  if (mOutput != nullptr && mOutput != output) {
    mEngine->removeOutput(mOutput);
    mEngine->detach(mOutput);
  }

  mOutput = output;
  return EffectResult::Success;
}

EffectResult VideoEffectGraph::setRenderSize(const uint32_t width, const uint32_t height) {
  if (mEngine == nullptr) {
    return EffectResult::NotAttached;
  }
  if (mSubtitle != nullptr) {
    mSubtitle->setOutputSize(width, height);
  }
  mRenderWidth = width;
  mRenderHeight = height;
  for (const auto& overlay : mLottieOverlays) {
    overlay->setOutputSize(width, height);
  }
  return mEngine->setRenderSize(width, height);
}

void VideoEffectGraph::setSubtitleEnabled(const bool enabled) {
  if (mSubtitle != nullptr) {
    mSubtitle->setEnabled(enabled);
  }
}

void VideoEffectGraph::setCueGroup(const AVCueGroup& cues) {
  if (mSubtitle != nullptr) {
    mSubtitle->setCueGroup(cues);
  }
}

std::shared_ptr<LottieOverlayNode> VideoEffectGraph::addLottieOverlay(
    const LottieOverlayConfig& config) {
  if (!mBuilt || mEngine == nullptr) {
    return nullptr;
  }
  const std::shared_ptr<LottieOverlayNode> overlay =
      LottieOverlayNode::create(mEngine, config);
  if (overlay == nullptr) {
    return nullptr;
  }
  overlay->setOutputSize(mRenderWidth, mRenderHeight);

  const bool wasRunning = mEngine->isRunning();
  if (wasRunning) {
    mEngine->pause();
  }
  disconnectChain();
  mLottieOverlays.push_back(overlay);
  std::stable_sort(mLottieOverlays.begin(), mLottieOverlays.end(),
                   [](const std::shared_ptr<LottieOverlayNode>& first,
                      const std::shared_ptr<LottieOverlayNode>& second) {
                     return first->zOrder() < second->zOrder();
                   });
  // The insertion order is the compositing order for equal zOrder; the subtitle pass is always last.
  rebuildChain();
  const EffectResult result = connectChain();
  if (wasRunning) {
    mEngine->resume();
  }
  if (result != EffectResult::Success) {
    mLottieOverlays.pop_back();
    rebuildChain();
    return nullptr;
  }
  return overlay;
}

void VideoEffectGraph::removeLottieOverlay(const std::shared_ptr<LottieOverlayNode>& overlay) {
  if (overlay == nullptr || mEngine == nullptr) {
    return;
  }
  const auto position = std::find(mLottieOverlays.begin(), mLottieOverlays.end(), overlay);
  if (position == mLottieOverlays.end()) {
    return;
  }

  const bool wasRunning = mEngine->isRunning();
  if (wasRunning) {
    mEngine->pause();
  }
  disconnectChain();
  mLottieOverlays.erase(position);
  mEngine->detach(overlay->node());
  rebuildChain();
  connectChain();
  if (wasRunning) {
    mEngine->resume();
  }
}

void VideoEffectGraph::clearLottieOverlays() {
  if (mEngine == nullptr || mLottieOverlays.empty()) {
    return;
  }
  const bool wasRunning = mEngine->isRunning();
  if (wasRunning) {
    mEngine->pause();
  }
  disconnectChain();
  for (const auto& overlay : mLottieOverlays) {
    mEngine->detach(overlay->node());
  }
  mLottieOverlays.clear();
  rebuildChain();
  connectChain();
  if (wasRunning) {
    mEngine->resume();
  }
}

void VideoEffectGraph::setPositionUs(const int64_t positionUs) {
  for (const auto& overlay : mLottieOverlays) {
    overlay->setTimeUs(positionUs);
  }
}

EffectResult VideoEffectGraph::submit(const PixelBufferView& view) {
  if (mInput == nullptr) {
    return EffectResult::NotAttached;
  }
  return mInput->submit(view);
}

EffectResult VideoEffectGraph::submit(const VideoFrameDesc& frame) {
  PixelBufferView view;
  view.width = frame.width;
  view.height = frame.height;
  view.format = frame.format;
  view.colorSpace = frame.colorSpace;
  view.storage = PixelBufferStorageKind::Cpu;
  view.capabilities = PixelBufferCapability::CpuRead;
  view.planeCount = frame.planeCount;
  for (uint32_t index = 0; index < frame.planeCount && index < 4; ++index) {
    view.planes[index] = { frame.planes[index].data, frame.planes[index].rowBytes, frame.width,
                           frame.height };
  }
  view.bt709 = frame.bt709;
  view.fullRange = frame.fullRange;
  view.p010 = frame.p010;
  view.bt2020 = frame.bt2020;
  view.chromaCositedEven = frame.chromaCositedEven;
  view.hdr = frame.hdr;
  view.hdrTransfer = frame.hdrTransfer;
  view.hdr10PlusTargetedNits = frame.hdr10PlusTargetedNits;
  view.hdr10PlusAverageMaxRgb = frame.hdr10PlusAverageMaxRgb;
  view.hdr10PlusFractionBrightPixels = frame.hdr10PlusFractionBrightPixels;
  view.hdrMaxContentLightLevel = frame.hdrMaxContentLightLevel;
  view.hdrMaxFrameAverageLightLevel = frame.hdrMaxFrameAverageLightLevel;
  view.source = frame.source;
  view.timestamp = frame.timestamp;
  return submit(view);
}

EffectResult VideoEffectGraph::execute() {
  if (mEngine == nullptr) {
    return EffectResult::NotAttached;
  }
  return mEngine->execute();
}

EffectResult VideoEffectGraph::setScaleMode(const EffectScaleMode mode) {
  if (mLayout == nullptr) {
    return EffectResult::NotAttached;
  }
  return mLayout->setScaleMode(mode);
}

EffectResult VideoEffectGraph::setVideoRotation(const int degrees) {
  if (mLayout == nullptr) {
    return EffectResult::NotAttached;
  }
  EffectRotation rotation = EffectRotation::None;
  if (degrees == 90) {
    rotation = EffectRotation::Clockwise90;
  } else if (degrees == 180) {
    rotation = EffectRotation::Clockwise180;
  } else if (degrees == 270) {
    rotation = EffectRotation::Clockwise270;
  }
  return mLayout->setRotation(rotation);
}

EffectResult VideoEffectGraph::setVideoMirror(const bool horizontal, const bool vertical) {
  if (mLayout == nullptr) {
    return EffectResult::NotAttached;
  }
  return mLayout->setMirror(horizontal, vertical);
}

} // namespace coral
