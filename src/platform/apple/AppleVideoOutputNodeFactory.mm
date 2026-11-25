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

#include <coral/playback/apple/AppleVideoOutputNodeFactory.h>

#include <coral/playback/apple/AppleIOSurface.h>

#include <effect/platform/apple/AppleIOSurfaceOutputNode.h>

#include <memory>
#include <utility>

namespace coral {

std::shared_ptr<EffectOutputNode> AppleVideoOutputNodeFactory::create(
    const std::shared_ptr<VideoSurface>& surface) {
  const auto appleSurface = std::dynamic_pointer_cast<AppleIOSurface>(surface);
  if (appleSurface == nullptr || !appleSurface->isValid()) {
    return nullptr;
  }
  return AppleIOSurfaceOutputNode::create(appleSurface->iosurface(), appleSurface->width(),
                                          appleSurface->height(), appleSurface->format());
}

bool AppleVideoOutputNodeFactory::querySize(const std::shared_ptr<VideoSurface>& surface,
                                            uint32_t* width, uint32_t* height) {
  if (surface == nullptr || width == nullptr || height == nullptr) {
    return false;
  }
  const auto appleSurface = std::dynamic_pointer_cast<AppleIOSurface>(surface);
  if (appleSurface == nullptr || !appleSurface->isValid()) {
    return false;
  }
  *width = appleSurface->width();
  *height = appleSurface->height();
  return *width > 0 && *height > 0;
}

} // namespace coral
