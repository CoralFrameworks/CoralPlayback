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

#include <coral/playback/android/AndroidSurface.h>

#include <android/native_window_jni.h>

namespace coral {

AndroidSurface::AndroidSurface(ANativeWindow* window) noexcept
    : mWindow(window) {
}

std::shared_ptr<AndroidSurface> AndroidSurface::fromSurface(JNIEnv* env, jobject surface) {
  if (env == nullptr || surface == nullptr) {
    return nullptr;
  }
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<AndroidSurface>(new AndroidSurface(window));
}

std::shared_ptr<AndroidSurface> AndroidSurface::fromNativeWindow(ANativeWindow* window) {
  if (window == nullptr) {
    return nullptr;
  }
  ANativeWindow_acquire(window);
  return std::shared_ptr<AndroidSurface>(new AndroidSurface(window));
}

AndroidSurface::~AndroidSurface() {
  if (mWindow != nullptr) {
    ANativeWindow_release(mWindow);
    mWindow = nullptr;
  }
}

void* AndroidSurface::getInstance() const {
  return mWindow;
}

bool AndroidSurface::isValid() const {
  return mWindow != nullptr;
}

void AndroidSurface::setDefaultBufferSize(const int width, const int height) {
  if (mWindow != nullptr && width > 0 && height > 0) {
    ANativeWindow_setBuffersGeometry(mWindow, width, height, 0);
  }
}

void AndroidSurface::release() {
  if (mWindow != nullptr) {
    ANativeWindow_release(mWindow);
    mWindow = nullptr;
  }
}

uint32_t AndroidSurface::width() const noexcept {
  return mWindow != nullptr ? static_cast<uint32_t>(ANativeWindow_getWidth(mWindow)) : 0;
}

uint32_t AndroidSurface::height() const noexcept {
  return mWindow != nullptr ? static_cast<uint32_t>(ANativeWindow_getHeight(mWindow)) : 0;
}

} // namespace coral
