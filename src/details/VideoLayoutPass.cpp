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
  float scale;
  if (mode == 0) {
    // Stretch: sample the whole source.
    vec2 sourceUv = uv;
    return texture(uInput0, sourceUv);
  } else if (mode == 1) {
    // Fit: preserve aspect ratio, letterbox the remainder.
    scale = min(targetSize.x / sourceSize.x, targetSize.y / sourceSize.y);
  } else {
    // Crop: preserve aspect ratio, fill and crop the overflow.
    scale = max(targetSize.x / sourceSize.x, targetSize.y / sourceSize.y);
  }

  vec2 visibleSource = targetSize / max(scale, 1.0e-6);
  vec2 sourceUv = (uv - 0.5) * visibleSource / sourceSize + 0.5;
  if (mode == 1 && (sourceUv.x < 0.0 || sourceUv.x > 1.0 || sourceUv.y < 0.0 || sourceUv.y > 1.0)) {
    return effectParams(1);
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
  };
  if (pass->setShader(shader) != EffectResult::Success) {
    return nullptr;
  }

  auto layout = std::shared_ptr<VideoLayoutPass>(new VideoLayoutPass(pass));
  if (layout->setScaleMode(EffectScaleMode::Fit) != EffectResult::Success ||
      layout->setLetterboxColor(EffectColor{ 0.0f, 0.0f, 0.0f, 1.0f }) != EffectResult::Success) {
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

EffectResult VideoLayoutPass::setLetterboxColor(const EffectColor& color) {
  if (mPass == nullptr) {
    return EffectResult::NotAttached;
  }
  return mPass->setParameter("letterbox", color);
}

} // namespace coral
