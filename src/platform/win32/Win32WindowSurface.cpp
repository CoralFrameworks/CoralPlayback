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

#include <coral/playback/win32/Win32WindowSurface.h>

#if defined(_WIN32)

namespace coral {

Win32WindowSurface::Win32WindowSurface(HWND window) noexcept
    : mWindow(window) {
}

void* Win32WindowSurface::getInstance() const {
  return mWindow;
}

bool Win32WindowSurface::isValid() const {
  return mWindow != nullptr && ::IsWindow(mWindow) != FALSE;
}

void Win32WindowSurface::release() {
  // The application owns the HWND; see the note in the header.
}

std::shared_ptr<VideoSurface> CreateWin32WindowSurface(HWND window) {
  if (window == nullptr || ::IsWindow(window) == FALSE) {
    return nullptr;
  }
  return std::make_shared<Win32WindowSurface>(window);
}

} // namespace coral

#endif // _WIN32
