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

#include "details/VideoEffectGraph.h"

#include <effect/EffectEngine.h>
#include <effect/NullOutputNode.h>
#include <effect/VideoFrameInputNode.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace coral {
namespace {

std::vector<uint8_t> rgbaFrame(const int width, const int height) {
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);
  for (size_t index = 0; index < pixels.size(); index += 4) {
    pixels[index + 0] = 32;
    pixels[index + 1] = 64;
    pixels[index + 2] = 128;
    pixels[index + 3] = 255;
  }
  return pixels;
}

std::string readGear() {
  const std::filesystem::path path =
      std::filesystem::path(CORAL_GRAPHICS_RESOURCE_DIR) / "apitest" / "lottie" / "gear.json";
  std::ifstream file(path, std::ios::binary);
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

TEST(VideoEffectGraphTest, BuildsInputLayoutAndOutput) {
  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = 64;
  config.renderHeight = 64;
  config.timeBase = 1'000'000;

  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto input = VideoFrameInputNode::create(64, 64);
  const auto output = NullOutputNode::create();
  ASSERT_NE(input, nullptr);
  ASSERT_NE(output, nullptr);

  const auto graph = VideoEffectGraph::create(engine, input, output);
  ASSERT_NE(graph, nullptr);
  EXPECT_TRUE(graph->isBuilt());
  EXPECT_EQ(graph->engine(), engine);
  EXPECT_EQ(graph->input(), input);
  EXPECT_EQ(graph->output(), output);
  ASSERT_NE(graph->layout(), nullptr);

  EXPECT_EQ(graph->setScaleMode(EffectScaleMode::Crop), EffectResult::Success);

  if (graph->start() != EffectResult::Success) {
    GTEST_SKIP() << "no GPU device on this host: " << engine->diagnostics();
  }

  const auto pixels = rgbaFrame(64, 64);
  PixelBufferView view;
  view.width = 64;
  view.height = 64;
  view.format = EffectPixelFormat::RGBA8;
  view.storage = PixelBufferStorageKind::Cpu;
  view.capabilities = PixelBufferCapability::CpuRead;
  view.planes[0].data = pixels.data();
  view.planes[0].rowBytes = 64u * 4u;
  view.planeCount = 1;
  view.timestamp = RenderTime::fromTicks(0, 1'000'000);

  EXPECT_EQ(graph->submit(view), EffectResult::Success);
  EXPECT_EQ(graph->execute(), EffectResult::Success);
  EXPECT_GT(engine->stats().frameCount, 0u);

  EXPECT_EQ(graph->stop(), EffectResult::Success);
}

TEST(VideoEffectGraphTest, ReplacesItsOutputWithoutRestarting) {
  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = 64;
  config.renderHeight = 64;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto input = VideoFrameInputNode::create(64, 64);
  const auto first = NullOutputNode::create();
  const auto second = NullOutputNode::create();
  ASSERT_NE(input, nullptr);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);

  const auto graph = VideoEffectGraph::create(engine, input, first);
  ASSERT_NE(graph, nullptr);
  EXPECT_EQ(graph->setOutput(second), EffectResult::Success);
  EXPECT_EQ(graph->output(), second);
  EXPECT_EQ(graph->setRenderSize(128, 72), EffectResult::Success);
}

TEST(VideoEffectGraphTest, AddsAndClearsLottieOverlays) {
  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = 320;
  config.renderHeight = 180;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto input = VideoFrameInputNode::create(320, 180);
  const auto output = NullOutputNode::create();
  ASSERT_NE(input, nullptr);
  ASSERT_NE(output, nullptr);
  const auto graph = VideoEffectGraph::create(engine, input, output);
  ASSERT_NE(graph, nullptr);

  LottieOverlayConfig overlayConfig;
  overlayConfig.json = readGear();
  overlayConfig.renderScale = 0.25f;
  overlayConfig.maxPreRenderFrames = 400;
  ASSERT_FALSE(overlayConfig.json.empty());

  const auto overlay = graph->addLottieOverlay(overlayConfig);
  ASSERT_NE(overlay, nullptr);
  EXPECT_EQ(graph->lottieOverlays().size(), 1u);

  graph->setPositionUs(500'000);
  graph->clearLottieOverlays();
  EXPECT_TRUE(graph->lottieOverlays().empty());
}

TEST(VideoEffectGraphTest, OrdersLottieOverlaysByZOrder) {
  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = 320;
  config.renderHeight = 180;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto graph = VideoEffectGraph::create(
      engine, VideoFrameInputNode::create(320, 180), NullOutputNode::create());
  ASSERT_NE(graph, nullptr);

  LottieOverlayConfig top;
  top.json = readGear();
  top.renderScale = 0.1f;
  top.maxPreRenderFrames = 400;
  top.zOrder = 10;
  LottieOverlayConfig bottom = top;
  bottom.zOrder = 0;

  ASSERT_NE(graph->addLottieOverlay(top), nullptr);
  ASSERT_NE(graph->addLottieOverlay(bottom), nullptr);
  ASSERT_EQ(graph->lottieOverlays().size(), 2u);
  EXPECT_EQ(graph->lottieOverlays()[0]->zOrder(), 0);
  EXPECT_EQ(graph->lottieOverlays()[1]->zOrder(), 10);
}

TEST(VideoEffectGraphTest, RecreatesPreparedOutputsAtRuntime) {
  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = 64;
  config.renderHeight = 64;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto graph = VideoEffectGraph::create(
      engine, VideoFrameInputNode::create(64, 64), NullOutputNode::create());
  ASSERT_NE(graph, nullptr);
  if (graph->start() != EffectResult::Success) {
    GTEST_SKIP() << "no GPU device on this host: " << engine->diagnostics();
  }
  EXPECT_EQ(engine->prepareOutputs(), EffectResult::Success);
  EXPECT_EQ(graph->stop(), EffectResult::Success);
}

TEST(VideoEffectGraphTest, RejectsMissingEndpoints) {
  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = 64;
  config.renderHeight = 64;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);
  EXPECT_EQ(VideoEffectGraph::create(nullptr, nullptr, nullptr), nullptr);
  EXPECT_EQ(VideoEffectGraph::create(engine, nullptr, nullptr), nullptr);
}

} // namespace
} // namespace coral
