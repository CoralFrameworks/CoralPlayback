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

#ifndef CORAL_PLAYBACK_SOURCE_FORMAT_HOLDER_H
#define CORAL_PLAYBACK_SOURCE_FORMAT_HOLDER_H

#include <media/Format.h>

#include <memory>

namespace coral {

class FormatHolder {
public:
  FormatHolder() : format() {}

  explicit FormatHolder(const std::shared_ptr<MediaFormat>& format) noexcept
      : format(format) {}

  /**
   * The format, held **strongly**.
   *
   * <p>Mirrors ExoPlayer's {@code FormatHolder.format}, which is a strong reference. A consumer that is
   * given a format owns it until it is given another one, so a format can never be observed as null
   * because the producer that reported it let go of its own reference. Holding a {@code weak_ptr} here
   * made {@code format.lock()} return null, which the renderers dereferenced.
   */
  std::shared_ptr<MediaFormat> format;

  void clear() { format.reset(); }
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_FORMAT_HOLDER_H
