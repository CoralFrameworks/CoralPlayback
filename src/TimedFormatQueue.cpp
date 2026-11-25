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

#include <coral/playback/TimedFormatQueue.h>

namespace coral {

void TimedFormatQueue::clear() {
  auto position = mTimedQueues.begin();
  while (position != mTimedQueues.end()) {
    position = mTimedQueues.erase(position);
  }
}

void TimedFormatQueue::add(int64_t positionUs, const std::shared_ptr<MediaFormat>& format) {
  mTimedQueues.push_back(std::make_unique<TimedValueHolder>(positionUs, format));
}

std::shared_ptr<MediaFormat>
TimedFormatQueue::pollFloor(int64_t positionUs) {
  std::shared_ptr<MediaFormat> format = nullptr;
  auto previousTimeDiff = INT64_MAX;
  while (!mTimedQueues.empty()) {
    auto timeDiff = positionUs - mTimedQueues.front()->positionUs;
    if (timeDiff < 0 && (-timeDiff >= previousTimeDiff)) {
      break;
    }
    previousTimeDiff = timeDiff;
    format = mTimedQueues.front()->mediaFormat;
    mTimedQueues.pop_front();
  }
  return format;
}

} // namespace coral
