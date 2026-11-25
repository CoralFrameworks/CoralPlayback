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

#include <coral/upstream/DefaultLoadErrorHandlingPolicy.h>

#include <algorithm>

namespace coral {

DefaultLoadErrorHandlingPolicy::DefaultLoadErrorHandlingPolicy(
    const int minimumLoadableRetryCount) noexcept
    : minimumLoadableRetryCount(std::max(0, minimumLoadableRetryCount)) {
}

int64_t DefaultLoadErrorHandlingPolicy::getRetryDelayMsFor(
    const LoadErrorInfo& loadErrorInfo) {
  // The first retry is immediate, and every further one waits a second longer, up to the cap. This
  // is exactly ExoPlayer's DefaultLoadErrorHandlingPolicy.
  const auto delayMs =
      static_cast<int64_t>(std::max(0, loadErrorInfo.errorCount - 1)) * DEFAULT_MIN_RETRY_DELAY_MS;
  return std::min(delayMs, DEFAULT_MAX_RETRY_DELAY_MS);
}

int DefaultLoadErrorHandlingPolicy::getMinimumLoadableRetryCount(const DataType dataType) {
  static_cast<void>(dataType);
  return minimumLoadableRetryCount;
}

} // namespace coral
