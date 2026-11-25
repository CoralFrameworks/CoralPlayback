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
#ifndef CORAL_PLAYBACK_SOURCE_TIMELINE_H
#define CORAL_PLAYBACK_SOURCE_TIMELINE_H

#include <coral/source/MediaItem.h>
#include <coral/source/MediaPeriodId.h>
#include <coral/source/RepeatMode.h>

#include <memory>
#include <optional>

namespace coral {

class Timeline {
public:
  virtual ~Timeline() = default;

  struct Window {
    /**
     * A {@link #uid} for a window that must be used for single-window {@link Timeline Timelines}.
     */
    static const PeriodUid& getSingleWindowUid();

    /**
     * A unique identifier for the window. Single-window {@link Timeline Timelines} must use {@link
     * #getSingleWindowUid}.
     */
    std::optional<PeriodUid> uid;

    /** The {@link MediaItem} associated to the window. Not necessarily unique. */
    std::shared_ptr<MediaItem> mediaItem;

    /**
     * The start time of the presentation to which this window belongs in milliseconds since the
     * Unix epoch, or {@link Definitions::kTimeUnset} if unknown or not applicable.
     * For informational purposes only.
     */
    int64_t presentationStartTimeMs;

    /**
     * The window's start time in milliseconds since the Unix epoch, or {@link
     * Definitions::kTimeUnset} if unknown or not applicable. For informational purposes only.
     */
    int64_t windowStartTimeMs;

    /**
     * The offset between {@link SystemClock#elapsedRealtime()} and the time since the Unix epoch
     * according to the clock of the media origin server, or {@link Definitions::kTimeUnset} if
     * unknown or not applicable.
     *
     * Note that the current Unix time can be retrieved using {@link #getCurrentUnixTimeMs()} and
     * is calculated as {@code SystemClock.elapsedRealtime() + elapsedRealtimeEpochOffsetMs}.
     */
    int64_t elapsedRealtimeEpochOffsetMs;

    /** Whether it's possible to seek within this window. */
    bool isSeekable;

    /** Whether this window may change when the timeline is updated. */
    bool isDynamic;

    /**
     * Whether the media in this window is live. For informational purposes only.
     *
     * Check {@link #isDynamic} to know whether this window may still change.
     */
    bool isLive;

    /**
     * Whether this window contains placeholder information because the real information has yet to
     * be loaded.
     */
    bool isPlaceholder;

    /** The index of the first period that belongs to this window. */
    int firstPeriodIndex;

    /**
     * The index of the last period that belongs to this window.
     */
    int lastPeriodIndex;

    /**
     * The default position relative to the start of the window at which to begin playback, in
     * microseconds. May be {@link Definitions::kTimeUnset} if and only if the window was populated
     * with a non-zero default position projection, and if the specified projection cannot be
     * performed whilst remaining within the bounds of the window.
     */
    int64_t defaultPositionUs;

    /**
     * The duration of this window in microseconds, or {@link Definitions::kTimeUnset} if unknown.
     */
    int64_t durationUs;

    /**
     * The position of the start of this window relative to the start of the first period belonging
     * to it, in microseconds.
     */
    int64_t positionInFirstPeriodUs;

    Window();

    /** Sets the data held by this window. */
    void set(const std::optional<PeriodUid>& uid,
        const std::shared_ptr<MediaItem>& mediaItem,
        int64_t presentationStartTimeMs,
        int64_t windowStartTimeMs,
        int64_t elapsedRealtimeEpochOffsetMs,
        bool isSeekable,
        bool isDynamic,
        bool isLive,
        int64_t defaultPositionUs,
        int64_t durationUs,
        int firstPeriodIndex,
        int lastPeriodIndex,
        int64_t positionInFirstPeriodUs);

    /**
     * Returns the default position relative to the start of the window at which to begin playback,
     * in milliseconds. May be {@link C#TIME_UNSET} if and only if the window was populated with a
     * non-zero default position projection, and if the specified projection cannot be performed
     * whilst remaining within the bounds of the window.
     */
    [[nodiscard]] int64_t getDefaultPositionMs() const { return defaultPositionUs / 1000; }

    /**
     * Returns the default position relative to the start of the window at which to begin playback,
     * in microseconds. May be {@link C#TIME_UNSET} if and only if the window was populated with a
     * non-zero default position projection, and if the specified projection cannot be performed
     * whilst remaining within the bounds of the window.
     */
    [[nodiscard]] int64_t getDefaultPositionUs() const { return defaultPositionUs; }

    /**
     * Returns the duration of this window in microseconds, or {@link C#TIME_UNSET} if unknown.
     */
    [[nodiscard]] int64_t getDurationUs() const { return durationUs; }

    /**
     * Returns the position of the start of this window relative to the start of the first period
     * belonging to it, in microseconds.
     */
    [[nodiscard]] int64_t getPositionInFirstPeriodUs() const { return positionInFirstPeriodUs; }
  };

  struct Period {
    /**
     * An identifier for the period. Not necessarily unique. May be null if the ids of the period
     * are not required.
     */
    std::optional<PeriodUid> id;

    /**
     * A unique identifier for the period. May be null if the ids of the period are not required.
     */
    std::optional<PeriodUid> uid;

    /**
     * The index of the window to which this period belongs.
     */
    int windowIndex;

    /**
     * The duration of this period in microseconds, or {@link Definitions::kTimeUnset} if unknown.
     */
    int64_t durationUs;

    int64_t positionInWindowUs;

    Period();

    /**
     * Sets the data held by this period.
     *
     * @param newId An identifier for the period. Not necessarily unique. May be null if the ids of
     *     the period are not required.
     * @param newUid A unique identifier for the period. May be null if the ids of the period are
     *     not required.
     * @param newWindowIndex The index of the window to which this period belongs.
     * @param newDurationUs The duration of this period in microseconds, or {@link
     *     Definitions::kTimeUnset} if unknown.
     * @param newPositionInWindowUs The position of the start of this period relative to the start
     *     of the window to which it belongs, in milliseconds. May be negative if the start of the
     *     period is not within the window.
     */
    void set(std::optional<PeriodUid> newId,
        std::optional<PeriodUid> newUid,
        int newWindowIndex,
        int64_t newDurationUs,
        int64_t newPositionInWindowUs) {
      this->id = std::move(newId);
      this->uid = std::move(newUid);
      this->windowIndex = newWindowIndex;
      this->durationUs = newDurationUs;
      this->positionInWindowUs = newPositionInWindowUs;
    }

    /**
     * Returns the duration of this period in microseconds, or {@link Definitions::kTimeUnset} if
     * unknown.
     */
    [[nodiscard]] int64_t getDurationUs() const {
      return durationUs;
    }

    /**
     * Returns the position of the start of this period relative to the start of the window to
     * which it belongs, in microseconds. May be negative if the start of the period is not within
     * the window.
     */
    [[nodiscard]] int64_t getPositionInWindowUs() const {
      return positionInWindowUs;
    }
  };

  /**
   * Returns whether the timeline is empty.
   */
  [[nodiscard]] bool isEmpty() const {
    return getWindowCount() == 0;
  }

  /**
   * Returns the number of windows in the timeline.
   */
  [[nodiscard]] virtual int getWindowCount() const = 0;

  /**
   * Returns the index of the window after the window at index {@code windowIndex} depending on the
   * {@code repeatMode} and whether shuffling is enabled.
   *
   * @param windowIndex Index of a window in the timeline.
   * @param repeatMode A repeat mode.
   * @param shuffleModeEnabled Whether shuffling is enabled.
   * @return The index of the next window, or {@link Definitions::kIndexUnset} if this is the last
   *     window.
   */
  virtual int getNextWindowIndex(int windowIndex,
      RepeatMode repeatMode,
      bool shuffleModeEnabled);

  /**
   * Returns the index of the window before the window at index {@code windowIndex} depending on the
   * {@code repeatMode} and whether shuffling is enabled.
   *
   * @param windowIndex Index of a window in the timeline.
   * @param repeatMode A repeat mode.
   * @param shuffleModeEnabled Whether shuffling is enabled.
   * @return The index of the previous window, or {@link Definitions::kIndexUnset} if this is the
   *     first window.
   */
  virtual int getPreviousWindowIndex(int windowIndex,
      RepeatMode repeatMode,
      bool shuffleModeEnabled);

  /**
   * Returns the index of the last window in the playback order depending on whether shuffling is
   * enabled.
   *
   * @param shuffleModeEnabled Whether shuffling is enabled.
   * @return The index of the last window in the playback order, or {@link Definitions::kIndexUnset}
   *     if the timeline is empty.
   */
  virtual int getLastWindowIndex(bool shuffleModeEnabled);

  /**
   * Returns the index of the first window in the playback order depending on whether shuffling is
   * enabled.
   *
   * @param shuffleModeEnabled Whether shuffling is enabled.
   * @return The index of the first window in the playback order, or {@link Definitions::kIndexUnset}
   *     if the timeline is empty.
   */
  virtual int getFirstWindowIndex(bool shuffleModeEnabled);

  /**
   * Populates a {@link Window} with data for the window at the specified index.
   *
   * @param windowIndex The index of the window.
   * @param window The {@link Window} to populate. Must not be null.
   * @return The populated {@link Window}, for convenience.
   */
  std::shared_ptr<Window>
  getWindow(int windowIndex, const std::shared_ptr<Window>& window) {
    return getWindow(windowIndex, window, /* defaultPositionProjectionUs= */ 0);
  }

  /**
   * Populates a {@link Window} with data for the window at the specified index.
   *
   * @param windowIndex The index of the window.
   * @param window The {@link Window} to populate. Must not be null.
   * @param defaultPositionProjectionUs A duration into the future that the populated window's
   *     default start position should be projected.
   * @return The populated {@link Window}, for convenience.
   */
  virtual std::shared_ptr<Window>
  getWindow(int windowIndex,
      const std::shared_ptr<Window>& window,
      int64_t defaultPositionProjectionUs) = 0;

  /**
   * Returns the number of periods in the timeline.
   */
  [[nodiscard]] virtual int getPeriodCount() const = 0;

  /**
   * Returns the index of the period after the period at index {@code periodIndex} depending on the
   * {@code repeatMode} and whether shuffling is enabled.
   *
   * @param periodIndex Index of a period in the timeline.
   * @param period A {@link Period} to be used internally. Must not be null.
   * @param window A {@link Window} to be used internally. Must not be null.
   * @param repeatMode A repeat mode.
   * @param shuffleModeEnabled Whether shuffling is enabled.
   * @return The index of the next period, or {@link Definitions::kIndexUnset} if this is the last
   *     period.
   */
  int getNextPeriodIndex(int periodIndex,
      const std::shared_ptr<Period>& period,
      const std::shared_ptr<Window>& window,
      RepeatMode repeatMode,
      bool shuffleModeEnabled);

  /**
   * Returns whether the given period is the last period of the timeline depending on the
   * {@code repeatMode} and whether shuffling is enabled.
   *
   * @param periodIndex A period index.
   * @param period A {@link Period} to be used internally. Must not be null.
   * @param window A {@link Window} to be used internally. Must not be null.
   * @param repeatMode A repeat mode.
   * @param shuffleModeEnabled Whether shuffling is enabled.
   * @return Whether the period of the given index is the last period of the timeline.
   */
  bool isLastPeriod(int periodIndex,
      const std::shared_ptr<Period>& period,
      const std::shared_ptr<Window>& window,
      RepeatMode repeatMode,
      bool shuffleModeEnabled);

  /**
   * Calls {@link #getPeriodPosition(Window, Period, int, long, long)} with a zero default position
   * projection.
   */
  std::pair<PeriodUid, int64_t>
  getPeriodPosition(const std::shared_ptr<Window>& window,
      const std::shared_ptr<Period>& period,
      int windowIndex,
      int64_t windowPositionUs) {
    return getPeriodPosition(window, period, windowIndex, windowPositionUs, 0);
  }

  /**
   * Converts (windowIndex, windowPositionUs) to the corresponding (periodUid, periodPositionUs).
   *
   * @param window A {@link Window} that may be overwritten.
   * @param period A {@link Period} that may be overwritten.
   * @param windowIndex The window index.
   * @param windowPositionUs The window time, or {@link C#TIME_UNSET} to use the window's default
   *     start position.
   * @param defaultPositionProjectionUs If {@code windowPositionUs} is {@link C#TIME_UNSET}, the
   *     duration into the future by which the window's position should be projected.
   * @return The corresponding (periodUid, periodPositionUs), or null if {@code #windowPositionUs}
   *     is {@link C#TIME_UNSET}, {@code defaultPositionProjectionUs} is non-zero, and the window's
   *     position could not be projected by {@code defaultPositionProjectionUs}.
   */
  std::pair<PeriodUid, int64_t>
  getPeriodPosition(const std::shared_ptr<Window>& window,
      const std::shared_ptr<Period>& period,
      int windowIndex,
      int64_t windowPositionUs,
      int64_t defaultPositionProjectionUs);

  /**
   * Populates a {@link Period} with data for the period with the specified unique identifier.
   *
   * @param periodUid The unique identifier of the period.
   * @param period The {@link Period} to populate. Must not be null.
   * @return The populated {@link Period}, for convenience.
   */
  virtual std::shared_ptr<Period>
  getPeriodByUid(const std::optional<PeriodUid>& periodUid, const std::shared_ptr<Period>& period) {
    return getPeriod(getIndexOfPeriod(periodUid), period, true);
  }

  /**
   * Populates a {@link Period} with data for the period at the specified index. {@link Period#id}
   * and {@link Period#uid} will be set to null.
   *
   * @param periodIndex The index of the period.
   * @param period The {@link Period} to populate. Must not be null.
   * @return The populated {@link Period}, for convenience.
   */
  std::shared_ptr<Period>
  getPeriod(int periodIndex, const std::shared_ptr<Period>& period) {
    return getPeriod(periodIndex, period, false);
  }

  /**
   * Populates a {@link Period} with data for the period at the specified index.
   *
   * @param periodIndex The index of the period.
   * @param period The {@link Period} to populate. Must not be null.
   * @param setIds Whether {@link Period#id} and {@link Period#uid} should be populated. If false,
   *     the fields will be set to null. The caller should pass false for efficiency reasons unless
   *     the fields are required.
   * @return The populated {@link Period}, for convenience.
   */
  virtual std::shared_ptr<Period>
  getPeriod(int periodIndex, const std::shared_ptr<Period>& period, bool setIds) = 0;

  /**
   * Returns the index of the period identified by its unique {@link Period#uid}, or {@link
   * Definitions::kIndexUnset} if the period is not in the timeline.
   *
   * @param uid A unique identifier for a period.
   * @return The index of the period, or {@link Definitions::kIndexUnset} if the period was not found.
   */
  virtual int getIndexOfPeriod(const std::optional<PeriodUid>& uid) = 0;

  /**
   * Returns the unique id of the period identified by its index in the timeline.
   *
   * @param periodIndex The index of the period.
   * @return The unique id of the period.
   */
  virtual std::optional<PeriodUid> getUidOfPeriod(int periodIndex) = 0;
};

class EmptyTimeline final : public Timeline {
public:
  EmptyTimeline() = default;
  ~EmptyTimeline() override = default;

  int getWindowCount() const override {
    return 0;
  }

  std::shared_ptr<Window>
  getWindow(int windowIndex,
      const std::shared_ptr<Window>& window,
      int64_t defaultPositionProjectionUs) override {
    return nullptr;
  }

  int getPeriodCount() const override {
    return 0;
  }

  std::shared_ptr<Period>
  getPeriod(int periodIndex, const std::shared_ptr<Period>& period, bool setIds) override {
    return nullptr;
  }

  int getIndexOfPeriod(const std::optional<PeriodUid>& uid) override {
    return -1;
  }

  std::optional<PeriodUid> getUidOfPeriod(int periodIndex) override {
    return std::nullopt;
  }
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_TIMELINE_H
