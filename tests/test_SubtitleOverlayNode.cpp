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

#include "details/SubtitleOverlayNode.h"

#include <effect/EffectEngine.h>
#include <effect/EffectPassNode.h>
#include <effect/ReadbackOutputNode.h>
#include <effect/VideoFrameInputNode.h>
#include <subtitle/SubtitleRenderer.h>

#include <graphics/Bitmap.h>
#include <graphics/Pixmap.h>

#include <gtest/gtest.h>

namespace coral {
namespace {

TEST(SubtitleOverlayNodeTest, EnablesForCuesAndDisablesForAnEmptyGroup) {
  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = 320;
  config.renderHeight = 180;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto overlay = SubtitleOverlayNode::create(engine);
  ASSERT_NE(overlay, nullptr);
  overlay->setOutputSize(320, 180);

  overlay->setEnabled(true);
  EXPECT_FALSE(overlay->isEnabled());

  AVCue cue;
  cue.text = "Hello";
  const AVCueGroup cues(std::vector<AVCue>{ cue });
  overlay->setCueGroup(cues);
  EXPECT_TRUE(overlay->isEnabled());

  overlay->setCueGroup(AVCueGroup());
  EXPECT_FALSE(overlay->isEnabled());
}

TEST(SubtitleOverlayNodeTest, RendersVisibleGlyphPixels) {
  SubtitleRenderer renderer;
  AVCue cue;
  cue.text = "Hello";
  SubtitleStyle style;
  TextDisplayOptions options;
  const auto bitmap = renderer.render(AVCueGroup(std::vector<AVCue>{ cue }), 320, 180, style, options);
  ASSERT_NE(bitmap, nullptr);
  const auto pixels = bitmap->makePixmap();
  const auto* data = static_cast<const uint8_t*>(pixels.addr(0, 0));
  ASSERT_NE(data, nullptr);
  size_t opaque = 0;
  for (int y = 0; y < bitmap->height(); ++y) {
    for (int x = 0; x < bitmap->width(); ++x) {
      if (data[y * pixels.rowBytes() + x * 4 + 3] != 0) ++opaque;
    }
  }
  EXPECT_GT(opaque, 0u);
}

TEST(SubtitleOverlayNodeTest, CompositesBitmapCuePixelsIntoTheOutput) {
  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = 4;
  config.renderHeight = 4;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto input = VideoFrameInputNode::create(4, 4);
  const auto output = ReadbackOutputNode::create();
  ASSERT_NE(input, nullptr);
  ASSERT_NE(output, nullptr);
  const auto overlay = SubtitleOverlayNode::create(engine);
  ASSERT_NE(overlay, nullptr);
  // SubtitleOverlayNode::create already attaches its pass; the input and output are attached here.
  ASSERT_EQ(engine->attach(input), EffectResult::Success);
  ASSERT_EQ(engine->attach(output), EffectResult::Success);
  ASSERT_EQ(engine->connect(input, overlay->node()), EffectResult::Success);
  ASSERT_EQ(engine->connect(overlay->node(), output), EffectResult::Success);
  ASSERT_EQ(engine->setOutput(output), EffectResult::Success);
  overlay->setOutputSize(4, 4);
  overlay->setEnabled(true);

  // A full-frame green bitmap cue; the base is black, so every output pixel must become green.
  AVCue cue;
  cue.bitmap.width = 2;
  cue.bitmap.height = 2;
  cue.bitmap.format = PixelFormat::kFormatRGBA;
  cue.bitmap.rowBytes = 2 * 4;
  cue.bitmap.pixels.assign(2 * 2 * 4, 0);
  for (size_t i = 0; i < 4; ++i) {
    cue.bitmap.pixels[i * 4 + 1] = 255;
    cue.bitmap.pixels[i * 4 + 3] = 255;
  }
  cue.position = 0.0f;
  cue.line = 0.0f;
  cue.size = 1.0f;
  cue.bitmapHeight = 1.0f;
  cue.positionAnchor = AVCue::AnchorType::kStart;
  cue.lineAnchor = AVCue::AnchorType::kStart;
  overlay->setCueGroup(AVCueGroup(std::vector<AVCue>{cue}));
  ASSERT_TRUE(overlay->isEnabled());

  std::vector<uint8_t> black(4u * 4u * 4u, 255u);
  for (size_t i = 0; i < 4u * 4u; ++i) {
    black[i * 4 + 0] = 0;
    black[i * 4 + 1] = 0;
    black[i * 4 + 2] = 0;
  }
  VideoFrameDesc frame;
  frame.width = 4;
  frame.height = 4;
  frame.format = EffectPixelFormat::RGBA8;
  frame.planeCount = 1;
  frame.planes[0].data = black.data();
  frame.planes[0].rowBytes = 4 * 4;
  if (engine->start() != EffectResult::Success) {
    GTEST_SKIP() << "no GPU device on this host: " << engine->diagnostics();
  }
  ASSERT_EQ(input->submit(frame), EffectResult::Success);
  ASSERT_EQ(engine->execute(), EffectResult::Success);
  ASSERT_EQ(engine->flushAndWait(), EffectResult::Success);

  EffectFrame metadata;
  std::vector<uint8_t> readback;
  ASSERT_TRUE(output->waitForFrame(&metadata, &readback));
  ASSERT_EQ(readback.size(), 4u * 4u * 4u);
  for (size_t i = 0; i < 4u * 4u; ++i) {
    EXPECT_GE(readback[i * 4 + 1], 245u) << "pixel " << i << " should carry the green subtitle";
    EXPECT_GE(readback[i * 4 + 3], 245u);
  }
  engine->stop();
}

} // namespace
} // namespace coral
