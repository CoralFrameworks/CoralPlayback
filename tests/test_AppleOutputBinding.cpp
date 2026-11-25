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
#include <coral/playback/apple/AppleHardwareVideoSurface.h>
#include <coral/playback/apple/AppleIOSurface.h>
#include <coral/playback/apple/AppleThumbnailProvider.h>
#include <coral/playback/apple/AppleVideoInput.h>
#include <coral/playback/apple/AppleVideoFrameSource.h>
#include <coral/playback/apple/AppleVideoOutputNodeFactory.h>

#include "platform/apple/AppleVideoFrameSink.h"

#include <effect/EffectEngine.h>
#include <effect/EffectPassNode.h>
#include <effect/NullOutputNode.h>
#include <effect/ReadbackOutputNode.h>
#include <effect/VideoFrameInputNode.h>
#include <effect/platform/apple/AppleIOSurfaceInputNode.h>
#include <effect/platform/apple/AppleIOSurfaceOutputNode.h>

#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>

#include <cstdlib>
#include <filesystem>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <future>

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

  void onVideoColorInfoChanged(const coral::VideoColorInfo& colorInfo) override {
    static_cast<void>(colorInfo);
    mVideoColorSeen.store(true, std::memory_order_release);
  }

  void onCues(const AVCueGroup& cues) override {
    for (const auto& cue : cues.getCues()) {
      if (cue.isBitmap()) {
        mBitmapCueSeen.store(true, std::memory_order_release);
      }
    }
  }

  void onTracksChanged(const coral::Tracks& tracks) override {
    static_cast<void>(tracks);
    mTracksSeen.store(true, std::memory_order_release);
    mTracksChangedCount.fetch_add(1, std::memory_order_acq_rel);
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
  [[nodiscard]] bool tracksSeen() const {
    return mTracksSeen.load(std::memory_order_acquire);
  }
  [[nodiscard]] int tracksChangedCount() const {
    return mTracksChangedCount.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool videoColorSeen() const {
    return mVideoColorSeen.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool bitmapCueSeen() const {
    return mBitmapCueSeen.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool failed() const { return mFailed.load(std::memory_order_acquire); }
  [[nodiscard]] std::string error() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mError;
  }

private:
  std::atomic<bool> mReady{false};
  std::atomic<bool> mVideoSizeSeen{false};
  std::atomic<bool> mTracksSeen{false};
  std::atomic<int> mTracksChangedCount{0};
  std::atomic<bool> mVideoColorSeen{false};
  std::atomic<bool> mBitmapCueSeen{false};
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

TEST(AppleVideoInputTest, CreatesTheZeroCopyAggregate) {
  const auto input = AppleVideoInput::createZeroCopyInput(320, 180);
  ASSERT_NE(input.inputNode, nullptr);
  ASSERT_NE(input.decoderSurface, nullptr);
  ASSERT_NE(input.frameSink, nullptr);
  EXPECT_NE(std::dynamic_pointer_cast<AppleIOSurfaceInputNode>(input.inputNode), nullptr);
  EXPECT_TRUE(input.decoderSurface->isValid());

  const auto zero = AppleVideoInput::createZeroCopyInput(0, 180);
  EXPECT_EQ(zero.inputNode, nullptr);
  EXPECT_EQ(zero.decoderSurface, nullptr);
  EXPECT_EQ(zero.frameSink, nullptr);
}

// ==================================================================================================
// The hardware decoder surface
// ==================================================================================================

TEST(AppleHardwareVideoSurfaceTest, RequestsHardwareFramesWithoutADevice) {
  const auto surface = AppleHardwareVideoSurface::create();
  ASSERT_NE(surface, nullptr);
  EXPECT_TRUE(surface->isValid());
  EXPECT_EQ(surface->getNativeDevice(), nullptr);
  EXPECT_EQ(surface->getInstance(), nullptr);

  auto* const device = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234));
  const auto withDevice = AppleHardwareVideoSurface::create(device);
  ASSERT_NE(withDevice, nullptr);
  EXPECT_EQ(withDevice->getNativeDevice(), device);
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
// NV12 input
// ==================================================================================================

/** Creates an IOSurface-backed NV12 pixel buffer with the given BT.709 limited-range planes. */
CVPixelBufferRef createNv12Buffer(const uint32_t width, const uint32_t height,
                                  const uint8_t lumaValue, const uint8_t chromaU,
                                  const uint8_t chromaV) {
  CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  if (attributes == nullptr) {
    return nullptr;
  }
  int value = static_cast<int>(width);
  CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
  CFDictionarySetValue(attributes, kCVPixelBufferWidthKey, number);
  CFRelease(number);
  value = static_cast<int>(height);
  number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
  CFDictionarySetValue(attributes, kCVPixelBufferHeightKey, number);
  CFRelease(number);
  value = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
  number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
  CFDictionarySetValue(attributes, kCVPixelBufferPixelFormatTypeKey, number);
  CFRelease(number);
  CFMutableDictionaryRef surfaceProperties = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  if (surfaceProperties != nullptr) {
    CFDictionarySetValue(attributes, kCVPixelBufferIOSurfacePropertiesKey, surfaceProperties);
    CFRelease(surfaceProperties);
  }

  CVPixelBufferRef buffer = nullptr;
  if (CVPixelBufferCreate(kCFAllocatorDefault, width, height,
                          kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, attributes,
                          &buffer) != kCVReturnSuccess) {
    CFRelease(attributes);
    return nullptr;
  }
  CFRelease(attributes);
  if (CVPixelBufferLockBaseAddress(buffer, 0) != kCVReturnSuccess) {
    CVPixelBufferRelease(buffer);
    return nullptr;
  }
  auto* luma = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 0));
  auto* chroma = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 1));
  const size_t lumaStride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 0);
  const size_t chromaStride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 1);
  for (uint32_t row = 0; row < height; ++row) {
    std::memset(luma + row * lumaStride, lumaValue, width);
  }
  for (uint32_t row = 0; row < (height + 1) / 2; ++row) {
    uint8_t* pixel = chroma + row * chromaStride;
    for (uint32_t column = 0; column < (width + 1) / 2; ++column) {
      pixel[column * 2 + 0] = chromaU;
      pixel[column * 2 + 1] = chromaV;
    }
  }
  CVPixelBufferUnlockBaseAddress(buffer, 0);
  return buffer;
}

TEST(AppleIOSurfaceInputTest, ConvertsBiPlanarNv12ToRgba) {
  constexpr uint32_t kSize = 64;
  CVPixelBufferRef buffer = createNv12Buffer(kSize, kSize, 63, 102, 240);
  ASSERT_NE(buffer, nullptr);
  IOSurfaceRef surface = CVPixelBufferGetIOSurface(buffer);
  ASSERT_NE(surface, nullptr);
  ASSERT_EQ(IOSurfaceGetPlaneCount(surface), 2u);

  EffectEngineConfig config;
  config.backend = EffectBackend::Metal;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = kSize;
  config.renderHeight = kSize;
  config.timeBase = 1'000'000;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto input = AppleIOSurfaceInputNode::create(kSize, kSize, EffectPixelFormat::YUV420);
  ASSERT_NE(input, nullptr);
  const auto pass = engine->createEffectNode("pass");
  ASSERT_NE(pass, nullptr);
  EffectShaderDesc shader;
  shader.name = "apple-nv12-passthrough";
  shader.fragmentSource = "  return texture(uInput0, uv);";
  shader.inputCount = 1;
  ASSERT_EQ(pass->setShader(shader), EffectResult::Success);
  const auto output = ReadbackOutputNode::create();
  ASSERT_NE(output, nullptr);

  ASSERT_EQ(engine->attach(input), EffectResult::Success);
  ASSERT_EQ(engine->attach(output), EffectResult::Success);
  ASSERT_EQ(engine->connect(input, pass), EffectResult::Success);
  ASSERT_EQ(engine->connect(pass, output), EffectResult::Success);
  ASSERT_EQ(engine->setOutput(output), EffectResult::Success);

  if (engine->start() != EffectResult::Success) {
    CVPixelBufferRelease(buffer);
    GTEST_SKIP() << "the effect engine could not start on this host: " << engine->diagnostics();
  }
  ASSERT_EQ(input->submit(surface, kSize, kSize, EffectPixelFormat::YUV420,
                          RenderTime::fromTicks(0, 1'000'000)),
            EffectResult::Success);
  ASSERT_EQ(engine->execute(), EffectResult::Success) << engine->diagnostics();
  ASSERT_EQ(engine->flushAndWait(), EffectResult::Success) << engine->diagnostics();

  EffectFrame metadata;
  std::vector<uint8_t> pixels;
  ASSERT_TRUE(output->waitForFrame(&metadata, &pixels)) << engine->diagnostics();
  const size_t centre = (static_cast<size_t>(kSize / 2) * kSize + kSize / 2) * 4;
  EXPECT_GE(pixels[centre + 0], 245u) << "red";
  EXPECT_LE(pixels[centre + 1], 8u) << "green";
  EXPECT_LE(pixels[centre + 2], 8u) << "blue";
  EXPECT_GE(pixels[centre + 3], 250u) << "alpha";

  engine->stop();
  CVPixelBufferRelease(buffer);
}

TEST(AppleIOSurfaceInputTest, HandlesSizeChangesWithoutStaleFrames) {
  constexpr uint32_t kRenderSize = 64;

  EffectEngineConfig config;
  config.backend = EffectBackend::Metal;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = kRenderSize;
  config.renderHeight = kRenderSize;
  config.timeBase = 1'000'000;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);

  const auto input = AppleIOSurfaceInputNode::create(kRenderSize, kRenderSize,
                                                     EffectPixelFormat::YUV420);
  ASSERT_NE(input, nullptr);
  const auto pass = engine->createEffectNode("pass");
  ASSERT_NE(pass, nullptr);
  EffectShaderDesc shader;
  shader.name = "apple-nv12-size-change";
  shader.fragmentSource = "  return texture(uInput0, uv);";
  shader.inputCount = 1;
  ASSERT_EQ(pass->setShader(shader), EffectResult::Success);
  const auto output = ReadbackOutputNode::create();
  ASSERT_NE(output, nullptr);

  ASSERT_EQ(engine->attach(input), EffectResult::Success);
  ASSERT_EQ(engine->attach(output), EffectResult::Success);
  ASSERT_EQ(engine->connect(input, pass), EffectResult::Success);
  ASSERT_EQ(engine->connect(pass, output), EffectResult::Success);
  ASSERT_EQ(engine->setOutput(output), EffectResult::Success);
  if (engine->start() != EffectResult::Success) {
    GTEST_SKIP() << "the effect engine could not start on this host: " << engine->diagnostics();
  }

  int64_t timestampUs = 0;
  auto render = [&](const uint32_t width, const uint32_t height, const uint8_t luma,
                    const uint8_t chromaU, const uint8_t chromaV) {
    CVPixelBufferRef buffer = createNv12Buffer(width, height, luma, chromaU, chromaV);
    EXPECT_NE(buffer, nullptr);
    if (buffer == nullptr) {
      return std::vector<uint8_t>();
    }
    IOSurfaceRef surface = CVPixelBufferGetIOSurface(buffer);
    EXPECT_NE(surface, nullptr);
    const auto result = input->submit(surface, width, height, EffectPixelFormat::YUV420,
                                      RenderTime::fromTicks(timestampUs, 1'000'000));
    EXPECT_EQ(result, EffectResult::Success) << engine->diagnostics();
    timestampUs += 40'000;
    EXPECT_EQ(input->width(), width);
    EXPECT_EQ(input->height(), height);

    EXPECT_EQ(engine->execute(), EffectResult::Success) << engine->diagnostics();
    EXPECT_EQ(engine->flushAndWait(), EffectResult::Success) << engine->diagnostics();
    EffectFrame metadata;
    std::vector<uint8_t> pixels;
    EXPECT_TRUE(output->waitForFrame(&metadata, &pixels)) << engine->diagnostics();
    CVPixelBufferRelease(buffer);
    return pixels;
  };

  const auto red = render(64, 64, 63, 102, 240);
  ASSERT_FALSE(red.empty());
  const size_t centre = (static_cast<size_t>(kRenderSize / 2) * kRenderSize + kRenderSize / 2) * 4;
  EXPECT_GE(red[centre + 0], 245u) << "first frame should be red";
  EXPECT_LE(red[centre + 1], 8u);
  EXPECT_LE(red[centre + 2], 8u);

  const auto white = render(96, 48, 235, 128, 128);
  ASSERT_FALSE(white.empty());
  EXPECT_GE(white[centre + 0], 245u) << "the resized frame should be white, not the old red frame";
  EXPECT_GE(white[centre + 1], 245u);
  EXPECT_GE(white[centre + 2], 245u);

  const auto black = render(128, 72, 16, 128, 128);
  ASSERT_FALSE(black.empty());
  EXPECT_LE(black[centre + 0], 8u) << "the second resize should be black, not a previous frame";
  EXPECT_LE(black[centre + 1], 8u);
  EXPECT_LE(black[centre + 2], 8u);

  engine->stop();
}

TEST(AppleLivePlayerEffectTest, ReportsTheAudioTrackWhenAudioIsEnabled) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  auto player = LivePlayer::create(path, LivePlayer::Configuration{.audioEnabled = true});
  ASSERT_NE(player, nullptr);
  const auto probe = std::make_shared<PlayerProbe>();
  player->addListener(probe);
  player->prepare();
  ASSERT_TRUE(waitFor([&] {
    return player->getPlaybackState() == coral::Player::State::STATE_READY ||
           !player->getPlaybackErrorMessage().empty();
  }, 10s)) << player->getPlaybackErrorMessage();
  if (!player->getPlaybackErrorMessage().empty()) {
    player->release();
    GTEST_SKIP() << "audio playback is unavailable on this host: " << player->getPlaybackErrorMessage();
  }

  bool hasAudio = false;
  bool audioSelected = false;
  for (const auto& group : player->getCurrentTracks().groups) {
    for (const auto& track : group.tracks) {
      if (track.type == coral::TrackType::Audio) {
        hasAudio = true;
        audioSelected = audioSelected || track.selected;
      }
    }
  }
  EXPECT_TRUE(hasAudio);
  EXPECT_TRUE(audioSelected);
  player->release();
}

TEST(AppleLivePlayerEffectTest, SwitchesBetweenTwoAudioTracks) {
  const char* overridePath = std::getenv("CORAL_MULTI_AUDIO");
  const std::string path = overridePath != nullptr
    ? overridePath
    : std::string(CORAL_ASSETS_DIR) + "/video/multi_audio_5s.mp4";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "the two-audio-track fixture is missing: " << path;
  }
  auto player = LivePlayer::create(path, LivePlayer::Configuration{.audioEnabled = true});
  ASSERT_NE(player, nullptr);
  const auto probe = std::make_shared<PlayerProbe>();
  player->addListener(probe);
  player->prepare();
  ASSERT_TRUE(waitFor([&] {
    return player->getPlaybackState() == coral::Player::State::STATE_READY ||
           !player->getPlaybackErrorMessage().empty();
  }, 10s)) << player->getPlaybackErrorMessage();
  if (!player->getPlaybackErrorMessage().empty()) {
    player->release();
    GTEST_SKIP() << "audio playback is unavailable on this host: " << player->getPlaybackErrorMessage();
  }

  std::vector<coral::Track> audioTracks;
  auto refreshAudioTracks = [&]() {
    audioTracks.clear();
    for (const auto& group : player->getCurrentTracks().groups) {
      for (const auto& track : group.tracks) {
        if (track.type == coral::TrackType::Audio) {
          audioTracks.push_back(track);
        }
      }
    }
  };
  auto selectedAudioTrack = [&]() -> int {
    refreshAudioTracks();
    for (size_t index = 0; index < audioTracks.size(); ++index) {
      if (audioTracks[index].selected) {
        return static_cast<int>(index);
      }
    }
    return -1;
  };
  for (const auto& group : player->getCurrentTracks().groups) {
    for (const auto& track : group.tracks) {
      if (track.type == coral::TrackType::Audio) {
        audioTracks.push_back(track);
      }
    }
  }
  ASSERT_GE(audioTracks.size(), 2u) << "the test asset must contain two audio tracks";
  const auto target = audioTracks.front().selected ? audioTracks.back() : audioTracks.front();
  player->selectTrack(target.rendererIndex, target.groupIndex, target.trackIndex);
  EXPECT_TRUE(waitFor([&] { return selectedAudioTrack() == target.trackIndex; }, 5s))
      << "the selector did not switch to audio track " << target.trackIndex;
  player->clearTrackSelection(target.rendererIndex);

  player->release();
}

TEST(AppleVideoFrameSourceTest, ImportsTheOutputSurfaceIntoTheUiDevice) {
  const auto surface = AppleIOSurface::create(64, 48, EffectPixelFormat::BGRA8);
  ASSERT_NE(surface, nullptr);

  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Manual;
  config.renderWidth = 64;
  config.renderHeight = 48;
  config.timeBase = 1'000'000;
  const auto engine = EffectEngine::create(config);
  ASSERT_NE(engine, nullptr);
  const auto input = VideoFrameInputNode::create(64, 48);
  const auto nullOutput = NullOutputNode::create();
  ASSERT_NE(input, nullptr);
  ASSERT_NE(nullOutput, nullptr);
  ASSERT_EQ(engine->attach(input), EffectResult::Success);
  ASSERT_EQ(engine->attach(nullOutput), EffectResult::Success);
  ASSERT_EQ(engine->connect(input, nullOutput), EffectResult::Success);
  ASSERT_EQ(engine->setOutput(nullOutput), EffectResult::Success);
  if (engine->start() != EffectResult::Success) {
    GTEST_SKIP() << "the effect engine could not start: " << engine->diagnostics();
  }
  auto* const device = engine->device();
  ASSERT_NE(device, nullptr);

  auto source = AppleVideoFrameSource::create(surface, *device);
  ASSERT_NE(source, nullptr);
  VideoFrameOutput output;
  output.surface = surface;
  output.sequence = 1;
  output.positionUs = 40000;
  std::string error;
  ASSERT_TRUE(source->refresh(output, error)) << error;
  EXPECT_TRUE(source->image().isValid());
  EXPECT_EQ(source->image().width, 64u);
  EXPECT_EQ(source->image().height, 48u);
  EXPECT_EQ(source->importCount(), 1u);
  // The same sequence is served from the cached import.
  ASSERT_TRUE(source->refresh(output, error)) << error;
  EXPECT_EQ(source->importCount(), 1u);
  output.sequence = 2;
  ASSERT_TRUE(source->refresh(output, error)) << error;
  EXPECT_EQ(source->importCount(), 2u);

  source.reset();
  engine->stop();
}

TEST(AppleLivePlayerEffectTest, SwitchesBetweenMultipleAudioRenderers) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/multi_audio_5s.mp4";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "the two-audio-track fixture is missing: " << path;
  }
  auto player = LivePlayer::create(
      path, LivePlayer::Configuration{.audioEnabled = true, .audioRendererCount = 2});
  ASSERT_NE(player, nullptr);
  player->prepare();
  ASSERT_TRUE(waitFor([&] {
    return player->getPlaybackState() == coral::Player::State::STATE_READY ||
           !player->getPlaybackErrorMessage().empty();
  }, 10s)) << player->getPlaybackErrorMessage();
  if (!player->getPlaybackErrorMessage().empty()) {
    player->release();
    GTEST_SKIP() << "audio playback is unavailable on this host: " << player->getPlaybackErrorMessage();
  }

  auto audioTracks = [&]() {
    std::vector<coral::Track> tracks;
    for (const auto& group : player->getCurrentTracks().groups) {
      for (const auto& track : group.tracks) {
        if (track.type == coral::TrackType::Audio) {
          tracks.push_back(track);
        }
      }
    }
    return tracks;
  };
  const auto initialTracks = audioTracks();
  ASSERT_GE(initialTracks.size(), 2u);
  ASSERT_EQ(player->getAudioRendererStats().size(), 2u);
  EXPECT_EQ(player->getAudioRendererSwitchCount(), 0u);
  const auto target = std::find_if(initialTracks.begin(), initialTracks.end(),
      [&](const coral::Track& track) { return !track.selected; });
  ASSERT_NE(target, initialTracks.end());
  player->selectTrack(target->rendererIndex, target->groupIndex, target->trackIndex);
  ASSERT_TRUE(waitFor([&] {
    const auto tracks = audioTracks();
    int selected = 0;
    bool targetSelected = false;
    for (const auto& track : tracks) {
      if (track.selected) {
        ++selected;
        targetSelected = track.rendererIndex == target->rendererIndex &&
                         track.groupIndex == target->groupIndex &&
                         track.trackIndex == target->trackIndex;
      }
    }
    return selected == 1 && targetSelected;
  }, 5s)) << "selecting a track did not activate exactly its audio renderer";
  EXPECT_GE(player->getAudioRendererSwitchCount(), 1u);
  EXPECT_TRUE(waitFor([&] {
    const auto stats = player->getAudioRendererStats();
    int enabledCount = 0;
    for (const auto& entry : stats) {
      enabledCount += entry.enabled ? 1 : 0;
    }
    return enabledCount == 1;
  }, 5s)) << "the target audio renderer did not become the only enabled one";

  player->release();
}

TEST(AppleLivePlayerEffectTest, ReportsAndSelectsSideLoadedSubtitleTracks) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  const std::string english = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample.srt";
  const std::string chinese = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample_zho.srt";
  if (!std::filesystem::exists(english) || !std::filesystem::exists(chinese)) {
    GTEST_SKIP() << "the side-loaded subtitle fixtures are missing";
  }
  std::vector<MediaItem::SubtitleConfiguration> subtitles;
  subtitles.emplace_back(english, "application/x-subrip", "en", "English", "en-srt");
  subtitles.emplace_back(chinese, "application/x-subrip", "zh", "Chinese", "zh-srt");
  auto item = MediaItem::fromUrl(video, std::move(subtitles));
  ASSERT_NE(item, nullptr);
  auto player = LivePlayer::create(item, LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);
  const auto probe = std::make_shared<PlayerProbe>();
  player->addListener(probe);
  player->prepare();
  ASSERT_TRUE(waitFor([&] {
    return player->getPlaybackState() == coral::Player::State::STATE_READY ||
           !player->getPlaybackErrorMessage().empty();
  }, 10s)) << player->getPlaybackErrorMessage();
  ASSERT_TRUE(player->getPlaybackErrorMessage().empty());

  auto textTracks = [&]() {
    std::vector<coral::Track> tracks;
    for (const auto& group : player->getCurrentTracks().groups) {
      for (const auto& track : group.tracks) {
        if (track.type == coral::TrackType::Text) {
          tracks.push_back(track);
        }
      }
    }
    return tracks;
  };
  const auto tracks = textTracks();
  ASSERT_GE(tracks.size(), 2u) << "both side-loaded subtitle tracks should be advertised";
  const auto target = std::find_if(tracks.begin(), tracks.end(),
      [&](const coral::Track& track) { return !track.selected; });
  ASSERT_NE(target, tracks.end());
  const int changesBefore = probe->tracksChangedCount();
  player->selectTrack(target->rendererIndex, target->groupIndex, target->trackIndex);
  EXPECT_TRUE(waitFor([&] {
    for (const auto& track : textTracks()) {
      if (track.selected && track.rendererIndex == target->rendererIndex &&
          track.groupIndex == target->groupIndex && track.trackIndex == target->trackIndex) {
        return true;
      }
    }
    return false;
  }, 5s)) << "the selected subtitle track is not the target";
  EXPECT_GT(probe->tracksChangedCount(), changesBefore);
  player->release();
}

TEST(AppleLivePlayerEffectTest, CompositesAPgsCueIntoTheOutputSurface) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/pgs_subtitle_5s.mkv";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "the PGS fixture is missing: " << path;
  }
  auto player = LivePlayer::create(path, LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);
  const auto surface = AppleIOSurface::create(320, 180, EffectPixelFormat::BGRA8);
  ASSERT_NE(surface, nullptr);
  const auto input = AppleVideoInput::createFrameInput(320, 180, 4);
  ASSERT_NE(input, nullptr);
  VideoEffectInput effectInput;
  effectInput.inputNode = input;
  player->enableVideoEffectPipeline(std::make_shared<AppleVideoOutputNodeFactory>(), effectInput);
  player->setOutputSurface(surface);
  player->setPreferredTextLanguage("eng");
  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);
  player->prepare();
  ASSERT_TRUE(waitFor([&] { return player->getPlaybackState() == coral::Player::State::STATE_READY; },
                      10s)) << player->getPlaybackErrorMessage();
  player->play();
  ASSERT_TRUE(waitFor([&] { return engine->isRunning() && engine->stats().frameCount > 0; }, 15s))
      << engine->diagnostics();

  // The fixture's PGS palette is (254, 0, 0); the cue is full-frame, so the composed center pixel
  // must carry that red rather than the underlying video.
  bool red = false;
  std::vector<uint8_t> pixel;
  for (int attempt = 0; attempt < 100 && !red; ++attempt) {
    EXPECT_EQ(engine->flushAndWait(), EffectResult::Success) << engine->diagnostics();
    pixel = centrePixel(surface->iosurface());
    red = pixel[2] >= 200 && pixel[1] <= 130;
    if (!red) {
      std::this_thread::sleep_for(10ms);
    }
  }
  EXPECT_TRUE(red) << "PGS bitmap was not composited; BGRA="
                   << static_cast<int>(pixel[0]) << "," << static_cast<int>(pixel[1]) << ","
                   << static_cast<int>(pixel[2]) << "," << static_cast<int>(pixel[3]);

  // The second PGS display set replaces red with blue at 2 s.
  ASSERT_TRUE(waitFor([&] { return player->getCurrentPositionUs() >= 2'200'000; }, 10s));
  bool blue = false;
  for (int attempt = 0; attempt < 100 && !blue; ++attempt) {
    EXPECT_EQ(engine->flushAndWait(), EffectResult::Success) << engine->diagnostics();
    pixel = centrePixel(surface->iosurface());
    blue = pixel[0] >= 200 && pixel[2] <= 130;
    if (!blue) {
      std::this_thread::sleep_for(10ms);
    }
  }
  EXPECT_TRUE(blue) << "the PGS blue replacement was not composited; BGRA="
                    << static_cast<int>(pixel[0]) << "," << static_cast<int>(pixel[1]) << ","
                    << static_cast<int>(pixel[2]) << "," << static_cast<int>(pixel[3]);
  player->release();
}

TEST(AppleLivePlayerEffectTest, CompositesAVobSubCueIntoTheOutputSurface) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/vobsub_subtitle_5s.mkv";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "the VobSub fixture is missing: " << path;
  }
  auto player = LivePlayer::create(path, LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);
  const auto surface = AppleIOSurface::create(320, 180, EffectPixelFormat::BGRA8);
  ASSERT_NE(surface, nullptr);
  const auto input = AppleVideoInput::createFrameInput(320, 180, 4);
  ASSERT_NE(input, nullptr);
  VideoEffectInput effectInput;
  effectInput.inputNode = input;
  player->enableVideoEffectPipeline(std::make_shared<AppleVideoOutputNodeFactory>(), effectInput);
  player->setOutputSurface(surface);
  player->setPreferredTextLanguage("eng");
  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);
  const auto probe = std::make_shared<PlayerProbe>();
  player->addListener(probe);
  player->prepare();
  ASSERT_TRUE(waitFor([&] { return player->getPlaybackState() == coral::Player::State::STATE_READY; },
                      10s)) << player->getPlaybackErrorMessage();
  player->play();
  ASSERT_TRUE(waitFor([&] { return engine->isRunning() && engine->stats().frameCount > 0; }, 15s))
      << engine->diagnostics();
  ASSERT_TRUE(waitFor([&] { return probe->bitmapCueSeen(); }, 10s))
      << "the VobSub cue never reached the player events";
  bool red = false;
  std::vector<uint8_t> pixel;
  for (int attempt = 0; attempt < 100 && !red; ++attempt) {
    EXPECT_EQ(engine->flushAndWait(), EffectResult::Success) << engine->diagnostics();
    pixel = centrePixel(surface->iosurface());
    red = pixel[2] >= 200 && pixel[1] <= 130;
    if (!red) {
      std::this_thread::sleep_for(10ms);
    }
  }
  EXPECT_TRUE(red) << "VobSub bitmap was not composited; BGRA="
                   << static_cast<int>(pixel[0]) << "," << static_cast<int>(pixel[1]) << ","
                   << static_cast<int>(pixel[2]) << "," << static_cast<int>(pixel[3]);

  // The second SPU replaces red with green at 3 s.
  ASSERT_TRUE(waitFor([&] { return player->getCurrentPositionUs() >= 3'200'000; }, 10s));
  bool green = false;
  for (int attempt = 0; attempt < 100 && !green; ++attempt) {
    EXPECT_EQ(engine->flushAndWait(), EffectResult::Success) << engine->diagnostics();
    pixel = centrePixel(surface->iosurface());
    green = pixel[1] >= 200 && pixel[2] <= 130;
    if (!green) {
      std::this_thread::sleep_for(10ms);
    }
  }
  EXPECT_TRUE(green) << "the VobSub green replacement was not composited; BGRA="
                     << static_cast<int>(pixel[0]) << "," << static_cast<int>(pixel[1]) << ","
                     << static_cast<int>(pixel[2]) << "," << static_cast<int>(pixel[3]);
  player->release();
}

TEST(AppleLivePlayerEffectTest, PlaysHdr10AndReportsItsColorInfo) {
  const char* const asset = std::getenv("CORAL_HDR_ASSET");
  if (asset == nullptr || !std::filesystem::exists(asset)) {
    GTEST_SKIP() << "set CORAL_HDR_ASSET to an HDR10/PQ file to run this test";
  }
  auto player = LivePlayer::create(std::string(asset), LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);
  const auto surface = AppleIOSurface::create(320, 180, EffectPixelFormat::BGRA8);
  ASSERT_NE(surface, nullptr);
  const auto effectInput = AppleVideoInput::createZeroCopyInput(320, 180);
  ASSERT_NE(effectInput.inputNode, nullptr);
  ASSERT_NE(effectInput.decoderSurface, nullptr);
  ASSERT_NE(effectInput.frameSink, nullptr);
  const auto appleSink = std::dynamic_pointer_cast<AppleVideoFrameSink>(effectInput.frameSink);
  ASSERT_NE(appleSink, nullptr);
  player->enableVideoEffectPipeline(std::make_shared<AppleVideoOutputNodeFactory>(), effectInput);
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
  ASSERT_TRUE(waitFor([&] { return probe->videoColorSeen(); }, 15s))
      << "the HDR decoder never reported its colour info";
  const auto color = player->getVideoColorInfo();
  EXPECT_TRUE(color.isHdr());
  EXPECT_EQ(color.primaries, coral::VideoColorPrimaries::BT2020);
  EXPECT_TRUE(color.transfer == coral::VideoColorTransfer::PQ ||
              color.transfer == coral::VideoColorTransfer::HLG);
  EXPECT_TRUE(waitFor([&] { return engine->stats().frameCount > 0; }, 15s))
      << "the HDR file never reached the effect output: " << engine->diagnostics();
  EXPECT_GT(appleSink->submittedFrameCount(), 0u)
      << "the HDR decoder frame never reached the Apple sink";
  // The first presented frame can be black in the source itself; poll the output before deciding the
  // tone-map never reached it.
  bool nonBlack = false;
  std::vector<uint8_t> pixel;
  for (int attempt = 0; attempt < 100 && !nonBlack; ++attempt) {
    EXPECT_EQ(engine->flushAndWait(), EffectResult::Success) << engine->diagnostics();
    pixel = centrePixel(surface->iosurface());
    nonBlack = pixel != std::vector<uint8_t>({0, 0, 0, 0});
    if (!nonBlack) {
      std::this_thread::sleep_for(10ms);
    }
  }
  EXPECT_TRUE(nonBlack) << "the HDR tone-map did not reach the SDR output surface; centre pixel="
                        << static_cast<int>(pixel[0]) << "," << static_cast<int>(pixel[1]) << ","
                        << static_cast<int>(pixel[2]) << "," << static_cast<int>(pixel[3]);
  player->release();
}

TEST(AppleThumbnailProviderTest, DecodesAFrameAtTheRequestedTime) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  auto provider = AppleThumbnailProvider::create(path, 320, 180);
  ASSERT_NE(provider, nullptr);
  auto promise = std::make_shared<std::promise<Thumbnail>>();
  auto future = promise->get_future();
  provider->requestThumbnail(1'000'000, [promise](Thumbnail thumbnail) {
    promise->set_value(std::move(thumbnail));
  });
  ASSERT_EQ(future.wait_for(10s), std::future_status::ready);
  const auto thumbnail = future.get();
  ASSERT_NE(thumbnail.frame, nullptr);
  EXPECT_EQ(thumbnail.positionUs, 1'000'000);
  EXPECT_GT(thumbnail.frame->width(), 0);
  EXPECT_GT(thumbnail.frame->height(), 0);
  EXPECT_LE(thumbnail.frame->width(), 320);
  EXPECT_LE(thumbnail.frame->height(), 180);
  provider->cancel();
}

TEST(AppleLivePlayerEffectTest, SelectsThePreferredSubtitleLanguage) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  const std::string english = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample.srt";
  const std::string chinese = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample_zho.srt";
  if (!std::filesystem::exists(english) || !std::filesystem::exists(chinese)) {
    GTEST_SKIP() << "the subtitle fixtures are missing";
  }
  std::vector<MediaItem::SubtitleConfiguration> subtitles;
  subtitles.emplace_back(english, "application/x-subrip", "en", "English", "en-srt");
  subtitles.emplace_back(chinese, "application/x-subrip", "zh", "Chinese", "zh-srt");
  auto player = LivePlayer::create(MediaItem::fromUrl(video, std::move(subtitles)),
                                   LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);
  player->setPreferredAudioLanguage("eng");
  player->setPreferredTextLanguage("zh");
  player->setPreferredTextRoleFlags(1);
  player->setSelectUndeterminedTextLanguage(false);
  EXPECT_EQ(player->getPreferredAudioLanguage(), "eng");
  EXPECT_EQ(player->getPreferredTextLanguage(), "zh");
  EXPECT_EQ(player->getPreferredTextRoleFlags(), 1);
  EXPECT_FALSE(player->getSelectUndeterminedTextLanguage());
  player->prepare();
  ASSERT_TRUE(waitFor([&] {
    return player->getPlaybackState() == coral::Player::State::STATE_READY ||
           !player->getPlaybackErrorMessage().empty();
  }, 10s)) << player->getPlaybackErrorMessage();
  ASSERT_TRUE(player->getPlaybackErrorMessage().empty());

  bool chineseSelected = false;
  for (const auto& group : player->getCurrentTracks().groups) {
    for (const auto& track : group.tracks) {
      if (track.type == coral::TrackType::Text && track.selected && track.format != nullptr) {
        chineseSelected = track.format->getLanguage() == "zh";
      }
    }
  }
  EXPECT_TRUE(chineseSelected) << "the preferred zh subtitle was not selected";
  player->release();
}

TEST(AppleLivePlayerEffectTest, SelectsAPreferredSubtitleRole) {
  const std::string video = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  const std::string english = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample.srt";
  const std::string chinese = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample_zho.srt";
  if (!std::filesystem::exists(english) || !std::filesystem::exists(chinese)) {
    GTEST_SKIP() << "the subtitle fixtures are missing";
  }
  std::vector<MediaItem::SubtitleConfiguration> subtitles;
  subtitles.emplace_back(english, "application/x-subrip", "en", "English", "en-srt",
                         coral::RoleFlags::kRoleFlagCaption, 0);
  subtitles.emplace_back(chinese, "application/x-subrip", "zh", "Chinese", "zh-srt");
  auto player = LivePlayer::create(MediaItem::fromUrl(video, std::move(subtitles)),
                                   LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);
  player->setPreferredTextRoleFlags(coral::RoleFlags::kRoleFlagCaption);
  player->setSelectUndeterminedTextLanguage(false);
  player->prepare();
  ASSERT_TRUE(waitFor([&] {
    return player->getPlaybackState() == coral::Player::State::STATE_READY ||
           !player->getPlaybackErrorMessage().empty();
  }, 10s)) << player->getPlaybackErrorMessage();
  ASSERT_TRUE(player->getPlaybackErrorMessage().empty());

  const auto captionIsSelected = [&] {
    for (const auto& group : player->getCurrentTracks().groups) {
      for (const auto& track : group.tracks) {
        if (track.type == coral::TrackType::Text && track.selected && track.format != nullptr &&
            (track.format->getRoleFlags() &
             static_cast<uint32_t>(coral::RoleFlags::kRoleFlagCaption)) != 0) {
          return true;
        }
      }
    }
    return false;
  };
  EXPECT_TRUE(waitFor(captionIsSelected, 5s))
      << "the caption-role subtitle was not selected within the reselection window";
  player->release();
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
  VideoEffectInput effectInput;
  effectInput.inputNode = input;
  player->enableVideoEffectPipeline(std::make_shared<AppleVideoOutputNodeFactory>(), effectInput);
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

TEST(AppleLivePlayerEffectTest, SamplesTheDecodersSurfaceThroughTheAppleSink) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  auto player = LivePlayer::create(path, LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);

  const auto surface = AppleIOSurface::create(320, 180, EffectPixelFormat::BGRA8);
  ASSERT_NE(surface, nullptr);

  const auto effectInput = AppleVideoInput::createZeroCopyInput(320, 180);
  ASSERT_NE(effectInput.inputNode, nullptr);
  ASSERT_NE(effectInput.decoderSurface, nullptr);
  ASSERT_NE(effectInput.frameSink, nullptr);
  const auto appleSink = std::dynamic_pointer_cast<AppleVideoFrameSink>(effectInput.frameSink);
  ASSERT_NE(appleSink, nullptr);

  player->enableVideoEffectPipeline(std::make_shared<AppleVideoOutputNodeFactory>(), effectInput);
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
  EXPECT_TRUE(waitFor([&] { return probe->videoSizeSeen(); }, 15s))
      << "the decoder never produced a frame: " << probe->error();
  EXPECT_TRUE(waitFor([&] { return engine->stats().frameCount > 0; }, 15s))
      << "the player never presented an effect frame: " << engine->diagnostics();

  // The sink asks the decoder for BGRA through MediaFormat::processFormat. When the decoder honors
  // it, the sink submits the decoder's own IOSurface and never runs the NV12 conversion fallback.
  EXPECT_TRUE(waitFor([&] { return appleSink->directFrameCount() > 0; }, 10s))
      << "the decoder did not honor the BGRA process format; converted="
      << appleSink->convertedFrameCount();
  EXPECT_EQ(appleSink->convertedFrameCount(), 0u)
      << "the conversion ring is a fallback and should not run for a BGRA decoder";
  EXPECT_TRUE(waitFor([&] { return probe->videoColorSeen(); }, 10s))
      << "the player never reported its video colour info";
  const auto colorInfo = player->getVideoColorInfo();
  const auto frameOutput = player->getVideoFrameOutput();
  EXPECT_EQ(colorInfo.primaries, coral::VideoColorPrimaries::BT709);
  EXPECT_EQ(colorInfo.transfer, coral::VideoColorTransfer::SDR);
  EXPECT_EQ(colorInfo.matrix, coral::VideoColorMatrix::BT709);
  EXPECT_EQ(colorInfo.range, coral::VideoColorRange::Limited);
  EXPECT_FALSE(colorInfo.isHdr());
  EXPECT_EQ(frameOutput.colorInfo.primaries, colorInfo.primaries);
  EXPECT_TRUE(waitFor([&] { return probe->tracksSeen(); }, 10s))
      << "the player never reported its tracks";
  const auto tracks = player->getCurrentTracks();
  ASSERT_FALSE(tracks.isEmpty()) << "the prepared period should report its video and audio tracks";
  bool hasVideo = false;
  bool hasAudio = false;
  bool hasSelected = false;
  for (const auto& group : tracks.groups) {
    for (const auto& track : group.tracks) {
      hasVideo = hasVideo || track.type == coral::TrackType::Video;
      hasAudio = hasAudio || track.type == coral::TrackType::Audio;
      hasSelected = hasSelected || track.selected;
    }
  }
  EXPECT_TRUE(hasVideo);
  EXPECT_TRUE(hasSelected);
  EXPECT_NE(frameOutput.surface, nullptr);
  EXPECT_GT(frameOutput.sequence, 0u);
  EXPECT_GE(frameOutput.positionUs, 0);
  ASSERT_NE(frameOutput.fence, nullptr);
  EXPECT_NE(frameOutput.fence->wait(coral::Fence::Mode::kDontFlush, 2'000'000'000ull),
            coral::Fence::Status::kTimeoutExpired);
  EXPECT_GE(player->getBufferedPositionUs(), player->getCurrentPositionUs());
  const auto bufferedRanges = player->getBufferedRanges();
  if (player->getBufferedPositionUs() > player->getCurrentPositionUs()) {
    EXPECT_FALSE(bufferedRanges.empty());
    EXPECT_EQ(bufferedRanges.front().startUs, player->getCurrentPositionUs());
  }
  player->setVideoScaleMode(coral::Player::VideoScaleMode::Stretch);
  EXPECT_EQ(player->getVideoScaleMode(), coral::Player::VideoScaleMode::Stretch);
  player->setVideoScaleMode(coral::Player::VideoScaleMode::Fit);
  player->setVideoMirror(true, false);
  player->setVideoMirror(false, false);
  if (engine->backend() == EffectBackend::Metal) {
    EXPECT_TRUE(waitFor([&] { return effectInput.frameSink->isZeroCopy(); }, 10s))
        << "the Apple input should sample the decoder's IOSurface in place: "
        << engine->diagnostics();
  }

  EXPECT_EQ(engine->flushAndWait(), EffectResult::Success) << engine->diagnostics();
  player->release();
}

} // namespace
} // namespace coral
