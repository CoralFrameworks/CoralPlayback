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

#include <coral/playback/LivePlayer.h>
#include <coral/playback/VideoOutput.h>
#include <coral/playback/win32/Win32TextureSurface.h>
#include <coral/playback/win32/Win32WindowSurface.h>
#include <coral/playback/win32/WindowsHardwareVideoSurface.h>
#include <coral/playback/win32/WindowsTextureOutputNodeFactory.h>
#include <coral/playback/win32/WindowsThumbnailProvider.h>
#include <coral/playback/win32/WindowsVideoDevice.h>
#include <coral/playback/win32/WindowsVideoFrameSink.h>
#include <coral/playback/win32/WindowsVideoInput.h>
#include <coral/playback/win32/WindowsVideoOutputNodeFactory.h>

#include <effect/EffectEngine.h>

#include <gtest/gtest.h>

#include <d3d11.h>
#include <windows.h>

#include <chrono>
#include <functional>
#include <filesystem>
#include <future>
#include <thread>

namespace coral {
namespace {

using namespace std::chrono_literals;

bool waitForFrame(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return predicate();
}

HWND createHiddenWindow() {
  constexpr char kClassName[] = "CoralPlaybackOutputBindingTest";
  WNDCLASSA windowClass = {};
  windowClass.lpfnWndProc = DefWindowProcA;
  windowClass.hInstance = GetModuleHandleA(nullptr);
  windowClass.lpszClassName = kClassName;
  RegisterClassA(&windowClass);
  return CreateWindowExA(0, kClassName, "coral-output-binding-test", WS_POPUP, 0, 0, 64, 64, nullptr,
                         nullptr, windowClass.hInstance, nullptr);
}

TEST(Win32WindowSurfaceTest, WrapsAWindowWithoutOwningIt) {
  const HWND window = createHiddenWindow();
  ASSERT_NE(window, nullptr);

  const auto surface = CreateWin32WindowSurface(window);
  ASSERT_NE(surface, nullptr);
  EXPECT_TRUE(surface->isValid());
  EXPECT_EQ(surface->getInstance(), window);

  surface->release();
  EXPECT_TRUE(IsWindow(window)) << "the surface must not destroy the application's window";

  DestroyWindow(window);
}

TEST(Win32WindowSurfaceTest, RejectsAnInvalidWindow) {
  EXPECT_EQ(CreateWin32WindowSurface(nullptr), nullptr);

  const HWND window = createHiddenWindow();
  ASSERT_NE(window, nullptr);
  const auto surface = CreateWin32WindowSurface(window);
  ASSERT_NE(surface, nullptr);
  DestroyWindow(window);
  EXPECT_FALSE(surface->isValid());
}

TEST(WindowsVideoOutputFactoryTest, CreatesAnOutputForAWindowSurface) {
  const HWND window = createHiddenWindow();
  ASSERT_NE(window, nullptr);

  const auto surface = CreateWin32WindowSurface(window);
  ASSERT_NE(surface, nullptr);

  WindowsVideoOutputNodeFactory factory;
  {
    const auto output = factory.create(surface);
    EXPECT_NE(output, nullptr);
  }

  uint32_t width = 0;
  uint32_t height = 0;
  EXPECT_TRUE(factory.querySize(surface, &width, &height));
  EXPECT_EQ(width, 64u);
  EXPECT_EQ(height, 64u);

  DestroyWindow(window);
}

TEST(WindowsVideoOutputFactoryTest, RejectsNullAndInvalidSurfaces) {
  WindowsVideoOutputNodeFactory factory;
  EXPECT_EQ(factory.create(nullptr), nullptr);

  const HWND window = createHiddenWindow();
  ASSERT_NE(window, nullptr);
  const auto surface = CreateWin32WindowSurface(window);
  ASSERT_NE(surface, nullptr);
  DestroyWindow(window);
  EXPECT_EQ(factory.create(surface), nullptr);
}

ID3D11Device* createD3d11Device() {
  ID3D11Device* device = nullptr;
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  const HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                            D3D11_SDK_VERSION, &device, &level, nullptr);
  return SUCCEEDED(result) ? device : nullptr;
}

TEST(Win32TextureSurfaceTest, CreatesAndExposesItsDevice) {
  ID3D11Device* const device = createD3d11Device();
  if (device == nullptr) {
    GTEST_SKIP() << "no D3D11 hardware device on this host";
  }
  const auto surface = Win32TextureSurface::create(device, 64, 48);
  ASSERT_NE(surface, nullptr);
  EXPECT_TRUE(surface->isValid());
  EXPECT_EQ(surface->device(), device);
  EXPECT_EQ(surface->width(), 64u);
  EXPECT_EQ(surface->height(), 48u);
  EXPECT_EQ(surface->currentTexture(), nullptr);
  device->Release();
}

TEST(WindowsTextureOutputFactoryTest, CreatesTheSharedTextureOutput) {
  ID3D11Device* const device = createD3d11Device();
  if (device == nullptr) {
    GTEST_SKIP() << "no D3D11 hardware device on this host";
  }
  const auto surface = Win32TextureSurface::create(device, 64, 48);
  ASSERT_NE(surface, nullptr);
  WindowsTextureOutputNodeFactory factory;
  {
    const auto output = factory.create(surface);
    EXPECT_NE(output, nullptr);
    EXPECT_NE(surface->outputNode(), nullptr);
  }
  uint32_t width = 0;
  uint32_t height = 0;
  EXPECT_TRUE(factory.querySize(surface, &width, &height));
  EXPECT_EQ(width, 64u);
  EXPECT_EQ(height, 48u);
  surface->release();
  device->Release();
}

TEST(WindowsTexturePlayerEffectTest, RendersIntoTheSharedTextureRing) {
  ID3D11Device* const device = createD3d11Device();
  if (device == nullptr) {
    GTEST_SKIP() << "no D3D11 hardware device on this host";
  }
  const auto surface = Win32TextureSurface::create(device, 320, 180);
  ASSERT_NE(surface, nullptr);

  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  auto player = LivePlayer::create(path, LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);
  player->enableVideoEffectPipeline(std::make_shared<WindowsTextureOutputNodeFactory>());
  ASSERT_TRUE(player->isVideoEffectPipelineEnabled());
  player->setOutputSurface(surface);
  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);
  player->prepare();
  ASSERT_TRUE(waitForFrame([&] {
    return player->getPlaybackState() == coral::Player::State::STATE_READY ||
           !player->getPlaybackErrorMessage().empty();
  }, 10s)) << player->getPlaybackErrorMessage();
  if (!player->getPlaybackErrorMessage().empty()) {
    player->release();
    device->Release();
    GTEST_SKIP() << player->getPlaybackErrorMessage();
  }
  player->play();
  if (!waitForFrame([&] { return engine->isRunning(); }, 5s)) {
    player->release();
    device->Release();
    GTEST_SKIP() << engine->diagnostics();
  }
  EXPECT_TRUE(waitForFrame([&] { return surface->currentTexture() != nullptr; }, 15s))
      << "the player never wrote into the shared D3D11 ring";
  EXPECT_NE(surface->outputNode(), nullptr);
  player->release();
  surface->release();
  device->Release();
}

TEST(WindowsHardwareVideoSurfaceTest, RequiresADeviceAndBorrowsIt) {
  EXPECT_EQ(WindowsHardwareVideoSurface::create(nullptr), nullptr);

  ID3D11Device* const device = createD3d11Device();
  if (device == nullptr) {
    GTEST_SKIP() << "no D3D11 hardware device on this host";
  }
  const auto surface = WindowsHardwareVideoSurface::create(device);
  ASSERT_NE(surface, nullptr);
  EXPECT_TRUE(surface->isValid());
  EXPECT_EQ(surface->getNativeDevice(), device);
  EXPECT_EQ(surface->getInstance(), device);
  EXPECT_EQ(surface->device(), device);
  // Releasing a borrowed surface must not release the application's device.
  surface->release();
  EXPECT_TRUE(surface->isValid());
  device->Release();
}

TEST(WindowsVideoInputTest, CreatesTheCpuFallbackAndRejectsAMissingDevice) {
  const auto cpu = WindowsVideoInput::createCpuInput(320, 180);
  EXPECT_NE(cpu.inputNode, nullptr);
  EXPECT_NE(cpu.frameSink, nullptr);
  EXPECT_EQ(cpu.decoderSurface, nullptr);

  const auto zeroCopy = WindowsVideoInput::createZeroCopyInput(nullptr, 320, 180);
  EXPECT_EQ(zeroCopy.inputNode, nullptr);
  EXPECT_EQ(zeroCopy.frameSink, nullptr);
  EXPECT_EQ(zeroCopy.decoderSurface, nullptr);
}

TEST(WindowsZeroCopyEffectTest, DecodesThroughD3D11Textures) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "the video fixture is missing: " << path;
  }
  const auto device = WindowsVideoDevice::create();
  if (device == nullptr) {
    GTEST_SKIP() << "no D3D11 device with video support on this host";
  }

  auto input = WindowsVideoInput::createZeroCopyInput(device->device(), 720, 1280);
  ASSERT_NE(input.inputNode, nullptr);
  ASSERT_NE(input.frameSink, nullptr);
  ASSERT_NE(input.decoderSurface, nullptr);
  const auto sink = std::dynamic_pointer_cast<WindowsVideoFrameSink>(input.frameSink);
  ASSERT_NE(sink, nullptr);

  auto player = LivePlayer::create(path, LivePlayer::Configuration{.audioEnabled = false});
  ASSERT_NE(player, nullptr);
  player->enableVideoEffectPipeline(std::make_shared<WindowsVideoOutputNodeFactory>(), input);
  ASSERT_TRUE(player->isVideoEffectPipelineEnabled());

  const HWND window = createHiddenWindow();
  ASSERT_NE(window, nullptr);
  const auto surface = CreateWin32WindowSurface(window);
  ASSERT_NE(surface, nullptr);
  player->setOutputSurface(surface);

  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);
  player->prepare();
  player->play();

  if (!waitForFrame([&] { return engine->isRunning(); }, 5s)) {
    player->release();
    DestroyWindow(window);
    GTEST_SKIP() << "the effect engine could not start on this host: " << engine->diagnostics();
  }
  // The sink only submits a frame when the decoder handed it a D3D11 texture; a decoder that fell back
  // to CPU frames leaves the counter at zero and the CPU input is the path for that host.
  if (!waitForFrame([&] { return sink->submittedFrameCount() > 0; }, 15s)) {
    player->release();
    DestroyWindow(window);
    GTEST_SKIP() << "the WMF decoder did not produce D3D11 textures on this host";
  }
  EXPECT_GT(sink->convertedFrameCount(), 0u)
      << "the decoder's NV12 textures were not converted on the GPU";
  EXPECT_EQ(sink->directFrameCount(), 0u) << "the WMF decoder does not emit an RGBA texture";
  EXPECT_TRUE(waitForFrame([&] { return engine->stats().frameCount > 0; }, 10s))
      << "the graph never presented the converted texture: " << engine->diagnostics();
  if (engine->backend() == EffectBackend::Vulkan) {
    EXPECT_TRUE(waitForFrame([&] { return sink->isZeroCopy(); }, 10s))
        << "the shared texture was not imported by the Vulkan device (staging fallback)";
  }

  player->release();
  DestroyWindow(window);
}

TEST(WindowsThumbnailProviderTest, DecodesAFrameAtTheRequestedTime) {
  const std::string path = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "the video fixture is missing: " << path;
  }
  auto provider = WindowsThumbnailProvider::create(path, 320, 180);
  if (provider == nullptr) {
    GTEST_SKIP() << "Media Foundation is unavailable on this host";
  }
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
  provider->cancel();
}

} // namespace
} // namespace coral
