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

#ifndef CORAL_PLAYBACK_ANDROID_ANDROID_JNI_SURFACE_H
#define CORAL_PLAYBACK_ANDROID_ANDROID_JNI_SURFACE_H

#include <codec/MediaCodec.h>

#include <jni.h>

#include <memory>

namespace coral {

/**
 * A decoder-facing `VideoSurface` that owns a Java `android.view.Surface` global reference.
 *
 * <p>`MediaCodec.configure` expects the `jobject` of the surface, not an `ANativeWindow`, so the
 * decoder path cannot use `AndroidSurface` (which wraps the window for the renderer). This class is
 * the JNI reference owner for a decoder output: a `Surface` created from a `SurfaceTexture`, for
 * example.
 */
class AndroidJniSurface final : public VideoSurface {
public:
  /** Wraps @p surface in a new JNI global reference. */
  static std::shared_ptr<AndroidJniSurface> fromSurface(JNIEnv* env, jobject surface);

  ~AndroidJniSurface() override;

  AndroidJniSurface(const AndroidJniSurface&) = delete;
  AndroidJniSurface& operator=(const AndroidJniSurface&) = delete;

  [[nodiscard]] void* getInstance() const override;

  [[nodiscard]] bool isValid() const override;

  /** The effect graph's SurfaceTexture input samples the decoder's frames. */
  [[nodiscard]] bool isSampledByConsumer() const override { return true; }

  void release() override;

private:
  AndroidJniSurface(JavaVM* vm, jobject surface) noexcept;

  void releaseReference();

  JavaVM* mVm = nullptr;
  jobject mSurface = nullptr;
};

} // namespace coral

#endif // CORAL_PLAYBACK_ANDROID_ANDROID_JNI_SURFACE_H
