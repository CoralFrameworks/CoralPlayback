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

#ifndef CORAL_PLAYBACK_ANDROID_ANDROID_SURFACE_H
#define CORAL_PLAYBACK_ANDROID_ANDROID_SURFACE_H

#include <codec/MediaCodec.h>

#include <jni.h>

#include <android/native_window.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * A `VideoSurface` over an Android `Surface` / `ANativeWindow`.
 *
 * <p>The wrapper owns one `ANativeWindow` reference. It can be created either from a Java
 * `android.view.Surface` (the usual JNI path) or from a window the caller already acquired.
 */
class AndroidSurface final : public VideoSurface {
public:
  /** Wraps @p surface; takes its own `ANativeWindow` reference. */
  static std::shared_ptr<AndroidSurface> fromSurface(JNIEnv* env, jobject surface);

  /** Wraps @p window; acquires an additional reference. */
  static std::shared_ptr<AndroidSurface> fromNativeWindow(ANativeWindow* window);

  ~AndroidSurface() override;

  AndroidSurface(const AndroidSurface&) = delete;
  AndroidSurface& operator=(const AndroidSurface&) = delete;

  [[nodiscard]] void* getInstance() const override;

  [[nodiscard]] bool isValid() const override;

  void setDefaultBufferSize(int width, int height) override;

  void release() override;

  [[nodiscard]] ANativeWindow* nativeWindow() const noexcept { return mWindow; }

  [[nodiscard]] uint32_t width() const noexcept;

  [[nodiscard]] uint32_t height() const noexcept;

private:
  explicit AndroidSurface(ANativeWindow* window) noexcept;

  ANativeWindow* mWindow = nullptr;
};

} // namespace coral

#endif // CORAL_PLAYBACK_ANDROID_ANDROID_SURFACE_H
