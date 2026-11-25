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

#include <coral/playback/VideoFrameSink.h>

#include <jni.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * Creates the zero-copy input side of the Android effect graph.
 *
 * <p>This is the platform bridge for `LivePlayer::enableVideoEffectPipeline`: it creates the
 * `AndroidSurfaceTextureInputNode`, wraps a Java `Surface` over the application's `SurfaceTexture` as
 * the decoder surface, and attaches the pull-model `AndroidSurfaceTextureFrameSink`. The caller owns
 * the `SurfaceTexture` and the OES texture; both must be created on an EGL context in the engine's
 * share group. The caller also fills `VideoEffectInput::sharedContext` with that `EGLContext`.
 */
class AndroidVideoInput {
public:
  /**
   * Creates the aggregated input for a decoder that renders into @p surfaceTexture.
   *
   * @param env            JNI environment of the calling thread.
   * @param surfaceTexture a `android.graphics.SurfaceTexture`, non-null.
   * @param textureName    the `GL_TEXTURE_EXTERNAL_OES` name the SurfaceTexture updates.
   * @param width          frame width in pixels.
   * @param height         frame height in pixels.
   * @return the input node, decoder surface and sink; null members when the arguments or the Java
   *         `Surface` could not be created.
   */
  static VideoEffectInput createSurfaceTextureInput(JNIEnv* env, jobject surfaceTexture,
                                                    uint32_t textureName, uint32_t width,
                                                    uint32_t height);
};

} // namespace coral

#endif // CORAL_PLAYBACK_ANDROID_ANDROID_VIDEO_INPUT_H
