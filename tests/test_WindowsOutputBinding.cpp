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

#include <coral/playback/VideoOutput.h>
#include <coral/playback/win32/Win32WindowSurface.h>

#include "platform/win32/WindowsVideoOutputNodeFactory.h"

#include <gtest/gtest.h>

#include <windows.h>

namespace coral {
namespace {

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

} // namespace
} // namespace coral
