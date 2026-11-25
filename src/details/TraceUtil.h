/*
 * Copyright (C) 2022 The Coral Project
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

#ifndef CORAL_PLAYBACK_DETAILS_TRACE_UTIL_H
#define CORAL_PLAYBACK_DETAILS_TRACE_UTIL_H

#include <utils/Systrace.h>

namespace coral {

/**
 * Calls through to {@link android.os.Trace} methods on supported API levels.
 */
class TraceUtil final {
public:
  static void beginSection(const char* sectionName) {
    SYSTRACE_CONTEXT();
    SYSTRACE_NAME_BEGIN(sectionName);
  }

  static void endSection() {
    SYSTRACE_CONTEXT();
    SYSTRACE_NAME_END();
  }

private:
  TraceUtil() = default;
  ~TraceUtil() = default;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_TRACE_UTIL_H
