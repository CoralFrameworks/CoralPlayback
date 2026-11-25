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

#include <coral/playback/android/AndroidJniSurface.h>

namespace coral {

AndroidJniSurface::AndroidJniSurface(JavaVM* vm, jobject surface) noexcept
    : mVm(vm),
      mSurface(surface) {
}

std::shared_ptr<AndroidJniSurface> AndroidJniSurface::fromSurface(JNIEnv* env, jobject surface) {
  if (env == nullptr || surface == nullptr) {
    return nullptr;
  }
  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != JNI_OK || vm == nullptr) {
    return nullptr;
  }
  jobject global = env->NewGlobalRef(surface);
  if (global == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<AndroidJniSurface>(new AndroidJniSurface(vm, global));
}

AndroidJniSurface::~AndroidJniSurface() {
  releaseReference();
}

void* AndroidJniSurface::getInstance() const {
  return mSurface;
}

bool AndroidJniSurface::isValid() const {
  return mSurface != nullptr;
}

void AndroidJniSurface::release() {
  releaseReference();
}

void AndroidJniSurface::releaseReference() {
  if (mSurface == nullptr || mVm == nullptr) {
    return;
  }
  JNIEnv* env = nullptr;
  bool attached = false;
  if (mVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
    if (mVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
      attached = true;
    }
  }
  if (env != nullptr) {
    env->DeleteGlobalRef(mSurface);
  }
  mSurface = nullptr;
  if (attached) {
    mVm->DetachCurrentThread();
  }
}

} // namespace coral
