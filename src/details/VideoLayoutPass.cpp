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

#include "VideoLayoutPass.h"

#include <effect/EffectEngine.h>
#include <effect/EffectPassNode.h>
#include <effect/EffectShader.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace coral {
namespace {

/**
 * The layout body.
 *
 * `effectSourceSize()` is the input frame size and `effectTargetSize()` the engine's render
 * size. The shader computes the source rectangle that fills/letterboxes/crops the target, then
 * samples the input with it. Keeping the math on the GPU means a surface resize is a parameter
 * change, not a new frame allocation on the CPU.
 */
constexpr char kLayoutShader[] = R"(
  vec2 sourceSize = max(effectSourceSize().xy, vec2(1.0));
  vec2 targetSize = max(effectTargetSize().xy, vec2(1.0));

  int mode = int(effectParams(0).x + 0.5);
  int rotation = int(effectParams(2).x + 0.5);
  bool mirrorH = effectParams(2).y > 0.5;
  bool mirrorV = effectParams(2).z > 0.5;

  // The rotated image has its extent swapped for a quarter turn, which is what Fit/Crop must
  // measure; using the unrotated size would letterbox a portrait video into a landscape target.
  vec2 rotatedSize = (rotation == 1 || rotation == 3) ? sourceSize.yx : sourceSize;
  float scale;
  if (mode == 0) {
    scale = 1.0;
  } else if (mode == 1) {
    // Fit: preserve aspect ratio, letterbox the remainder.
    scale = min(targetSize.x / rotatedSize.x, targetSize.y / rotatedSize.y);
  } else {
    // Crop: preserve aspect ratio, fill and crop the overflow.
    scale = max(targetSize.x / rotatedSize.x, targetSize.y / rotatedSize.y);
  }

  // `rotatedUv` is in the rotated image's coordinates. Calculate the content rectangle there, then
  // inverse-rotate it to the source texture; applying rotation before Fit is what keeps the content
  // rectangle and the letterbox in the same orientation. Stretch samples the whole rotated image.
  vec2 rotatedUv = uv;
  if (mode != 0) {
    vec2 visibleSource = targetSize / max(scale, 1.0e-6);
    rotatedUv = (uv - 0.5) * visibleSource / rotatedSize + 0.5;
    if (mode == 1 &&
        (rotatedUv.x < 0.0 || rotatedUv.x > 1.0 || rotatedUv.y < 0.0 || rotatedUv.y > 1.0)) {
      return effectParams(1);
    }
  }
  if (mirrorH) rotatedUv.x = 1.0 - rotatedUv.x;
  if (mirrorV) rotatedUv.y = 1.0 - rotatedUv.y;

  vec2 sourceUv = rotatedUv;
  if (rotation == 1) {
    sourceUv = vec2(rotatedUv.y, 1.0 - rotatedUv.x);
  } else if (rotation == 2) {
    sourceUv = vec2(1.0 - rotatedUv.x, 1.0 - rotatedUv.y);
  } else if (rotation == 3) {
    sourceUv = vec2(1.0 - rotatedUv.y, rotatedUv.x);
  }
  return texture(uInput0, sourceUv);
)";

int32_t scaleModeValue(const EffectScaleMode mode) noexcept {
  switch (mode) {
    case EffectScaleMode::Stretch:
      return 0;
    case EffectScaleMode::Fit:
      return 1;
    case EffectScaleMode::Crop:
    default:
      return 2;
  }
}

} // namespace

VideoLayoutPass::VideoLayoutPass(std::shared_ptr<EffectPassNode> pass) noexcept
    : mPass(std::move(pass)) {
}

std::shared_ptr<VideoLayoutPass> VideoLayoutPass::create(
    const std::shared_ptr<EffectEngine>& engine, const std::string_view name) {
  if (engine == nullptr) {
    return nullptr;
  }

  const std::string passName = name.empty() ? "video-layout" : std::string(name);
  const std::shared_ptr<EffectPassNode> pass = engine->createEffectNode(passName);
  if (pass == nullptr) {
    return nullptr;
  }

  EffectShaderDesc shader;
  shader.name = "playback/video-layout";
  shader.fragmentSource = kLayoutShader;
  shader.parameters = {
      { "mode", EffectValueType::Int },
      { "letterbox", EffectValueType::Color },
      { "transform", EffectValueType::Vec4 },
  };
  if (pass->setShader(shader) != EffectResult::Success) {
    return nullptr;
  }

  auto layout = std::shared_ptr<VideoLayoutPass>(new VideoLayoutPass(pass));
  if (layout->setScaleMode(EffectScaleMode::Fit) != EffectResult::Success ||
      layout->setLetterboxColor(EffectColor{ 0.0f, 0.0f, 0.0f, 1.0f }) != EffectResult::Success ||
      layout->setRotation(EffectRotation::None) != EffectResult::Success ||
      layout->setMirror(false, false) != EffectResult::Success) {
    return nullptr;
  }
  return layout;
}

VideoLayoutPass::~VideoLayoutPass() = default;

EffectResult VideoLayoutPass::setScaleMode(const EffectScaleMode mode) {
  if (mPass == nullptr) {
    return EffectResult::NotAttached;
  }
  const EffectResult result = mPass->setParameter("mode", scaleModeValue(mode));
  if (result == EffectResult::Success) {
    mScaleMode = mode;
  }
  return result;
}

EffectResult VideoLayoutPass::setRotation(const EffectRotation rotation) {
  if (mPass == nullptr) {
    return EffectResult::NotAttached;
  }
  int value = 0;
  switch (rotation) {
    case EffectRotation::Clockwise90:
      value = 1;
      break;
    case EffectRotation::Clockwise180:
      value = 2;
      break;
    case EffectRotation::Clockwise270:
      value = 3;
      break;
    case EffectRotation::None:
    default:
      value = 0;
      break;
  }
  const EffectResult result = mPass->setParameter("transform",
    EffectVec4{ static_cast<float>(value), mMirrorHorizontal ? 1.0f : 0.0f,
                mMirrorVertical ? 1.0f : 0.0f, 0.0f });
  if (result == EffectResult::Success) {
    mRotation = rotation;
  }
  return result;
}

EffectResult VideoLayoutPass::setMirror(const bool horizontal, const bool vertical) {
  if (mPass == nullptr) {
    return EffectResult::NotAttached;
  }
  int rotation = 0;
  switch (mRotation) {
    case EffectRotation::Clockwise90:
      rotation = 1;
      break;
    case EffectRotation::Clockwise180:
      rotation = 2;
      break;
    case EffectRotation::Clockwise270:
      rotation = 3;
      break;
    case EffectRotation::None:
    default:
      rotation = 0;
      break;
  }
  const EffectResult result = mPass->setParameter("transform",
    EffectVec4{ static_cast<float>(rotation), horizontal ? 1.0f : 0.0f,
                vertical ? 1.0f : 0.0f, 0.0f });
  if (result == EffectResult::Success) {
    mMirrorHorizontal = horizontal;
    mMirrorVertical = vertical;
  }
  return result;
}

EffectResult VideoLayoutPass::setLetterboxColor(const EffectColor& color) {
  if (mPass == nullptr) {
    return EffectResult::NotAttached;
  }
  return mPass->setParameter("letterbox", color);
}

} // namespace coral
