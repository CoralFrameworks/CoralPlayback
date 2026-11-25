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
#ifndef CORAL_PLAYBACK_SOURCE_MEDIA_PERIOD_INFO_H
#define CORAL_PLAYBACK_SOURCE_MEDIA_PERIOD_INFO_H

#include <coral/source/MediaPeriodId.h>

namespace coral {

/** Stores the information required to load and play a {@link MediaPeriod}. */
struct MediaPeriodInfo {

  /** The media period's identifier. */
  MediaPeriodId id;

  /** The start position of the media to play within the media period, in microseconds. */
  int64_t startPositionUs;

  /**
   * The requested next start position for the current period, in microseconds, or {@link
   * Definitions::kTimeUnset} if the period was requested to start at its default position.
   */
  int64_t requestedContentPositionUs;

  /**
   * The end position to which the media period's content is clipped or {@link
   * Definitions::kTimeUnset} if the content is not clipped. The value {@link
   * Definitions::kTimeEndOfSource} indicates that a postroll ad follows at the end of this content
   * media period.
   */
  int64_t endPositionUs;

  /**
   * The duration of the media period, like {@link #endPositionUs} but with {@link
   * Definitions::kTimeEndOfSource} and {@link Definitions::kTimeUnset} resolved to the period
   * duration if known.
   */
  int64_t durationUs;

  /**
   * Whether this is the last media period in its timeline period (e.g., a postroll ad, or a media
   * period corresponding to a timeline period without ads).
   */
  bool isLastInTimelinePeriod;

  /** Whether this is the last media period in its timeline window. */
  bool isLastInTimelineWindow;

  /**
   * Whether this is the last media period in the entire timeline. If true, {@link
   * #isLastInTimelinePeriod} will also be true.
   */
  bool isFinal;

  MediaPeriodInfo()
      : id(),
        startPositionUs(0),
        requestedContentPositionUs(Definitions::kTimeUnset),
        endPositionUs(Definitions::kTimeUnset),
        durationUs(Definitions::kTimeUnset),
        isLastInTimelinePeriod(false),
        isLastInTimelineWindow(false),
        isFinal(false) {}

  explicit MediaPeriodInfo(MediaPeriodId id,
      int64_t startPositionUs,
      int64_t requestedContentPositionUs,
      int64_t endPositionUs,
      int64_t durationUs,
      bool isLastInTimelinePeriod,
      bool isLastInTimelineWindow,
      bool isFinal) noexcept
      : id(std::move(id)),
        startPositionUs(startPositionUs),
        requestedContentPositionUs(requestedContentPositionUs),
        endPositionUs(endPositionUs),
        durationUs(durationUs),
        isLastInTimelinePeriod(isLastInTimelinePeriod),
        isLastInTimelineWindow(isLastInTimelineWindow),
        isFinal(isFinal) {}

  MediaPeriodInfo copyWithStartPositionUs(int64_t newStartPositionUs) {
    if (newStartPositionUs == this->startPositionUs) {
      return *this;
    } else {
      return MediaPeriodInfo(id,
          newStartPositionUs,
          requestedContentPositionUs,
          endPositionUs,
          durationUs,
          isLastInTimelinePeriod,
          isLastInTimelineWindow,
          isFinal);
    }
  }

  MediaPeriodInfo copyWithRequestedContentPositionUs(int64_t newRequestedContentPositionUs) {
    if (requestedContentPositionUs == newRequestedContentPositionUs) {
      return *this;
    }
    return MediaPeriodInfo(id,
        startPositionUs,
        newRequestedContentPositionUs,
        endPositionUs,
        durationUs,
        isLastInTimelinePeriod,
        isLastInTimelineWindow,
        isFinal);
  }

  friend bool operator==(const MediaPeriodInfo& lhs, const MediaPeriodInfo& rhs) {
    return lhs.id == rhs.id
           && lhs.startPositionUs == rhs.startPositionUs
           && lhs.requestedContentPositionUs == rhs.requestedContentPositionUs
           && lhs.endPositionUs == rhs.endPositionUs
           && lhs.durationUs == rhs.durationUs
           && lhs.isLastInTimelinePeriod == rhs.isLastInTimelinePeriod
           && lhs.isLastInTimelineWindow == rhs.isLastInTimelineWindow
           && lhs.isFinal == rhs.isFinal;
  }

  friend bool operator!=(const MediaPeriodInfo& lhs, const MediaPeriodInfo& rhs) {
    return !(lhs == rhs);
  }
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MEDIA_PERIOD_INFO_H
