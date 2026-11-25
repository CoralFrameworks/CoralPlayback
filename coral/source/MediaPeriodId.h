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
#ifndef CORAL_PLAYBACK_SOURCE_MEDIA_PERIOD_ID_H
#define CORAL_PLAYBACK_SOURCE_MEDIA_PERIOD_ID_H

#include <media/Definitions.h>

#include <string>
#include <utility>

namespace coral {

class PeriodUid final {
private:
  std::string id_;
  std::string subId_;

public:
  PeriodUid() : id_() {}

  explicit PeriodUid(const std::string& id) noexcept
      : id_(id),
        subId_(id) {}

  explicit PeriodUid(std::string id, std::string subId) noexcept
      : id_(std::move(id)),
        subId_(std::move(subId)) {}

  virtual ~PeriodUid() = default;

  PeriodUid(const PeriodUid& other) = default;
  PeriodUid& operator=(const PeriodUid& other) = default;

  [[nodiscard]] const std::string& id() const { return id_; }

  [[nodiscard]] const std::string& subId() const { return subId_; }

  [[nodiscard]] std::string toString() const {
    return (subId_ == id_) ? "{ id: " + id_ + "}"
                           : "{ id: " + id_ + ", subId: " + subId_ + "}";
  }

  bool operator==(const PeriodUid& other) const {
    return id_ == other.id_ && subId_ == other.subId_;
  }

  bool operator!=(const PeriodUid& other) const {
    return !(*this == other);
  }
};

/** Identifier for a {@link MediaPeriod}. */
class MediaPeriodId final {
private:
  /** The unique id of the period. */
  PeriodUid periodUid_;

  /**
   * The sequence number of the window in the buffered sequence of windows this media period is
   * part of. {@link Definitions::kIndexUnset} if the media period id is not part of a buffered
   * sequence of windows.
   */
  int64_t windowSequenceNumber_;

public:
  MediaPeriodId() : periodUid_(), windowSequenceNumber_(Definitions::kIndexUnset) {}

  explicit MediaPeriodId(const PeriodUid& periodUid) noexcept
      : MediaPeriodId(periodUid, Definitions::kIndexUnset) {}

  explicit MediaPeriodId(const PeriodUid& periodUid, int64_t windowSequenceNumber) noexcept
      : periodUid_(periodUid),
        windowSequenceNumber_(windowSequenceNumber) {}

  [[nodiscard]] const PeriodUid& periodUid() const { return periodUid_; }

  [[nodiscard]] int64_t windowSequenceNumber() const { return windowSequenceNumber_; }

  /** Returns a copy of this period identifier but with {@code newPeriodUid} as its period uid. */
  MediaPeriodId copyWithPeriodUid(const PeriodUid& newPeriodUid) const {
    if (periodUid_ == newPeriodUid) {
      return *this;
    }
    // The sequence number identifies the window this period belongs to for the lifetime of the
    // playlist, so it is preserved: the sources that remap the uid of a period, e.g. to mask it,
    // must not lose it.
    return MediaPeriodId(newPeriodUid, windowSequenceNumber_);
  }

  MediaPeriodId(const MediaPeriodId& other) = default;
  MediaPeriodId(MediaPeriodId&& other) = default;
  MediaPeriodId& operator=(const MediaPeriodId& other) = default;
  MediaPeriodId& operator=(MediaPeriodId&& other) = default;

  friend bool operator==(const MediaPeriodId& lhs, const MediaPeriodId& rhs) {
    return lhs.periodUid_ == rhs.periodUid_;
  }

  friend bool operator!=(const MediaPeriodId& lhs, const MediaPeriodId& rhs) {
    return !(lhs == rhs);
  }
};

} // namespace coral

namespace std {

template<>
struct hash<coral::PeriodUid> {
  size_t operator()(const coral::PeriodUid& uid) const noexcept {
    return std::hash<std::string>()(uid.id());
  }
};

} // namespace std

#endif //CORAL_PLAYBACK_SOURCE_MEDIA_PERIOD_ID_H
