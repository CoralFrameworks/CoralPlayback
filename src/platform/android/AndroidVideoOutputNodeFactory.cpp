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

#include <coral/playback/android/AndroidVideoOutputNodeFactory.h>

#include <coral/playback/android/AndroidSurface.h>

#include <effect/platform/android/AndroidSurfaceOutputNode.h>

#include <utility>

namespace coral {

std::shared_ptr<EffectOutputNode> AndroidVideoOutputNodeFactory::create(
    const std::shared_ptr<VideoSurface>& surface) {
  const auto androidSurface = std::dynamic_pointer_cast<AndroidSurface>(surface);
  if (androidSurface == nullptr || !androidSurface->isValid()) {
    return nullptr;
  }
  return AndroidSurfaceOutputNode::create(androidSurface->nativeWindow());
}

bool AndroidVideoOutputNodeFactory::querySize(const std::shared_ptr<VideoSurface>& surface,
                                              uint32_t* width,
                                              uint32_t* height) {
  if (surface == nullptr || width == nullptr || height == nullptr) {
    return false;
  }
  const auto androidSurface = std::dynamic_pointer_cast<AndroidSurface>(surface);
  if (androidSurface == nullptr || !androidSurface->isValid()) {
    return false;
  }
  *width = androidSurface->width();
  *height = androidSurface->height();
  return *width > 0 && *height > 0;
}

} // namespace coral
