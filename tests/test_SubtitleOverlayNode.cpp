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

} // namespace
} // namespace coral
