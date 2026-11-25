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

#include <coral/playback/android/AndroidJniSurface.h>

#include "AndroidVideoFrameSink.h"

#include <effect/platform/android/AndroidSurfaceTextureInputNode.h>


namespace coral {

VideoEffectInput AndroidVideoInput::createSurfaceTextureInput(
    JNIEnv* env, jobject surfaceTexture, const uint32_t textureName, const uint32_t width,
    const uint32_t height) {
  VideoEffectInput input;
  if (env == nullptr || surfaceTexture == nullptr || textureName == 0 || width == 0 || height == 0) {
    return input;
  }

  const auto node = AndroidSurfaceTextureInputNode::create(env, surfaceTexture, textureName, width,
                                                           height);
  if (node == nullptr) {
    return input;
  }

  // MediaCodec wants the Java Surface object, not an ANativeWindow, so the decoder surface wraps a
  // Surface created from the application's SurfaceTexture.
  jclass surfaceClass = env->FindClass("android/view/Surface");
  if (surfaceClass == nullptr) {
    return input;
  }
  const jmethodID constructor = env->GetMethodID(
      surfaceClass, "<init>", "(Landroid/graphics/SurfaceTexture;)V");
  jobject javaSurface = constructor != nullptr
                          ? env->NewObject(surfaceClass, constructor, surfaceTexture)
                          : nullptr;
  const auto decoderSurface = javaSurface != nullptr
                                ? AndroidJniSurface::fromSurface(env, javaSurface)
                                : nullptr;
  if (javaSurface != nullptr) {
    env->DeleteLocalRef(javaSurface);
  }
  env->DeleteLocalRef(surfaceClass);
  if (decoderSurface == nullptr) {
    return input;
  }

  input.inputNode = node;
  input.decoderSurface = decoderSurface;
  input.frameSink = std::make_shared<AndroidSurfaceTextureFrameSink>();
  return input;
}

} // namespace coral
