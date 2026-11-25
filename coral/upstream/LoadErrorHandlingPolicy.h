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

#ifndef CORAL_PLAYBACK_UPSTREAM_LOAD_ERROR_HANDLING_POLICY_H
#define CORAL_PLAYBACK_UPSTREAM_LOAD_ERROR_HANDLING_POLICY_H

#include <codec/Error.h>

#include <media/Definitions.h>

#include <cstdint>
#include <string>

namespace coral {

/**
 * The kind of data a load reads.
 *
 * <p>Mirrors ExoPlayer's {@code C.DATA_TYPE_*} constants, with the same values, so that a policy
 * ported from Java keeps its meaning.
 */
enum class DataType : int32_t {
  kUnknown = 0,
  kMedia = 1,
  kMediaInitialization = 2,
  kDrm = 3,
  kManifest = 4,
  kTimeSynchronization = 5,
  kAd = 6,
  kMediaProgressiveLive = 7,
  kCustomBase = 10000,
};

/**
 * Decides how a failed load is retried.
 *
 * <p>This is the counterpart of ExoPlayer's {@code LoadErrorHandlingPolicy}: a media source asks it
 * how long to wait before a retry, and how many attempts a load may take at all. The source that
 * owns the policy reports the outcome to its listeners either way, so a policy only decides whether
 * the failure is worth another attempt.
 */
class LoadErrorHandlingPolicy {
public:
  /** Information about a load that failed, mirroring ExoPlayer's {@code LoadErrorInfo}. */
  struct LoadErrorInfo {
    /** The url that was being read. */
    std::string url;

    /** The number of times the load was attempted, including the attempt that failed. */
    int errorCount = 0;

    /** The failure. */
    Error error;

    /** How long the failed attempt took, in milliseconds. */
    int64_t loadDurationMs = 0;

    LoadErrorInfo() = default;

    LoadErrorInfo(std::string url, const int errorCount, Error error,
        const int64_t loadDurationMs)
        : url(std::move(url)),
          errorCount(errorCount),
          error(std::move(error)),
          loadDurationMs(loadDurationMs) {
    }
  };

  virtual ~LoadErrorHandlingPolicy() = default;

  /**
   * Returns the delay before the load is retried.
   *
   * @param loadErrorInfo Information about the failure.
   *
   * @return The delay in milliseconds, or {@link Definitions::kTimeUnset} when the load must not be
   *         retried at all.
   */
  virtual int64_t getRetryDelayMsFor(const LoadErrorInfo& loadErrorInfo) = 0;

  /**
   * Returns how many times a load of the given kind may be attempted, including the first attempt.
   *
   * @param dataType The kind of data that is loaded.
   */
  virtual int getMinimumLoadableRetryCount(DataType dataType) = 0;

  /**
   * Called once the load has definitely ended, whether it succeeded, failed or was canceled, so
   * that a policy that keeps per load state can release it.
   *
   * @param loadTaskId The identifier of the load, see {@link LoadEventInfo#loadTaskId}.
   */
  virtual void onLoadTaskConcluded(int64_t loadTaskId) {
    static_cast<void>(loadTaskId);
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_UPSTREAM_LOAD_ERROR_HANDLING_POLICY_H
