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

#include "LottieOverlayNode.h"

#include <effect/EffectEngine.h>
#include <effect/EffectPassNode.h>
#include <effect/EffectShader.h>
#include <effect/EffectTexture.h>

#include <lottie/Lottie.h>

#include <graphics/Pixmap.h>
#include <graphics/Surface.h>

#include <geometry/Color.h>
#include <geometry/Rect.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace coral {
namespace {

constexpr char kLottieShader[] = R"(
  vec4 base = texture(uInput0, uv);
  vec4 rect = effectParams(0);
  float alpha = effectParams(1).x;

  vec2 local = (vDevice - rect.xy) / max(rect.zw, vec2(1.0e-6));
  if (local.x < 0.0 || local.x > 1.0 || local.y < 0.0 || local.y > 1.0) {
    return base;
  }

  vec4 over = texture(uOverlay, local) * alpha;
  return vec4(over.rgb + base.rgb * (1.0 - over.a), 1.0);
)";

uint32_t rasterSize(const graphics::Scalar value, const float scale) {
  const float scaled = static_cast<float>(value) * scale;
  return scaled <= 1.0f ? 1u : static_cast<uint32_t>(std::lround(scaled));
}

} // namespace

LottieOverlayNode::LottieOverlayNode(std::shared_ptr<EffectPassNode> pass,
                                     const uint32_t frameCount,
                                     const int64_t frameDurationUs,
                                     const int64_t startTimeUs,
                                     const int zOrder,
                                     LottieOverlayConfig config,
                                     const float aspect) noexcept
    : mPass(std::move(pass)),
      mFrameCount(frameCount),
      mFrameDurationUs(frameDurationUs),
      mStartTimeUs(startTimeUs),
      mZOrder(zOrder),
      mConfig(std::move(config)),
      mAspect(aspect) {
}

std::shared_ptr<LottieOverlayNode> LottieOverlayNode::create(
    const std::shared_ptr<EffectEngine>& engine, const LottieOverlayConfig& config) {
  if (engine == nullptr || config.json.empty()) {
    return nullptr;
  }

  std::string error;
  std::optional<graphics::Lottie> parsed = graphics::Lottie::parse(config.json, &error);
  if (!parsed.has_value() || parsed->isEmpty()) {
    return nullptr;
  }
  graphics::Lottie lottie = std::move(*parsed);

  const float frameRate = static_cast<float>(lottie.frameRate());
  const float duration = static_cast<float>(lottie.duration());
  if (frameRate <= 0.0f || duration <= 0.0f) {
    return nullptr;
  }
  const uint32_t frameCount =
      static_cast<uint32_t>(std::ceil(duration * frameRate));
  if (frameCount == 0 || frameCount > config.maxPreRenderFrames) {
    return nullptr;
  }

  const uint32_t width = rasterSize(lottie.width(), config.renderScale);
  const uint32_t height = rasterSize(lottie.height(), config.renderScale);
  if (width == 0 || height == 0) {
    return nullptr;
  }
  const size_t bytes = static_cast<size_t>(width) * height * 4u * frameCount;
  if (bytes > config.maxPreRenderBytes) {
    return nullptr;
  }

  const std::unique_ptr<graphics::Surface> surface = graphics::Surface::makeRaster(
      static_cast<int>(width), static_cast<int>(height));
  if (surface == nullptr) {
    return nullptr;
  }
  const std::shared_ptr<graphics::Canvas> canvas = surface->getCanvas();
  if (canvas == nullptr) {
    return nullptr;
  }

  std::vector<TextureSource> frames;
  frames.reserve(frameCount);
  const graphics::Rect bounds =
      graphics::Rect::MakeWH(static_cast<graphics::Scalar>(width),
                              static_cast<graphics::Scalar>(height));
  for (uint32_t index = 0; index < frameCount; ++index) {
    canvas->clear(graphics::Color::BlackTransparent());
    lottie.render(*canvas, bounds, static_cast<graphics::Scalar>(index) / frameRate);
    const graphics::Pixmap pixels = surface->peekPixels();
    const auto* const data = static_cast<const uint8_t*>(pixels.addr(0, 0));
    if (data == nullptr) {
      return nullptr;
    }

    EffectImageDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = EffectPixelFormat::RGBA8;
    desc.colorSpace = EffectColorSpace::SRGB;
    desc.alphaMode = EffectAlphaMode::Premultiplied;
    desc.rowBytes = static_cast<uint32_t>(pixels.rowBytes());
    desc.pixels = data;
    desc.byteSize = static_cast<size_t>(desc.rowBytes) * height;
    const std::shared_ptr<const EffectImage> image = EffectImage::create(desc);
    if (image == nullptr) {
      return nullptr;
    }
    frames.push_back(TextureSource::fromImage(image));
  }

  const std::shared_ptr<EffectPassNode> pass = engine->createEffectNode(
      config.name.empty() ? "lottie-overlay" : config.name);
  if (pass == nullptr) {
    return nullptr;
  }

  const int64_t frameDurationUs =
      std::max<int64_t>(1, static_cast<int64_t>(std::llround(
          1'000'000.0 / (static_cast<double>(frameRate) * std::max(config.speed, 0.01f)))));
  EffectTextureSequenceDesc sequence;
  sequence.frames = std::move(frames);
  sequence.frameDuration = RenderTime::fromTicks(frameDurationUs, 1'000'000);
  sequence.startTime = RenderTime::fromTicks(0, 1'000'000);
  sequence.loop = config.loop;

  EffectShaderDesc shader;
  shader.name = "playback/lottie-overlay";
  shader.fragmentSource = kLottieShader;
  shader.inputCount = 1;
  shader.parameters = {
      { "uRect", EffectValueType::Vec4 },
      { "uAlpha", EffectValueType::Float },
  };
  // Bind the sequence before the shader is compiled: the program generator declares only the
  // textures that already exist in the node's snapshot.
  if (pass->setTextureSequence("uOverlay", std::move(sequence)) != EffectResult::Success) {
    return nullptr;
  }
  if (pass->setShader(shader) != EffectResult::Success) {
    return nullptr;
  }

  const float aspect = static_cast<float>(lottie.width()) /
                       std::max(1.0f, static_cast<float>(lottie.height()));
  const std::shared_ptr<LottieOverlayNode> node(
      new LottieOverlayNode(pass, frameCount, frameDurationUs, config.startTimeUs, config.zOrder,
                            config, aspect));
  node->updateLayout();
  pass->setEnabled(true);
  return node;
}

LottieOverlayNode::~LottieOverlayNode() = default;

void LottieOverlayNode::setTimeUs(const int64_t timeUs) {
  if (mPass == nullptr) {
    return;
  }
  mPass->setSequenceTime(
      "uOverlay", RenderTime::fromTicks(std::max<int64_t>(0, timeUs - mStartTimeUs), 1'000'000));
}

void LottieOverlayNode::setOutputSize(const uint32_t width, const uint32_t height) {
  if (width == 0 || height == 0 || (mOutputWidth == width && mOutputHeight == height)) {
    return;
  }
  mOutputWidth = width;
  mOutputHeight = height;
  updateLayout();
}

void LottieOverlayNode::updateLayout() {
  if (mPass == nullptr) {
    return;
  }
  const float targetWidth = static_cast<float>(std::max<uint32_t>(mOutputWidth, 1u));
  const float targetHeight = static_cast<float>(std::max<uint32_t>(mOutputHeight, 1u));

  float width = mConfig.widthPx;
  float height = mConfig.heightPx;
  if (width > 0.0f && height <= 0.0f) {
    height = width / std::max(mAspect, 1.0e-6f);
  } else if (height > 0.0f && width <= 0.0f) {
    width = height * mAspect;
  } else if (width <= 0.0f && height <= 0.0f) {
    height = targetHeight * std::max(mConfig.scale, 0.0f);
    width = height * mAspect;
  }

  float x = 0.0f;
  float y = 0.0f;
  switch (mConfig.positionMode) {
    case LottiePositionMode::TopLeftPixels:
      x = mConfig.xPx;
      y = mConfig.yPx;
      break;
    case LottiePositionMode::CenterPixels:
      x = mConfig.xPx - width * 0.5f;
      y = mConfig.yPx - height * 0.5f;
      break;
    case LottiePositionMode::Anchored: {
      const float left = mConfig.marginX;
      const float right = targetWidth - width - mConfig.marginX;
      const float top = mConfig.marginY;
      const float bottom = targetHeight - height - mConfig.marginY;
      const float centerX = (targetWidth - width) * 0.5f + mConfig.marginX;
      const float centerY = (targetHeight - height) * 0.5f + mConfig.marginY;
      switch (mConfig.anchor) {
        case LottieAnchor::TopLeft:
          x = left;
          y = top;
          break;
        case LottieAnchor::TopCenter:
          x = (targetWidth - width) * 0.5f;
          y = top;
          break;
        case LottieAnchor::TopRight:
          x = right;
          y = top;
          break;
        case LottieAnchor::CenterLeft:
          x = left;
          y = (targetHeight - height) * 0.5f;
          break;
        case LottieAnchor::Center:
          x = centerX;
          y = centerY;
          break;
        case LottieAnchor::CenterRight:
          x = right;
          y = (targetHeight - height) * 0.5f;
          break;
        case LottieAnchor::BottomLeft:
          x = left;
          y = bottom;
          break;
        case LottieAnchor::BottomCenter:
          x = (targetWidth - width) * 0.5f;
          y = bottom;
          break;
        case LottieAnchor::BottomRight:
        default:
          x = right;
          y = bottom;
          break;
      }
      break;
    }
    case LottiePositionMode::NormalizedCenter:
    default:
      x = targetWidth * mConfig.centerX - width * 0.5f;
      y = targetHeight * mConfig.centerY - height * 0.5f;
      break;
  }

  const EffectVec4 rect{ x / targetWidth, y / targetHeight, width / targetWidth,
                         height / targetHeight };
  mPass->setParameter("uRect", rect);
  mPass->setParameter("uAlpha", mConfig.alpha);
}

void LottieOverlayNode::setEnabled(const bool enabled) {
  mEnabled = enabled;
  if (mPass != nullptr) {
    mPass->setEnabled(enabled);
  }
}

bool LottieOverlayNode::isEnabled() const noexcept {
  return mEnabled && mPass != nullptr && mPass->isEnabled();
}

} // namespace coral
