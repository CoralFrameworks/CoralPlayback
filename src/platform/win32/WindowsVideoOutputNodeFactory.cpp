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

#include "WindowsVideoOutputNodeFactory.h"

#if defined(_WIN32)

#include <coral/playback/win32/Win32WindowSurface.h>

#include <effect/platform/win/WindowsSwapChainOutputNode.h>

namespace coral {

std::shared_ptr<EffectOutputNode> WindowsVideoOutputNodeFactory::create(
    const std::shared_ptr<VideoSurface>& surface) {
  if (surface == nullptr || !surface->isValid()) {
    return nullptr;
  }
  auto* const window = static_cast<HWND>(surface->getInstance());
  if (window == nullptr || ::IsWindow(window) == FALSE) {
    return nullptr;
  }
  return WindowsSwapChainOutputNode::create(window);
}

bool WindowsVideoOutputNodeFactory::querySize(
    const std::shared_ptr<VideoSurface>& surface, uint32_t* width, uint32_t* height) {
  if (surface == nullptr || !surface->isValid() || width == nullptr || height == nullptr) {
    return false;
  }
  auto* const window = static_cast<HWND>(surface->getInstance());
  if (window == nullptr || ::IsWindow(window) == FALSE) {
    return false;
  }
  RECT client = {};
  if (::GetClientRect(window, &client) == FALSE) {
    return false;
  }
  const LONG clientWidth = client.right - client.left;
  const LONG clientHeight = client.bottom - client.top;
  if (clientWidth <= 0 || clientHeight <= 0) {
    return false;
  }
  *width = static_cast<uint32_t>(clientWidth);
  *height = static_cast<uint32_t>(clientHeight);
  return true;
}

} // namespace coral

#endif // _WIN32
