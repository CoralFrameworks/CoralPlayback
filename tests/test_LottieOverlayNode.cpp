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

#include "details/LottieOverlayNode.h"

#include <effect/EffectEngine.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace coral {
namespace {

std::string readGear() {
  const std::filesystem::path path =
      std::filesystem::path(CORAL_GRAPHICS_RESOURCE_DIR) / "apitest" / "lottie" / "gear.json";
  std::ifstream file(path, std::ios::binary);
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

TEST(LottieOverlayNodeTest, PreRendersAShortLoopAndDrivesItByTime) {
  LottieOverlayConfig config;
  config.json = readGear();
  config.renderScale = 0.25f;
  config.maxPreRenderFrames = 400;
  ASSERT_FALSE(config.json.empty());

  EffectEngineConfig engineConfig;
  engineConfig.renderMode = EffectRenderMode::Manual;
  engineConfig.renderWidth = 320;
  engineConfig.renderHeight = 180;
  const auto engine = EffectEngine::create(engineConfig);
  ASSERT_NE(engine, nullptr);

  const auto overlay = LottieOverlayNode::create(engine, config);
  ASSERT_NE(overlay, nullptr);
  EXPECT_GT(overlay->frameCount(), 0u);
  EXPECT_TRUE(overlay->isEnabled());

  overlay->setTimeUs(500'000);
  overlay->setEnabled(false);
  EXPECT_FALSE(overlay->isEnabled());
  overlay->setEnabled(true);
  EXPECT_TRUE(overlay->isEnabled());
}

TEST(LottieOverlayNodeTest, RejectsInvalidJson) {
  LottieOverlayConfig config;
  config.json = "{not a lottie document}";

  EffectEngineConfig engineConfig;
  engineConfig.renderMode = EffectRenderMode::Manual;
  engineConfig.renderWidth = 320;
  engineConfig.renderHeight = 180;
  const auto engine = EffectEngine::create(engineConfig);
  ASSERT_NE(engine, nullptr);

  EXPECT_EQ(LottieOverlayNode::create(engine, config), nullptr);
}

} // namespace
} // namespace coral
