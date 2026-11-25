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

#ifndef CORAL_PLAYBACK_SOURCE_CLIPPING_MEDIA_PERIOD_H
#define CORAL_PLAYBACK_SOURCE_CLIPPING_MEDIA_PERIOD_H

#include <coral/source/MediaPeriod.h>

#include <media/Definitions.h>

#include <memory>
#include <vector>

namespace coral {

class ClippingMediaPeriod final : public MediaPeriod,
                                  public MediaPeriod::Callback,
                                  public std::enable_shared_from_this<ClippingMediaPeriod> {
public:
  explicit ClippingMediaPeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod,
      bool enableInitialDiscontinuity,
      int64_t startPositionUs,
      int64_t endPositionUs) noexcept;

  ~ClippingMediaPeriod() override;

  using Callback = MediaPeriod::Callback;

  /**
   * Updates the clipping start/end times for this source, in microseconds.
   *
   * @param startUs The clipping start time in microseconds.
   * @param endUs The clipping end time in microseconds, or {@link
   * Definitions::kTimeEndOfSource} to indicate the end of the source.
   */
  void updateClipping(int64_t startUs, int64_t endUs);

  void prepare(Callback* callback, int64_t positionUs) override;

  void onPrepared(const std::shared_ptr<MediaPeriod>& source) override;

  const std::shared_ptr<TrackGroupArray>& getTrackGroups() const override;

  int64_t
  selectTracks(const std::vector<std::shared_ptr<TrackSelection>>& selections,
      const std::vector<bool>& mayRetainStreamFlags,
      std::vector<std::shared_ptr<SampleStream>>& streams,
      std::vector<bool>& streamResetFlags,
      int64_t positionUs) override;

  void discardBuffer(int64_t positionUs, bool toKeyframe) override;

  int64_t readDiscontinuity() override;

  int64_t seekToUs(int64_t positionUs) override;

  int64_t
  getAdjustedSeekPositionUs(int64_t positionUs,
      const SeekParameters& seekParameters) override;

  void onContinueLoadingRequested(
      const std::shared_ptr<MediaPeriod>& source) override;

  /**
   * Reports the failure of the clipped period, which is the one that owns it.
   *
   * @throws MediaPeriodException If the clipped period cannot be prepared.
   */
  void maybeThrowPrepareError() override;

  /**
   * Returns the failure of the clipped period, or an empty string while it can be prepared.
   */
  [[nodiscard]] std::string getPrepareError() const override;

  int64_t getBufferedPositionUs() override;

  int64_t getNextLoadPositionUs() override;

  bool continueLoading(int64_t positionUs) override;

  bool isLoading() override;

  void reevaluateBuffer(int64_t positionUs) override;

  bool isPendingInitialDiscontinuity() const {
    return pendingInitialDiscontinuityPositionUs != Definitions::kTimeUnset;
  }

  /** Drops the child reference after the source has released it. */
  void releaseChildPeriod();
  const std::shared_ptr<MediaPeriod>& getMediaPeriod() const {
    return mediaPeriod;
  }

private:
  SeekParameters clipSeekParameters(int64_t positionUs,
      const SeekParameters& seekParameters) const;

  static bool shouldKeepInitialDiscontinuity(
      int64_t startUs,
      const std::vector<std::shared_ptr<TrackSelection>>& selections);

  /**
   * Wraps a {@link SampleStream} and clips its samples.
   */
  class ClippingSampleStream final : public SampleStream {
  private:
    friend class ClippingMediaPeriod;

    ClippingMediaPeriod* const mediaSource;
    std::shared_ptr<SampleStream> childStream;
    bool sentEos;

  public:
    explicit ClippingSampleStream(ClippingMediaPeriod* mediaSource,
        const std::shared_ptr<SampleStream>& stream) noexcept
        : mediaSource(mediaSource),
          childStream(stream),
          sentEos(false) {}

    void clearSentEos() { sentEos = false; }

    [[nodiscard]] MediaType getMediaType() const override {
      return childStream->getMediaType();
    }

    [[nodiscard]] bool isReady() const override {
      return !mediaSource->isPendingInitialDiscontinuity() &&
             childStream->isReady();
    }

    ReadDataResult
    readData(FormatHolder& formatHolder, AVPacketBuffer* buffer, bool formatRequired) override;

    int skipData(int64_t positionUs) override {
      if (mediaSource->isPendingInitialDiscontinuity()) {
        return Definitions::kResultNothingRead;
      }
      return childStream->skipData(positionUs);
    }
  };

  /**
   * The {@link MediaPeriod} wrapped by this clipping media period.
   */
  std::shared_ptr<MediaPeriod> mediaPeriod;

  MediaPeriod::Callback* mCallback;
  std::vector<std::shared_ptr<ClippingSampleStream>> sampleStreams;

  int64_t pendingInitialDiscontinuityPositionUs;
  int64_t startPositionUs;
  int64_t endPositionUs;
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_CLIPPING_MEDIA_PERIOD_H
