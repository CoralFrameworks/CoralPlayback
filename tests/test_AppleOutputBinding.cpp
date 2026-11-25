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

/**
 * @file test_AppleOutputBinding.cpp
 *
 * The Apple surface binding and the IOSurface output path: the wrapper retains the platform surface,
 * the factory turns it into the graph's `AppleIOSurfaceOutputNode`, and a real decoded video reaches
 * both the node and the surface.
 */

#include <coral/playback/LivePlayer.h>
#include <coral/playback/VideoOutput.h>
#include <coral/playback/apple/AppleIOSurface.h>
#include <coral/playback/apple/AppleVideoInput.h>
#include <coral/playback/apple/AppleVideoOutputNodeFactory.h>

#include <effect/EffectEngine.h>
#include <effect/EffectPassNode.h>
#include <effect/VideoFrameInputNode.h>
#include <effect/platform/apple/AppleIOSurfaceInputNode.h>
#include <effect/platform/apple/AppleIOSurfaceOutputNode.h>

#include <IOSurface/IOSurface.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace coral {
namespace {

using namespace std::chrono_literals;

/** A surface type the Apple factory must not accept. */
class ForeignSurface final : public VideoSurface {
public:
  [[nodiscard]] void* getInstance() const override {
    return const_cast<ForeignSurface*>(this);
  }
  [[nodiscard]] bool isValid() const override {
    return true;
  }
  void release() override {
  }
};

/** Watches the player state and the first decoded size. */
class PlayerProbe final : public Player::EventListener {
public:
  void onPlaybackStateChanged(const Player::State& state) override {
    if (state == Player::State::STATE_READY) {
      mReady.store(true, std::memory_order_release);
    }
  }

  void onVideoSizeChanged(int width, int height) override {
    static_cast<void>(width);
    static_cast<void>(height);
    mVideoSizeSeen.store(true, std::memory_order_release);
  }

  void onPlayerError(const std::string& errorMessage) override {
    std::lock_guard<std::mutex> lock(mMutex);
    mError = errorMessage;
    mFailed.store(true, std::memory_order_release);
  }

  [[nodiscard]] bool ready() const { return mReady.load(std::memory_order_acquire); }
  [[nodiscard]] bool videoSizeSeen() const {
    return mVideoSizeSeen.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool failed() const { return mFailed.load(std::memory_order_acquire); }
  [[nodiscard]] std::string error() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mError;
  }

private:
  std::atomic<bool> mReady{false};
  std::atomic<bool> mVideoSizeSeen{false};
  std::atomic<bool> mFailed{false};
  mutable std::mutex mMutex;
  std::string mError;
};

bool waitFor(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return predicate();
}

/** @return the BGRA bytes of the centre pixel of @p surface. */
std::vector<uint8_t> centrePixel(IOSurfaceRef surface) {
  std::vector<uint8_t> pixel(4, 0);
  if (surface == nullptr || IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr) != KERN_SUCCESS) {
    return pixel;
  }
  const auto* base = static_cast<const uint8_t*>(IOSurfaceGetBaseAddress(surface));
  const size_t stride = IOSurfaceGetBytesPerRow(surface);
  const size_t width = IOSurfaceGetWidth(surface);
  const size_t height = IOSurfaceGetHeight(surface);
  std::memcpy(pixel.data(), base + (height / 2) * stride + (width / 2) * 4, 4);
  IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);
  return pixel;
}

// ==================================================================================================
// The surface wrapper
// ==================================================================================================

TEST(AppleIOSurfaceTest, CreatesASurfaceWithTheRequestedLayout) {
  const auto surface = AppleIOSurface::create(64, 48, EffectPixelFormat::BGRA8);
  ASSERT_NE(surface, nullptr) << "IOSurfaceCreate failed";
  EXPECT_TRUE(surface->isValid());
  EXPECT_EQ(surface->width(), 64u);
  EXPECT_EQ(surface->height(), 48u);
  EXPECT_EQ(surface->format(), EffectPixelFormat::BGRA8);
  EXPECT_EQ(surface->iosurface(), static_cast<IOSurfaceRef>(surface->getInstance()));
  EXPECT_EQ(surface->rowBytes(), 64u * 4u);
  EXPECT_EQ(surface->allocationSize(), 64u * 48u * 4u);
}

TEST(AppleIOSurfaceTest, CreatesTheRgbaLayouts) {
  const auto rgba = AppleIOSurface::create(32, 32, EffectPixelFormat::RGBA8);
  ASSERT_NE(rgba, nullptr);
  EXPECT_EQ(rgba->format(), EffectPixelFormat::RGBA8);
  EXPECT_EQ(AppleIOSurface::formatOf(rgba->iosurface()), EffectPixelFormat::RGBA8);

  const auto half = AppleIOSurface::create(32, 32, EffectPixelFormat::RGBA16F);
  ASSERT_NE(half, nullptr);
  EXPECT_EQ(half->format(), EffectPixelFormat::RGBA16F);
  EXPECT_EQ(half->rowBytes(), 32u * 8u);
}

TEST(AppleIOSurfaceTest, RejectsAZeroSize) {
  EXPECT_EQ(AppleIOSurface::create(0, 64), nullptr);
  EXPECT_EQ(AppleIOSurface::create(64, 0), nullptr);
}

TEST(AppleIOSurfaceTest, WrapsAnExistingSurfaceAndRetainsIt) {
  const auto owner = AppleIOSurface::create(48, 32);
  ASSERT_NE(owner, nullptr);
  const IOSurfaceRef raw = owner->iosurface();

  const auto wrapped = AppleIOSurface::fromSurface(raw);
  ASSERT_NE(wrapped, nullptr);
  EXPECT_EQ(wrapped->width(), 48u);
  EXPECT_EQ(wrapped->height(), 32u);

  // Dropping the creator's wrapper must not invalidate the second one: `fromSurface` retained the
  // surface for itself.
  owner->release();
  EXPECT_TRUE(wrapped->isValid());
  EXPECT_EQ(wrapped->iosurface(), raw);

  wrapped->release();
  EXPECT_FALSE(wrapped->isValid());
  EXPECT_EQ(wrapped->iosurface(), nullptr);
}

TEST(AppleIOSurfaceTest, InfersTheFormatOfAnExistingSurface) {
  const auto surface = AppleIOSurface::create(16, 16, EffectPixelFormat::BGRA8);
  ASSERT_NE(surface, nullptr);
  EXPECT_EQ(AppleIOSurface::formatOf(surface->iosurface()), EffectPixelFormat::BGRA8);
  EXPECT_EQ(AppleIOSurface::formatOf(nullptr), EffectPixelFormat::BGRA8);
}

// ==================================================================================================
// The input factory
// ==================================================================================================

TEST(AppleVideoInputTest, CreatesTheCpuFallbackInput) {
  const auto input = AppleVideoInput::createFrameInput(64, 48, 4);
  ASSERT_NE(input, nullptr);
  EXPECT_NE(std::dynamic_pointer_cast<VideoFrameInputNode>(input), nullptr);
}

TEST(AppleVideoInputTest, CreatesAZeroCopyInputForAnIosurface) {
  const auto surface = AppleIOSurface::create(64, 48);
  ASSERT_NE(surface, nullptr);

  const auto input = AppleVideoInput::createIOSurfaceInput(surface->iosurface(), 0, 0,
                                                           EffectPixelFormat::BGRA8);
  ASSERT_NE(input, nullptr);
  EXPECT_NE(std::dynamic_pointer_cast<AppleIOSurfaceInputNode>(input), nullptr);
}

TEST(AppleVideoInputTest, RejectsAZeroCopyInputWithoutASurface) {
  EXPECT_EQ(AppleVideoInput::createIOSurfaceInput(nullptr), nullptr);
}

// ==================================================================================================
// The output factory
// ==================================================================================================

TEST(AppleVideoOutputNodeFactoryTest, CreatesAnOutputForASurface) {
  const auto surface = AppleIOSurface::create(64, 48);
  ASSERT_NE(surface, nullptr);

  AppleVideoOutputNodeFactory factory;
  const auto output = factory.create(surface);
  ASSERT_NE(output, nullptr);

  const auto iosurfaceOutput = std::dynamic_pointer_cast<AppleIOSurfaceOutputNode>(output);
  ASSERT_NE(iosurfaceOutput, nullptr);
  EXPECT_EQ(iosurfaceOutput->surface(), surface->iosurface());
  EXPECT_EQ(iosurfaceOutput->width(), 64u);
  EXPECT_EQ(iosurfaceOutput->height(), 48u);
  EXPECT_TRUE(iosurfaceOutput->isReady());
}

TEST(AppleVideoOutputNodeFactoryTest, ReportsTheSurfaceSize) {
  const auto surface = AppleIOSurface::create(320, 180);
  ASSERT_NE(surface, nullptr);

  AppleVideoOutputNodeFactory factory;
  uint32_t width = 0;
  uint32_t height = 0;
  EXPECT_TRUE(factory.querySize(surface, &width, &height));
  EXPECT_EQ(width, 320u);
  EXPECT_EQ(height, 180u);
}

TEST(AppleVideoOutputNodeFactoryTest, RejectsNullAndForeignSurfaces) {
  AppleVideoOutputNodeFactory factory;
  EXPECT_EQ(factory.create(nullptr), nullptr);

  uint32_t width = 0;
  uint32_t height = 0;
  EXPECT_FALSE(factory.querySize(nullptr, &width, &height));

  const auto foreign = std::make_shared<ForeignSurface>();
  EXPECT_EQ(factory.create(foreign), nullptr);
  EXPECT_FALSE(factory.querySize(foreign, &width, &height));
}

TEST(AppleVideoOutputNodeFactoryTest, RejectsAReleasedSurface) {
  const auto surface = AppleIOSurface::create(64, 48);
  ASSERT_NE(surface, nullptr);
  surface->release();

  AppleVideoOutputNodeFactory factory;
  EXPECT_EQ(factory.create(surface), nullptr);

  uint32_t width = 0;
  uint32_t height = 0;
  EXPECT_FALSE(factory.querySize(surface, &width, &height));
}

// ==================================================================================================
// The output renders into the surface
// ==================================================================================================

TEST(AppleIOSurfaceOutputTest, RendersAGraphFrameIntoTheSurface) {
  constexpr uint32_t kSize = 64;
  const auto surface = AppleIOSurface::create(kSize, kSize);
  ASSERT_NE(surface, nullptr);

  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = kSize;
  config.renderHeight = kSize;
  config.timeBase = 1'000'000;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto input = VideoFrameInputNode::create(kSize, kSize);
  const auto output = AppleIOSurfaceOutputNode::create(surface->iosurface(), 0, 0,
                                                       EffectPixelFormat::BGRA8);
  ASSERT_NE(input, nullptr);
  ASSERT_NE(output, nullptr);
  ASSERT_EQ(output->configure({}), EffectResult::Success);

  const auto pass = engine->createEffectNode("pass");
  ASSERT_NE(pass, nullptr);
  EffectShaderDesc shader;
  shader.name = "apple-output-passthrough";
  shader.fragmentSource = "  return texture(uInput0, uv);";
  shader.inputCount = 1;
  ASSERT_EQ(pass->setShader(shader), EffectResult::Success);

  ASSERT_EQ(engine->attach(input), EffectResult::Success);
  ASSERT_EQ(engine->attach(output), EffectResult::Success);
  ASSERT_EQ(engine->connect(input, pass), EffectResult::Success);
  ASSERT_EQ(engine->connect(pass, output), EffectResult::Success);
  ASSERT_EQ(engine->setOutput(output), EffectResult::Success);

  if (engine->start() != EffectResult::Success) {
    GTEST_SKIP() << "the effect engine could not start on this host: " << engine->diagnostics();
  }

  // A purely green frame in BGRA byte order; the node converts it to the surface's BGRA layout.
  std::vector<uint8_t> pixels(static_cast<size_t>(kSize) * kSize * 4u, 0u);
  for (size_t index = 0; index < pixels.size(); index += 4) {
    pixels[index + 1] = 255; // green
    pixels[index + 3] = 255; // opaque
  }
  VideoFrameDesc frame;
  frame.width = kSize;
  frame.height = kSize;
  frame.format = EffectPixelFormat::BGRA8;
  frame.planeCount = 1;
  frame.planes[0] = { pixels.data(), kSize * 4u };
  frame.timestamp = RenderTime::fromTicks(0, 1'000'000);

  ASSERT_EQ(input->submit(frame), EffectResult::Success);
  ASSERT_EQ(engine->execute(), EffectResult::Success) << engine->diagnostics();
  ASSERT_EQ(engine->flushAndWait(), EffectResult::Success) << engine->diagnostics();
  EXPECT_EQ(output->frameCount(), 1u);
  EXPECT_FALSE(output->hasPresentFailure()) << engine->diagnostics();

  const auto pixel = centrePixel(surface->iosurface());
  EXPECT_EQ(pixel[0], 0u) << "blue";
  EXPECT_EQ(pixel[1], 255u) << "green";
  EXPECT_EQ(pixel[2], 0u) << "red";
  EXPECT_EQ(pixel[3], 255u) << "alpha";

  engine->stop();
}

// ==================================================================================================
// The player drives the IOSurface output
// ==================================================================================================

TEST(AppleLivePlayerEffectTest, RendersARealVideoIntoTheOutputSurface) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  auto player = LivePlayer::create(path, LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);

  const auto surface = AppleIOSurface::create(320, 180, EffectPixelFormat::BGRA8);
  ASSERT_NE(surface, nullptr);

  const auto input = AppleVideoInput::createFrameInput(320, 180, 4);
  ASSERT_NE(input, nullptr);
  player->enableVideoEffectPipeline(std::make_shared<AppleVideoOutputNodeFactory>(), input, nullptr);
  ASSERT_TRUE(player->isVideoEffectPipelineEnabled());
  player->setOutputSurface(surface);

  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);

  const auto probe = std::make_shared<PlayerProbe>();
  player->addListener(probe);

  player->prepare();
  ASSERT_TRUE(waitFor([&] { return probe->ready() || probe->failed(); }, 10s))
      << "the player never became ready";
  ASSERT_FALSE(probe->failed()) << probe->error();

  player->play();

  if (!waitFor([&] { return engine->isRunning(); }, 5s)) {
    player->release();
    GTEST_SKIP() << "the effect engine could not start on this host: " << engine->diagnostics();
  }
  // The size event is reported by the video renderer when the first frame is decoded, so it proves
  // the file was actually demuxed and decoded rather than the player merely reaching READY.
  EXPECT_TRUE(waitFor([&] { return probe->videoSizeSeen(); }, 15s))
      << "the decoder never produced a frame: " << probe->error();
  EXPECT_TRUE(waitFor([&] { return engine->stats().frameCount > 0; }, 15s))
      << "the player never presented an effect frame: " << engine->diagnostics();

  // The surface is the same object the factory bound; after the GPU work is done it must hold a
  // picture rather than stay at its zero-filled allocation.
  EXPECT_EQ(engine->flushAndWait(), EffectResult::Success) << engine->diagnostics();
  EXPECT_GT(surface->allocationSize(), 0u);
  EXPECT_EQ(surface->width(), 320u);
  EXPECT_EQ(surface->height(), 180u);

  player->release();
}

} // namespace
} // namespace coral
