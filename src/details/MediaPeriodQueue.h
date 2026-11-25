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
#ifndef CORAL_PLAYBACK_DETAILS_MEDIA_PERIOD_QUEUE_H
#define CORAL_PLAYBACK_DETAILS_MEDIA_PERIOD_QUEUE_H

#include "MediaPeriodHolder.h"

#include <coral/playback/PlaybackInfo.h>
#include <coral/source/Timeline.h>

#include <memory>
#include <optional>

namespace coral {

/**
 * Holds a queue of media periods, from the currently playing media period at the front to the
 * loading media period at the end of the queue, with methods for controlling loading and updating
 * the queue. Also has a reference to the media period currently being read.
 */
class MediaPeriodQueue final {
private:
  /**
   * Limits the maximum number of periods to buffer ahead of the current playing period. The
   * buffering policy normally prevents buffering too far ahead, but the policy could allow too m
   * any small periods to be buffered if the period count were not limited.
   */
  static constexpr int MAXIMUM_BUFFER_AHEAD_PERIODS = 100;

  std::shared_ptr<Timeline::Period> period;
  std::shared_ptr<Timeline::Window> window;

  MediaPeriodHolder::Factory* mediaPeriodHolderFactory;

  int64_t nextWindowSequenceNumber;
  RepeatMode repeatMode;
  bool shuffleModeEnabled;
  std::shared_ptr<MediaPeriodHolder> playing;
  std::shared_ptr<MediaPeriodHolder> reading;
  std::shared_ptr<MediaPeriodHolder> loading;
  size_t length;
  std::optional<PeriodUid> oldFrontPeriodUid;
  int64_t oldFrontPeriodWindowSequenceNumber;

public:
  explicit MediaPeriodQueue(MediaPeriodHolder::Factory* factory) noexcept;

  /**
   * Sets the {@link RepeatMode} and returns whether the repeat mode change has been fully handled.
   * If not, it is necessary to seek to the current playback position.
   *
   * @param timeline The current timeline.
   * @param repeatMode The new repeat mode.
   * @return Whether the repeat mode change has been fully handled.
   */
  bool updateRepeatMode(const std::shared_ptr<Timeline>& timeline,
    RepeatMode repeatMode);

  /**
   * Sets whether shuffling is enabled and returns whether the shuffle mode change has been fully
   * handled. If not, it is necessary to seek to the current playback position.
   *
   * @param timeline The current timeline.
   * @param shuffleModeEnabled Whether shuffling mode is enabled.
   * @return Whether the shuffle mode change has been fully handled.
   */
  bool updateShuffleModeEnabled(const std::shared_ptr<Timeline>& timeline,
    bool shuffleModeEnabled);

  /** Returns whether {@code mediaPeriod} is the current loading media period. */
  [[nodiscard]] bool isLoading(const std::shared_ptr<MediaPeriod>& mediaPeriod) const;

  /**
   * If there is a loading period, reevaluates its buffer.
   *
   * @param rendererPositionUs The current renderer position.
   */
  void reevaluateBuffer(int64_t rendererPositionUs);

  /** Returns whether a new loading media period should be enqueued, if available. */
  bool shouldLoadNextMediaPeriod();

  /**
   * Returns the {@link MediaPeriodInfo} for the next media period to load.
   *
   * @param rendererPositionUs The current renderer position.
   * @param playbackInfo The current playback information.
   * @return The {@link MediaPeriodInfo} for the next media period to load, or {@code null} if not
   *     yet known.
   */
  std::optional<MediaPeriodInfo>
  getNextMediaPeriodInfo(int64_t rendererPositionUs, const PlaybackInfo& playbackInfo);

  /**
   * Enqueues a new media period holder based on the specified information as the new loading media
   * period, and returns it.
   *
   * @param info Information used to identify this media period in its period.
   */
  const std::shared_ptr<MediaPeriodHolder>&
  enqueueNextMediaPeriodHolder(const MediaPeriodInfo& info);

  /**
   * Returns the loading period holder which is at the end of the queue, or null if the queue is
   * empty.
   */
  [[nodiscard]] const std::shared_ptr<MediaPeriodHolder>& getLoadingPeriod() const {
    return loading;
  }

  /**
   * Returns the playing period holder which is at the front of the queue, or null if the queue is
   * empty.
   */
  [[nodiscard]] const std::shared_ptr<MediaPeriodHolder>& getPlayingPeriod() const {
    return playing;
  }

  /** Returns the reading period holder, or null if the queue is empty. */
  [[nodiscard]] const std::shared_ptr<MediaPeriodHolder>& getReadingPeriod() const {
    return reading;
  }

  /**
   * Continues reading from the next period holder in the queue.
   *
   * @return The updated reading period holder.
   */
  const std::shared_ptr<MediaPeriodHolder>& advanceReadingPeriod();

  /**
   * Dequeues the playing period holder from the front of the queue and advances the playing period
   * holder to be the next item in the queue.
   *
   * @return The updated playing period holder, or null if the queue is or becomes empty.
   */
  const std::shared_ptr<MediaPeriodHolder>& advancePlayingPeriod();

  /**
   * Removes all period holders after the given period holder. This process may also remove the
   * currently reading period holder. If that is the case, the reading period holder is set to be
   * the same as the playing period holder at the front of the queue.
   *
   * @param mediaPeriodHolder The media period holder that shall be the new end of the queue.
   * @return Whether the reading period has been removed.
   */
  bool removeAfter(const std::shared_ptr<MediaPeriodHolder>& mediaPeriodHolder);

  /** Clears the queue. */
  void clear();

  /**
   * Updates media periods in the queue to take into account the latest timeline, and returns
   * whether the timeline change has been fully handled. If not, it is necessary to seek to the
   * current playback position. The method assumes that the first media period in the queue is still
   * consistent with the new timeline.
   *
   * @param timeline The new timeline.
   * @param rendererPositionUs The current renderer position in microseconds.
   * @param maxRendererReadPositionUs The maximum renderer position up to which renderers have read
   *     the current reading media period in microseconds, or {@link Definitions::kTimeEndOfSource}
   *     if they have read to the end.
   * @return Whether the timeline change has been handled completely.
   */
  bool updateQueuedPeriods(const std::shared_ptr<Timeline>& timeline,
    int64_t rendererPositionUs,
    int64_t maxRendererReadPositionUs);

  /**
   * Returns new media period info based on specified {@code mediaPeriodInfo} but taking into
   * account the current timeline. This method must only be called if the period is still part of
   * the current timeline.
   *
   * @param timeline The current timeline used to update the media period.
   * @param info Media period info for a media period based on an old timeline.
   * @return The updated media period info for the current timeline.
   */
  std::optional<MediaPeriodInfo>
  getUpdatedMediaPeriodInfo(const std::shared_ptr<Timeline>& timeline,
    const MediaPeriodInfo& info);

  /**
   * Resolves the specified timeline period and position to a {@link MediaPeriodId} that should be
   * played, returning an identifier for an identifier for a content media period if not.
   *
   * @param timeline The timeline the period is part of.
   * @param periodUid The uid of the timeline period to play.
   * @param positionUs The next content position in the period to play.
   * @return The identifier for the first media period to play.
   */
  MediaPeriodId
  resolveMediaPeriodId(const std::shared_ptr<Timeline>& timeline,
    const PeriodUid& periodUid,
    int64_t positionUs);

private:
  void notifyQueueUpdate();

  int64_t resolvePeriodIndexToWindowSequenceNumber(
    const std::shared_ptr<Timeline>& timeline, const PeriodUid& periodUid);

  /**
   * Returns whether a period described by {@code oldInfo} can be kept for playing the media period
   * described by {@code newInfo}.
   */
  static bool
  canKeepMediaPeriodHolder(const MediaPeriodInfo& oldInfo,
    const MediaPeriodInfo& newInfo);

  /**
   * Returns whether a duration change of a period is compatible with keeping the following periods.
   */
  static bool areDurationsCompatible(int64_t previousDurationUs, int64_t newDurationUs);

  /**
   * Updates the queue for any playback mode change, and returns whether the change was fully
   * handled. If not, it is necessary to seek to the current playback position.
   */
  bool updateForPlaybackModeChange(const std::shared_ptr<Timeline>& timeline);

  /**
   * Returns the first {@link MediaPeriodInfo} to play, based on the specified playback position.
   */
  std::optional<MediaPeriodInfo> getFirstMediaPeriodInfo(const PlaybackInfo& playbackInfo);

  /**
   * Returns the {@link MediaPeriodInfo} for the media period following {@code mediaPeriodHolder}'s
   * media period.
   *
   * @param timeline The current timeline.
   * @param mediaPeriodHolder The media period holder.
   * @param rendererPositionUs The current renderer position in microseconds.
   * @return The following media period's info, or {@code null} if it is not yet possible to get the
   *     next media period info.
   */
  std::optional<MediaPeriodInfo>
  getFollowingMediaPeriodInfo(const std::shared_ptr<Timeline>& timeline,
    const std::shared_ptr<MediaPeriodHolder>& mediaPeriodHolder,
    int64_t rendererPositionUs);

  std::optional<MediaPeriodInfo>
  getMediaPeriodInfo(const std::shared_ptr<Timeline>& timeline,
    const MediaPeriodId& id,
    int64_t requestedContentPositionUs,
    int64_t startPositionUs);

  std::optional<MediaPeriodInfo>
  getMediaPeriodInfoForContent(const std::shared_ptr<Timeline>& timeline,
    const PeriodUid& id,
    int64_t startPositionUs,
    int64_t requestedContentPositionUs,
    int64_t windowSequenceNumber);

  bool isLastInWindow(const std::shared_ptr<Timeline>& timeline,
    const MediaPeriodId& id);

  bool isLastInTimeline(const std::shared_ptr<Timeline>& timeline,
    const MediaPeriodId& id,
    bool isLastMediaPeriodInPeriod);
};

} // namespace coral

#endif //CORAL_PLAYBACK_DETAILS_MEDIA_PERIOD_QUEUE_H
