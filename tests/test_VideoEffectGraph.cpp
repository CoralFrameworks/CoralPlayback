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
#include <effect/ReadbackOutputNode.h>
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

TEST(VideoEffectGraphTest, RotatesAndMirrorsTheLayout) {
  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = 4;
  config.renderHeight = 4;
  config.timeBase = 1'000'000;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto input = VideoFrameInputNode::create(4, 4);
  const auto output = ReadbackOutputNode::create();
  ASSERT_NE(input, nullptr);
  ASSERT_NE(output, nullptr);
  const auto graph = VideoEffectGraph::create(engine, input, output);
  ASSERT_NE(graph, nullptr);
  ASSERT_EQ(graph->setVideoRotation(90), EffectResult::Success);
  if (graph->start() != EffectResult::Success) {
    GTEST_SKIP() << "no GPU device on this host: " << engine->diagnostics();
  }

  // Four quadrants: top-left red, top-right green, bottom-left blue, bottom-right white.
  std::vector<uint8_t> pixels(4u * 4u * 4u, 255u);
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      const size_t index = (static_cast<size_t>(y) * 4u + static_cast<size_t>(x)) * 4u;
      if (y < 2 && x < 2) {
        pixels[index + 0] = 255; pixels[index + 1] = 0; pixels[index + 2] = 0;
      } else if (y < 2) {
        pixels[index + 0] = 0; pixels[index + 1] = 255; pixels[index + 2] = 0;
      } else if (x < 2) {
        pixels[index + 0] = 0; pixels[index + 1] = 0; pixels[index + 2] = 255;
      } else {
        pixels[index + 0] = 255; pixels[index + 1] = 255; pixels[index + 2] = 255;
      }
    }
  }
  VideoFrameDesc frame;
  frame.width = 4;
  frame.height = 4;
  frame.format = EffectPixelFormat::RGBA8;
  frame.planeCount = 1;
  frame.planes[0].data = pixels.data();
  frame.planes[0].rowBytes = 4 * 4;
  ASSERT_EQ(input->submit(frame), EffectResult::Success);
  ASSERT_EQ(graph->execute(), EffectResult::Success);
  ASSERT_EQ(engine->flushAndWait(), EffectResult::Success);

  EffectFrame metadata;
  std::vector<uint8_t> readback;
  ASSERT_TRUE(output->waitForFrame(&metadata, &readback));
  auto pixel = [&](const int x, const int y) -> const uint8_t* {
    return readback.data() + (static_cast<size_t>(y) * 4u + static_cast<size_t>(x)) * 4u;
  };
  // A clockwise quarter turn: the original bottom-left is now top-left, the original top-left is
  // top-right, and the original bottom-right is bottom-left.
  EXPECT_GE(pixel(0, 0)[2], 245u) << "top-left should be the original bottom-left (blue)";
  EXPECT_GE(pixel(3, 0)[0], 245u) << "top-right should be the original top-left (red)";
  EXPECT_GE(pixel(0, 3)[0], 245u) << "bottom-left should be the original bottom-right (white)";
  EXPECT_GE(pixel(0, 3)[1], 245u);
  EXPECT_GE(pixel(0, 3)[2], 245u);
  EXPECT_GE(pixel(3, 3)[1], 245u) << "bottom-right should be the original top-right (green)";

  graph->setVideoMirror(true, false);
  ASSERT_EQ(input->submit(frame), EffectResult::Success);
  ASSERT_EQ(graph->execute(), EffectResult::Success);
  ASSERT_EQ(engine->flushAndWait(), EffectResult::Success);
  ASSERT_TRUE(output->waitForFrame(&metadata, &readback));
  EXPECT_GE(pixel(0, 0)[0], 245u) << "horizontal mirror should move the red quadrant back left";
  EXPECT_LE(pixel(0, 0)[2], 8u);

  // A counter-clockwise quarter turn (270 clockwise) moves the original top-right to top-left.
  graph->setVideoMirror(false, false);
  ASSERT_EQ(graph->setVideoRotation(270), EffectResult::Success);
  ASSERT_EQ(input->submit(frame), EffectResult::Success);
  ASSERT_EQ(graph->execute(), EffectResult::Success);
  ASSERT_EQ(engine->flushAndWait(), EffectResult::Success);
  ASSERT_TRUE(output->waitForFrame(&metadata, &readback));
  EXPECT_GE(pixel(0, 0)[1], 245u) << "270 top-left should be the original top-right (green)";
  EXPECT_GE(pixel(3, 0)[0], 245u) << "270 top-right should be the original bottom-right (white)";
  EXPECT_GE(pixel(3, 0)[1], 245u);
  EXPECT_GE(pixel(3, 0)[2], 245u);
  EXPECT_GE(pixel(0, 3)[0], 245u) << "270 bottom-left should be the original top-left (red)";
  EXPECT_GE(pixel(3, 3)[2], 245u) << "270 bottom-right should be the original bottom-left (blue)";

  // A 4x2 source rotated to 2x4 must letterbox horizontally in a 4x4 target; using the unrotated
  // source size would instead stretch it across the target.
  std::vector<uint8_t> wide(4u * 2u * 4u, 255u);
  VideoFrameDesc wideFrame;
  wideFrame.width = 4;
  wideFrame.height = 2;
  wideFrame.format = EffectPixelFormat::RGBA8;
  wideFrame.planeCount = 1;
  wideFrame.planes[0].data = wide.data();
  wideFrame.planes[0].rowBytes = 4 * 4;
  graph->setVideoRotation(90);
  graph->setVideoMirror(false, false);
  ASSERT_EQ(input->submit(wideFrame), EffectResult::Success);
  ASSERT_EQ(graph->execute(), EffectResult::Success);
  ASSERT_EQ(engine->flushAndWait(), EffectResult::Success);
  ASSERT_TRUE(output->waitForFrame(&metadata, &readback));
  EXPECT_LE(pixel(0, 0)[0], 8u) << "the left column should be letterbox";
  EXPECT_GE(pixel(1, 0)[0], 245u) << "the rotated content should occupy the centre columns";
  EXPECT_LE(pixel(3, 0)[0], 8u) << "the right column should be letterbox";
  graph->stop();
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
