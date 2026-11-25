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

#ifndef CORAL_PLAYBACK_ANDROID_ANDROID_VIDEO_INPUT_H
#define CORAL_PLAYBACK_ANDROID_ANDROID_VIDEO_INPUT_H

#include <effect/EffectInputNode.h>

#include <jni.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * Creates the zero-copy input side of the Android effect graph.
 *
 * <p>The application owns the `SurfaceTexture` and the OES texture; this entry point only adapts
 * them to an `AndroidSurfaceTextureInputNode`. The caller must create both on an EGL context in
 * the engine's share group, as required by that node.
 */
class AndroidVideoInput {
public:
  static std::shared_ptr<EffectInputNode> createSurfaceTextureInput(
      JNIEnv* env, jobject surfaceTexture, uint32_t textureName, uint32_t width, uint32_t height);
};

} // namespace coral

#endif // CORAL_PLAYBACK_ANDROID_ANDROID_VIDEO_INPUT_H
