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
#ifndef CORAL_PLAYBACK_SOURCE_MASKING_MEDIA_PERIOD_H
#define CORAL_PLAYBACK_SOURCE_MASKING_MEDIA_PERIOD_H

#include <coral/source/MediaPeriod.h>
#include <coral/source/MediaSource.h>

#include <codec/Error.h>

#include <memory>

namespace coral {

/**
 * Media period that wraps a media source and defers calling its {@link
 * MediaSource#createPeriod(MediaPeriodId, Allocator, long)} method until {@link
 * #createPeriod(MediaPeriodId)} has been called. This is useful if you need to return a media
 * period immediately but the media source that should create it is not yet prepared.
 */
class MaskingMediaPeriod final : public MediaPeriod,
                                 public MediaPeriod::Callback,
                                 public std::enable_shared_from_this<MaskingMediaPeriod> {
public:
  using Callback = MediaPeriod::Callback;

  class PrepareListener {
  public:
    virtual ~PrepareListener() = default;

    /** Called when preparing the media period completes. */
    virtual void onPrepareComplete(const MediaPeriodId& mediaPeriodId) = 0;

    /**
     * Called the first time an error occurs while refreshing source info or preparing the period.
     */
    virtual void onPrepareError(const MediaPeriodId& mediaPeriodId, const Error* error) = 0;
  };

  explicit MaskingMediaPeriod(const std::shared_ptr<MediaSource>& mediaSource,
      MediaPeriodId id,
      const std::shared_ptr<Allocator>& allocator,
      int64_t preparePositionUs) noexcept;

  ~MaskingMediaPeriod() override = default;

  /**
   * Sets a listener for preparation events.
   *
   * @param listener An listener to be notified of media period preparation events. If a listener is
   *     set, {@link #maybeThrowPrepareError()} will not throw but will instead pass the first
   *     preparation error (if any) to the listener.
   */
  void setPrepareListener(PrepareListener* listener);

  /** Returns the position at which the masking media period was prepared, in microseconds. */
  int64_t getPreparePositionUs() const {
    return preparePositionUs;
  }

  /**
   * Overrides the default prepare position at which to prepare the media period. This method must
   * be called before {@link #createPeriod(MediaPeriodId)}.
   *
   * @param preparePositionUs The default prepare position to use, in microseconds.
   */
  void overridePreparePositionUs(int64_t newPositionUs) {
    preparePositionOverrideUs = newPositionUs;
  }

  /** Returns the prepare position override set by {@link #overridePreparePositionUs(int64_t)}. */
  [[nodiscard]] int64_t getPreparePositionOverrideUs() const { return preparePositionOverrideUs; }

  /**
   * Calls {@link MediaSource#createPeriod(MediaPeriodId, Allocator, long)} on the wrapped source
   * then prepares it if {@link #prepare(Callback, long)} has been called. Call {@link
   * #releasePeriod()} to release the period.
   *
   * @param id The identifier that should be used to create the media period from the media source.
   */
  void createPeriod(const MediaPeriodId& id);

  /**
   * Releases the period.
   */
  void releasePeriod();

  void prepare(MediaPeriod::Callback* callback, int64_t positionUs) override;

  const std::shared_ptr<TrackGroupArray>& getTrackGroups() const override {
    return mediaPeriod->getTrackGroups();
  }

  int64_t selectTracks(const std::vector<std::shared_ptr<TrackSelection>>& selections,
      const std::vector<bool>& mayRetainStreamFlags,
      std::vector<std::shared_ptr<SampleStream>>& streams,
      std::vector<bool>& streamResetFlags,
      int64_t positionUs) override;

  void discardBuffer(int64_t positionUs, bool toKeyframe) override;

  int64_t readDiscontinuity() override;

  int64_t getBufferedPositionUs() override;

  int64_t seekToUs(int64_t positionUs) override;

  int64_t getAdjustedSeekPositionUs(int64_t positionUs,
      const SeekParameters& seekParameters) override;

  int64_t getNextLoadPositionUs() override;

  void reevaluateBuffer(int64_t positionUs) override;

  bool continueLoading(int64_t positionUs) override;

  bool isLoading() override {
    return mediaPeriod != nullptr && mediaPeriod->isLoading();
  }

  void onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& period) override;

  /**
   * Reports the failure of the wrapped period, which is the one that owns it.
   *
   * @throws MediaPeriodException If the wrapped period cannot be prepared.
   */
  void maybeThrowPrepareError() override;

  /**
   * Returns the failure of the wrapped period, or an empty string while it can be prepared.
   */
  [[nodiscard]] std::string getPrepareError() const override;

  void onPrepared(const std::shared_ptr<MediaPeriod>& mediaPeriod) override;

  [[nodiscard]] const MediaPeriodId& getId() const { return id; }

private:
  friend class MaskingMediaSource;

  int64_t getPreparePositionWithOverride(int64_t newPreparePositionUs);

  /** The {@link MediaSource} which will create the actual media period. */
  std::shared_ptr<MediaSource> mediaSource;
  /** The {@link MediaPeriodId} used to create the masking media period. */
  MediaPeriodId id;

  std::shared_ptr<Allocator> allocator;
  std::shared_ptr<MediaPeriod> mediaPeriod;
  Callback* callback;
  int64_t preparePositionUs;
  PrepareListener* listener;
  int64_t preparePositionOverrideUs;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_MASKING_MEDIA_PERIOD_H
