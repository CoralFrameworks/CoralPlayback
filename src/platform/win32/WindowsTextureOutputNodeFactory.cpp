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

#include <coral/playback/win32/WindowsTextureOutputNodeFactory.h>

#if defined(_WIN32)

#include <coral/playback/win32/Win32TextureSurface.h>

#include <effect/platform/win/WindowsD3D11EncoderOutputNode.h>

namespace coral {

std::shared_ptr<EffectOutputNode> WindowsTextureOutputNodeFactory::create(
    const std::shared_ptr<VideoSurface>& surface) {
  const auto textureSurface = std::dynamic_pointer_cast<Win32TextureSurface>(surface);
  if (textureSurface == nullptr || !textureSurface->isValid()) {
    return nullptr;
  }
  auto node = WindowsD3D11EncoderOutputNode::create(
      textureSurface->device(), textureSurface->width(), textureSurface->height());
  if (node == nullptr) {
    return nullptr;
  }
  textureSurface->setOutputNode(node);
  return node;
}

bool WindowsTextureOutputNodeFactory::querySize(const std::shared_ptr<VideoSurface>& surface,
                                                uint32_t* width, uint32_t* height) {
  const auto textureSurface = std::dynamic_pointer_cast<Win32TextureSurface>(surface);
  if (textureSurface == nullptr || !textureSurface->isValid() || width == nullptr ||
      height == nullptr) {
    return false;
  }
  *width = textureSurface->width();
  *height = textureSurface->height();
  return true;
}

} // namespace coral

#endif // defined(_WIN32)
