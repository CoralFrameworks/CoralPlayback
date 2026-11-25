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

#include "SubtitleOverlayNode.h"

#include <effect/EffectEngine.h>
#include <effect/EffectPassNode.h>
#include <effect/EffectShader.h>
#include <effect/EffectTexture.h>

#include <graphics/Bitmap.h>
#include <graphics/Pixmap.h>

#include <utility>

namespace coral {
namespace {

constexpr char kSubtitleShader[] = R"(
  vec4 base = texture(uInput0, uv);
  // `vDevice` is top-left based on every backend; image rows are uploaded top-first.
  vec4 sub = texture(uSubtitle, vDevice);
  return vec4(sub.rgb + base.rgb * (1.0 - sub.a), 1.0);
)";

} // namespace

SubtitleOverlayNode::SubtitleOverlayNode(std::shared_ptr<EffectPassNode> pass)
    : mPass(std::move(pass)) {
  // Android's font scan can otherwise select a bitmap-only face (emoji fonts). Roboto is the
  // platform's Latin body face and is also resolved by the desktop FontManager when available.
  if (mStyle.fontFamily.empty()) {
    mStyle.fontFamily = "Roboto";
  }
}

std::shared_ptr<SubtitleOverlayNode> SubtitleOverlayNode::create(
    const std::shared_ptr<EffectEngine>& engine) {
  if (engine == nullptr) {
    return nullptr;
  }
  const std::shared_ptr<EffectPassNode> pass = engine->createEffectNode("subtitle-overlay");
  if (pass == nullptr) {
    return nullptr;
  }

  EffectShaderDesc shader;
  shader.name = "playback/subtitle-overlay";
  shader.fragmentSource = kSubtitleShader;
  shader.inputCount = 1;
  // The program generator declares only the textures that are already bound. Bind the sampler to
  // a transparent texel first so the first compilation sees `uSubtitle`; the cue image replaces it
  // on the first non-empty group.
  if (pass->setTexture("uSubtitle", TextureSource::solid(EffectColor{ 0.0f, 0.0f, 0.0f, 0.0f })) !=
      EffectResult::Success) {
    return nullptr;
  }
  if (pass->setShader(shader) != EffectResult::Success) {
    return nullptr;
  }
  pass->setEnabled(false);

  return std::shared_ptr<SubtitleOverlayNode>(new SubtitleOverlayNode(pass));
}

SubtitleOverlayNode::~SubtitleOverlayNode() = default;

bool SubtitleOverlayNode::isEnabled() const noexcept {
  return mPass != nullptr && mPass->isEnabled();
}

void SubtitleOverlayNode::setOutputSize(const uint32_t width, const uint32_t height) {
  if (mWidth == width && mHeight == height) {
    return;
  }
  mWidth = width;
  mHeight = height;
  if (mEnabled && !mCues.isEmpty()) {
    renderCues();
  }
}

void SubtitleOverlayNode::setEnabled(const bool enabled) {
  mEnabled = enabled;
  if (mPass == nullptr) {
    return;
  }
  if (!enabled) {
    mPass->setEnabled(false);
    return;
  }
  if (mCues.isEmpty() || mWidth == 0 || mHeight == 0) {
    mPass->setEnabled(false);
    return;
  }
  renderCues();
  mPass->setEnabled(true);
}

void SubtitleOverlayNode::setCueGroup(const AVCueGroup& cues) {
  if (mCues == cues) {
    return;
  }
  mCues = cues;
  if (!mEnabled || mCues.isEmpty() || mWidth == 0 || mHeight == 0) {
    if (mPass != nullptr) {
      mPass->setEnabled(false);
    }
    return;
  }
  renderCues();
  mPass->setEnabled(true);
}

void SubtitleOverlayNode::renderCues() {
  if (mPass == nullptr || mWidth == 0 || mHeight == 0) {
    return;
  }
  const std::shared_ptr<graphics::Bitmap> bitmap =
      mRenderer.render(mCues, mWidth, mHeight, mStyle, mOptions);
  if (bitmap == nullptr || bitmap->isEmpty()) {
    mPass->setEnabled(false);
    return;
  }

  const graphics::Pixmap pixels = bitmap->makePixmap();
  const auto* const data = static_cast<const uint8_t*>(pixels.addr(0, 0));
  if (data == nullptr) {
    mPass->setEnabled(false);
    return;
  }

  EffectImageDesc desc;
  desc.width = mWidth;
  desc.height = mHeight;
  desc.format = EffectPixelFormat::RGBA8;
  desc.colorSpace = EffectColorSpace::SRGB;
  desc.alphaMode = EffectAlphaMode::Premultiplied;
  desc.rowBytes = static_cast<uint32_t>(pixels.rowBytes());
  desc.pixels = data;
  desc.byteSize = static_cast<size_t>(desc.rowBytes) * mHeight;
  const std::shared_ptr<const EffectImage> image = EffectImage::create(desc);
  if (image == nullptr) {
    mPass->setEnabled(false);
    return;
  }
  mPass->setTexture("uSubtitle", TextureSource::fromImage(image));
}

} // namespace coral
