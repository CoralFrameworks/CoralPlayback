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

#include "details/VideoLayoutPass.h"

#include <effect/EffectEngine.h>
#include <effect/EffectPassNode.h>

#include <gtest/gtest.h>

namespace coral {
namespace {

TEST(VideoLayoutPassTest, CreatesAnAttachedPass) {
  const auto engine = EffectEngine::create(EffectBackend::Default);
  ASSERT_NE(engine, nullptr);

  const auto layout = VideoLayoutPass::create(engine);
  ASSERT_NE(layout, nullptr);
  ASSERT_NE(layout->node(), nullptr);
  EXPECT_EQ(layout->node()->inputCount(), 1u);
  EXPECT_EQ(layout->scaleMode(), EffectScaleMode::Fit);
}

TEST(VideoLayoutPassTest, UpdatesScaleModeAndLetterbox) {
  const auto engine = EffectEngine::create(EffectBackend::Default);
  ASSERT_NE(engine, nullptr);

  const auto layout = VideoLayoutPass::create(engine, "test-layout");
  ASSERT_NE(layout, nullptr);

  EXPECT_EQ(layout->setScaleMode(EffectScaleMode::Crop), EffectResult::Success);
  EXPECT_EQ(layout->scaleMode(), EffectScaleMode::Crop);
  EXPECT_EQ(layout->setScaleMode(EffectScaleMode::Stretch), EffectResult::Success);
  EXPECT_EQ(layout->scaleMode(), EffectScaleMode::Stretch);
  EXPECT_EQ(layout->setLetterboxColor(EffectColor{ 1.0f, 0.0f, 0.0f, 1.0f }),
            EffectResult::Success);
}

TEST(VideoLayoutPassTest, RejectsNoEngine) {
  EXPECT_EQ(VideoLayoutPass::create(nullptr), nullptr);
}

} // namespace
} // namespace coral
