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

#include <coral/playback/android/AndroidVideoInput.h>

#include <effect/platform/android/AndroidSurfaceTextureInputNode.h>

namespace coral {

std::shared_ptr<EffectInputNode> AndroidVideoInput::createSurfaceTextureInput(
    JNIEnv* env, jobject surfaceTexture, const uint32_t textureName, const uint32_t width,
    const uint32_t height) {
  if (env == nullptr || surfaceTexture == nullptr || textureName == 0 || width == 0 || height == 0) {
    return nullptr;
  }
  return AndroidSurfaceTextureInputNode::create(env, surfaceTexture, textureName, width, height);
}

} // namespace coral
