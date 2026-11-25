/*
 * Copyright (C) 2023 The Coral Project
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
#ifndef CORAL_PLAYBACK_SOURCE_LOAD_EVENT_INFO_H
#define CORAL_PLAYBACK_SOURCE_LOAD_EVENT_INFO_H

#include <unordered_map>
#include <string>

namespace coral {

/** {@link MediaSource} load event information. */
struct LoadEventInfo {

  /** Identifies the load task to which this event corresponds. */
  int64_t loadTaskId;
//
//  /** Defines the requested data. */
//  DataSpec dataSpec;

  /**
   * The url string from which data is being read. The uri will be identical to the one in {@link
   * #dataSpec}.uri unless redirection has occurred. If redirection has occurred, this is the uri
   * after redirection.
   */
  std::string url;

  /** The response headers associated with the load, or an empty map if unavailable. */
  std::unordered_map<std::string, std::vector<std::string>> responseHeaders;

  /** The value of {@link SystemClock#elapsedRealtime} at the time of the load event. */
  int64_t elapsedRealtimeMs;

  /** The duration of the load up to the event time. */
  int64_t loadDurationMs;

  /** The number of bytes that were loaded up to the event time. */
  int64_t bytesLoaded;

  LoadEventInfo()
      : loadTaskId(0),
        elapsedRealtimeMs(0),
        loadDurationMs(0),
        bytesLoaded(0) {
  }

  LoadEventInfo(const int64_t loadTaskId,
      std::string url,
      std::unordered_map<std::string, std::vector<std::string>> responseHeaders,
      const int64_t elapsedRealtimeMs,
      const int64_t loadDurationMs,
      const int64_t bytesLoaded) noexcept
      : loadTaskId(loadTaskId),
        url(std::move(url)),
        responseHeaders(std::move(responseHeaders)),
        elapsedRealtimeMs(elapsedRealtimeMs),
        loadDurationMs(loadDurationMs),
        bytesLoaded(bytesLoaded) {
  }

  LoadEventInfo(const LoadEventInfo& other) = default;
  LoadEventInfo& operator=(const LoadEventInfo& other) = default;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_LOAD_EVENT_INFO_H
