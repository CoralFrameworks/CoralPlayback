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

#ifndef CORAL_PLAYBACK_UPSTREAM_DEFAULT_LOAD_ERROR_HANDLING_POLICY_H
#define CORAL_PLAYBACK_UPSTREAM_DEFAULT_LOAD_ERROR_HANDLING_POLICY_H

#include <coral/upstream/LoadErrorHandlingPolicy.h>

namespace coral {

/**
 * The {@link LoadErrorHandlingPolicy} a source uses when the application does not provide one.
 *
 * <p>This is the counterpart of ExoPlayer's {@code DefaultLoadErrorHandlingPolicy}: it retries a
 * load a bounded number of times, waiting longer after every failure, so that a source that is
 * briefly unavailable, e.g. because the network dropped, recovers without the application having to
 * do anything.
 */
class DefaultLoadErrorHandlingPolicy final : public LoadErrorHandlingPolicy {
public:
  /**
   * The number of attempts a media load may take. ExoPlayer also exports a larger count for a live
   * progressive stream, which a source that reads one can pass to the constructor.
   */
  static constexpr int DEFAULT_MIN_LOADABLE_RETRY_COUNT = 3;

  /** The number of attempts ExoPlayer allows a live progressive stream. */
  static constexpr int DEFAULT_MIN_LOADABLE_RETRY_COUNT_LIVE = 6;

  /** The shortest delay between two attempts, in milliseconds. */
  static constexpr int64_t DEFAULT_MIN_RETRY_DELAY_MS = 1000;

  /** The longest delay between two attempts, in milliseconds. */
  static constexpr int64_t DEFAULT_MAX_RETRY_DELAY_MS = 5000;

  /**
   * @param minimumLoadableRetryCount The number of attempts a load may take, including the first.
   */
  explicit DefaultLoadErrorHandlingPolicy(
      int minimumLoadableRetryCount = DEFAULT_MIN_LOADABLE_RETRY_COUNT) noexcept;

  ~DefaultLoadErrorHandlingPolicy() override = default;

  /**
   * Returns a delay that grows by {@link #DEFAULT_MIN_RETRY_DELAY_MS} per failed attempt and is
   * capped at {@link #DEFAULT_MAX_RETRY_DELAY_MS}.
   */
  int64_t getRetryDelayMsFor(const LoadErrorInfo& loadErrorInfo) override;

  int getMinimumLoadableRetryCount(DataType dataType) override;

private:
  int minimumLoadableRetryCount;
};

} // namespace coral

#endif // CORAL_PLAYBACK_UPSTREAM_DEFAULT_LOAD_ERROR_HANDLING_POLICY_H
