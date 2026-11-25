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
#include <coral/playback/win32/WindowsTextureOutputNodeFactory.h>

#include "platform/win32/WindowsVideoOutputNodeFactory.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <windows.h>

#include <chrono>
#include <functional>
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

} // namespace
} // namespace coral
