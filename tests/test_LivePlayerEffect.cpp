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
#include <coral/playback/win32/Win32WindowSurface.h>

#include "platform/win32/WindowsVideoOutputNodeFactory.h"

#include <effect/EffectEngine.h>

#include <gtest/gtest.h>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace coral {
namespace {

using namespace std::chrono_literals;

HWND createHiddenWindow(const int width, const int height) {
  constexpr char kClassName[] = "CoralLivePlayerEffectTest";
  WNDCLASSA windowClass = {};
  windowClass.lpfnWndProc = DefWindowProcA;
  windowClass.hInstance = GetModuleHandleA(nullptr);
  windowClass.lpszClassName = kClassName;
  RegisterClassA(&windowClass);
  return CreateWindowExA(0, kClassName, "coral-live-player-effect", WS_POPUP, 0, 0, width, height,
                         nullptr, nullptr, windowClass.hInstance, nullptr);
}

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

TEST(LivePlayerEffectTest, RendersARealVideoThroughTheWindowsOutput) {  const std::string path =
      std::string(CORAL_ASSETS_DIR) + "/video/android-screens-lavf-56.36.100-aac-avc-main-1280x720.mkv";
  auto player = LivePlayer::create(path);
  ASSERT_NE(player, nullptr);
  player->enableVideoEffectPipeline(std::make_shared<WindowsVideoOutputNodeFactory>());
  ASSERT_TRUE(player->isVideoEffectPipelineEnabled());

  const HWND window = createHiddenWindow(320, 180);
  ASSERT_NE(window, nullptr);
  const auto surface = CreateWin32WindowSurface(window);
  ASSERT_NE(surface, nullptr);
  player->setOutputSurface(surface);

  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);

  player->prepare();
  player->play();

  if (!waitFor([&] { return engine->isRunning(); }, 5s)) {
    player->release();
    DestroyWindow(window);
    GTEST_SKIP() << "the effect engine could not start on this host: " << engine->diagnostics();
  }

  EXPECT_TRUE(waitFor([&] { return engine->stats().frameCount > 0; }, 15s))
      << "the player never presented an effect frame: " << engine->diagnostics();

  player->release();
  DestroyWindow(window);
}

TEST(LivePlayerEffectTest, RendersSubtitlesThroughTheGpuOverlay) {
  const std::string videoPath =
      std::string(CORAL_ASSETS_DIR) + "/video/android-screens-lavf-56.36.100-aac-avc-main-1280x720.mkv";
  const std::string subtitlePath = std::string(CORAL_ASSETS_DIR) + "/subtitle/sample.srt";

  auto player = LivePlayer::create(videoPath);
  ASSERT_NE(player, nullptr);
  player->enableVideoEffectPipeline(std::make_shared<WindowsVideoOutputNodeFactory>());
  player->setSubtitleFile(subtitlePath);

  const HWND window = createHiddenWindow(320, 180);
  ASSERT_NE(window, nullptr);
  const auto surface = CreateWin32WindowSurface(window);
  ASSERT_NE(surface, nullptr);
  player->setOutputSurface(surface);

  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);

  player->prepare();
  player->play();

  if (!waitFor([&] { return engine->isRunning(); }, 5s)) {
    player->release();
    DestroyWindow(window);
    GTEST_SKIP() << "the effect engine could not start on this host: " << engine->diagnostics();
  }

  EXPECT_TRUE(waitFor([&] { return player->isSubtitleOverlayActive(); }, 10s))
      << "the GPU subtitle pass never became active: " << engine->diagnostics();

  player->release();
  DestroyWindow(window);
}

TEST(LivePlayerEffectTest, CompositesAPreRenderedLottieOverlay) {
  const std::string videoPath =
      std::string(CORAL_ASSETS_DIR) + "/video/android-screens-lavf-56.36.100-aac-avc-main-1280x720.mkv";
  const std::filesystem::path lottiePath =
      std::filesystem::path(CORAL_GRAPHICS_RESOURCE_DIR) / "apitest" / "lottie" / "gear.json";

  std::ifstream lottieFile(lottiePath, std::ios::binary);
  std::ostringstream lottieJson;
  lottieJson << lottieFile.rdbuf();

  auto player = LivePlayer::create(videoPath);
  ASSERT_NE(player, nullptr);
  player->enableVideoEffectPipeline(std::make_shared<WindowsVideoOutputNodeFactory>());

  LottieOverlayConfig overlay;
  overlay.json = lottieJson.str();
  overlay.renderScale = 0.25f;
  overlay.maxPreRenderFrames = 400;
  ASSERT_TRUE(player->addLottieOverlay(overlay));

  const HWND window = createHiddenWindow(320, 180);
  ASSERT_NE(window, nullptr);
  const auto surface = CreateWin32WindowSurface(window);
  ASSERT_NE(surface, nullptr);
  player->setOutputSurface(surface);

  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);
  player->prepare();
  player->play();

  if (!waitFor([&] { return engine->isRunning(); }, 5s)) {
    player->release();
    DestroyWindow(window);
    GTEST_SKIP() << "the effect engine could not start on this host: " << engine->diagnostics();
  }

  EXPECT_TRUE(waitFor([&] { return engine->stats().frameCount > 0; }, 15s))
      << "the Lottie graph never presented a frame: " << engine->diagnostics();

  player->clearLottieOverlays();
  player->release();
  DestroyWindow(window);
}

TEST(LivePlayerEffectTest, RendersAnImageThroughTheSameGraph) {
  const std::string imagePath = std::string(CORAL_ASSETS_DIR) + "/image/image.png";
  auto player = LivePlayer::create(imagePath);
  ASSERT_NE(player, nullptr);
  player->enableVideoEffectPipeline(std::make_shared<WindowsVideoOutputNodeFactory>());

  const HWND window = createHiddenWindow(320, 180);
  ASSERT_NE(window, nullptr);
  const auto surface = CreateWin32WindowSurface(window);
  ASSERT_NE(surface, nullptr);
  player->setOutputSurface(surface);

  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);
  player->prepare();
  player->play();

  if (!waitFor([&] { return engine->isRunning(); }, 5s)) {
    player->release();
    DestroyWindow(window);
    GTEST_SKIP() << "the effect engine could not start on this host: " << engine->diagnostics();
  }

  EXPECT_TRUE(waitFor([&] { return engine->stats().frameCount > 0; }, 15s))
      << "the image never reached the effect output: " << engine->diagnostics();

  player->release();
  DestroyWindow(window);
}

TEST(LivePlayerEffectTest, AnImageHonoursItsExplicitDuration) {
  const std::string imagePath = std::string(CORAL_ASSETS_DIR) + "/image/image.png";
  auto item = MediaItem::fromUrl(imagePath, {}, 300);
  auto player = LivePlayer::create(item);
  ASSERT_NE(player, nullptr);
  player->enableVideoEffectPipeline(std::make_shared<WindowsVideoOutputNodeFactory>());

  const HWND window = createHiddenWindow(320, 180);
  ASSERT_NE(window, nullptr);
  const auto surface = CreateWin32WindowSurface(window);
  ASSERT_NE(surface, nullptr);
  player->setOutputSurface(surface);

  const auto engine = player->getEffectEngine();
  ASSERT_NE(engine, nullptr);
  player->prepare();
  player->play();

  EXPECT_TRUE(waitFor([&] { return engine->stats().frameCount > 0; }, 5s))
      << "the image never reached the effect output: " << engine->diagnostics();
  EXPECT_TRUE(waitFor(
      [&] { return player->getPlaybackState() == Player::State::STATE_ENDED; }, 5s))
      << "the image item did not end after its explicit duration; duration="
      << player->getDurationUs() << " position=" << player->getCurrentPositionUs()
      << " state=" << static_cast<int>(player->getPlaybackState());

  player->release();
  DestroyWindow(window);
}

} // namespace
} // namespace coral
