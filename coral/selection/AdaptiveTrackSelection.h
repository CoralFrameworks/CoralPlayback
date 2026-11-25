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

#ifndef CORAL_PLAYBACK_SELECTION_ADAPTIVE_TRACK_SELECTION_H
#define CORAL_PLAYBACK_SELECTION_ADAPTIVE_TRACK_SELECTION_H

#include <coral/selection/BaseTrackSelection.h>

namespace coral {

/**
 * A bandwidth based adaptive {@link TrackSelection}, whose selected track is
 * updated to be the one of highest quality given the current network conditions
 * and the state of the buffer.
 */
class AdaptiveTrackSelection final : public BaseTrackSelection {
private:
  /** Provides the allocated bandwidth. */
  class BandwidthProvider {
  public:
    virtual ~BandwidthProvider() = default;

    /**
     * Returns the allocated bitrate.
     */
    virtual int64_t getAllocatedBandwidth() = 0;
  };

  class DefaultBandwidthProvider final : public BandwidthProvider {
  private:
    std::shared_ptr<BandwidthMeter> bandwidthMeter;
    float const bandwidthFraction;
    int64_t const reservedBandwidth;
    std::vector<std::vector<int64_t>> allocationCheckpoints;

  public:
    explicit DefaultBandwidthProvider(
        const std::shared_ptr<BandwidthMeter>& bandwidthMeter,
        const float bandwidthFraction,
        const int64_t reservedBandwidth) noexcept;

    int64_t getAllocatedBandwidth() override;

    void experimentalSetBandwidthAllocationCheckpoints(
        const std::vector<std::vector<int64_t>>& allocationCheckpoints);
  };

  std::unique_ptr<DefaultBandwidthProvider> bandwidthProvider;
  int64_t const minDurationForQualityIncreaseUs;
  int64_t const maxDurationForQualityDecreaseUs;
  int64_t const minDurationToRetainAfterDiscardUs;
  float const bufferedFractionToLiveEdgeForQualityIncrease;
  float playbackSpeed;
  int selectedIndex;
  int64_t lastBufferEvaluationMs;

  /**
   * Whether the queue has not been evaluated yet.
   *
   * <p>Mirrors ExoPlayer's {@code pendingInitialEvaluation}: the first evaluation is not rate limited,
   * which also keeps the unset {@link #lastBufferEvaluationMs} from being subtracted.
   */
  bool pendingInitialEvaluation = true;

  /** Whether the playback is running, i.e. whether a switch may be applied right away. */
  bool playWhenReady = false;

  /**
   * The time until which each track of the selection is excluded, or 0 when it is not.
   *
   * <p>Mirrors ExoPlayer's {@code blacklistUntilMs}, and is guarded by the selection being used from
   * one thread, like the rest of the selection.
   */
  std::vector<int64_t> blacklistUntilMs;

public:
  static constexpr int DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS = 10000;

  static constexpr int DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS = 25000;

  static constexpr int DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS = 25000;

  static constexpr float DEFAULT_BANDWIDTH_FRACTION = 0.7f;

  static constexpr float
      DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE = 0.75f;

  class Factory final : public TrackSelection::Factory {
  private:
    int const minDurationForQualityIncreaseMs;
    int const maxDurationForQualityDecreaseMs;
    int const minDurationToRetainAfterDiscardMs;
    float const bandwidthFraction;
    float const bufferedFractionToLiveEdgeForQualityIncrease;

  public:
    Factory() : Factory(DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS,
        DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS,
        DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS,
        DEFAULT_BANDWIDTH_FRACTION) {}

    explicit Factory(int minDurationForQualityIncreaseMs,
        int maxDurationForQualityDecreaseMs,
        int minDurationToRetainAfterDiscardMs,
        float bandwidthFraction) noexcept
        : Factory(minDurationForQualityIncreaseMs,
        maxDurationForQualityDecreaseMs,
        minDurationToRetainAfterDiscardMs, bandwidthFraction,
        DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE) {}

    explicit Factory(
        int minDurationForQualityIncreaseMs,
        int maxDurationForQualityDecreaseMs,
        int minDurationToRetainAfterDiscardMs,
        float bandwidthFraction,
        float bufferedFractionToLiveEdgeForQualityIncrease) noexcept
        : minDurationForQualityIncreaseMs(minDurationForQualityIncreaseMs),
          maxDurationForQualityDecreaseMs(maxDurationForQualityDecreaseMs),
          minDurationToRetainAfterDiscardMs(minDurationToRetainAfterDiscardMs),
          bandwidthFraction(bandwidthFraction),
          bufferedFractionToLiveEdgeForQualityIncrease(
              bufferedFractionToLiveEdgeForQualityIncrease) {}

    virtual ~Factory() override = default;

    std::vector<std::shared_ptr<TrackSelection>> createTrackSelections(
        const std::vector<std::unique_ptr<Definition>>& definitions,
        const std::shared_ptr<BandwidthMeter>& bandwidthMeter) override;

    std::shared_ptr<AdaptiveTrackSelection> createAdaptiveTrackSelection(
        const std::shared_ptr<TrackGroup>& group,
        const std::shared_ptr<BandwidthMeter>& bandwidthMeter,
        const std::vector<int>& tracks, int totalFixedTrackBandwidth);
  };

  explicit AdaptiveTrackSelection(
      const std::shared_ptr<TrackGroup>& group, const std::vector<int>& tracks,
      const std::shared_ptr<BandwidthMeter>& bandwidthMeter) noexcept;

  explicit AdaptiveTrackSelection(
      const std::shared_ptr<TrackGroup>& group, const std::vector<int>& tracks,
      const std::shared_ptr<BandwidthMeter>& bandwidthMeter,
      int64_t reservedBandwidth, int64_t minDurationForQualityIncreaseMs,
      int64_t maxDurationForQualityDecreaseMs,
      int64_t minDurationToRetainAfterDiscardMs, float bandwidthFraction,
      float bufferedFractionToLiveEdgeForQualityIncrease) noexcept;

  explicit AdaptiveTrackSelection(
      const std::shared_ptr<TrackGroup>& group, const std::vector<int>& tracks,
      std::unique_ptr<DefaultBandwidthProvider> bandwidthProvider,
      int64_t minDurationForQualityIncreaseMs,
      int64_t maxDurationForQualityDecreaseMs,
      int64_t minDurationToRetainAfterDiscardMs,
      float bufferedFractionToLiveEdgeForQualityIncrease) noexcept;

  void experimentalSetBandwidthAllocationCheckpoints(
      const std::vector<std::vector<int64_t>>& allocationCheckpoints);

  void enable() override;

  void disable() override;

  int getSelectedIndex() const override;

  void onPlaybackSpeed(float speed) override;

  void onPlayWhenReadyChanged(bool playWhenReady) override;

  /**
   * Re-evaluates the selected track for the state of the playback.
   *
   * <p>Mirrors ExoPlayer's {@code AdaptiveTrackSelection.updateSelectedTrack}, reduced to the inputs
   * this player can provide: it has no chunk source, so it neither inspects the queue of the chunks
   * that are being read nor excludes a track because a chunk of it failed. A decrease in quality is
   * applied as soon as the bandwidth estimate drops, because the alternative is running out of
   * buffered media; an increase waits until enough media is buffered to absorb the switch.
   *
   * @param playbackPositionUs The current playback position.
   * @param bufferedDurationUs The duration of the media that is buffered.
   * @param availableDurationUs The duration of the media that is available, or
   *                            {@link Definitions::kTimeEndOfSource} when it is unknown.
   * @param queueSize The number of chunks being read, kept for the signature of ExoPlayer's method.
   */
  void updateSelectedTrack(int64_t playbackPositionUs, int64_t bufferedDurationUs,
      int64_t availableDurationUs, int queueSize);

  /**
   * Returns the index of the track the current bandwidth estimate allows: the highest one whose
   * bitrate does not exceed the allocated bandwidth, or the lowest one when none does.
   *
   * @param nowMs The current time, kept for the signature of ExoPlayer's method. A track that was
   *              blacklisted because reading it failed is re-considered once that time has passed;
   *              this selection has no blacklist, so the value is not used.
   * @param enableBackBuffer Whether a track can be selected while the back buffer is being kept.
   */
  int determineIdealSelectedIndex(int64_t nowMs, bool enableBackBuffer);

  /**
   * Returns whether a track can be selected for the given number of bits per second.
   *
   * @param format The format of the track.
   * @param formatBitrate The bitrate of the track.
   * @param effectiveBitrate The bandwidth the track can use.
   */
  static bool canSelectFormat(const std::shared_ptr<MediaFormat>& format,
      int64_t formatBitrate, int64_t effectiveBitrate);

  /**
   * Returns the bitrate of a track, or {@link Definitions::kUnspecified} when its format declares none.
   *
   * <p>Like ExoPlayer, the average bitrate is preferred and the peak bitrate is the fallback.
   */
  static int64_t getFormatBitrate(const std::shared_ptr<MediaFormat>& format);

  /**
   * Returns whether the media that was read ahead should be discarded from the back of the buffer.
   *
   * <p>Mirrors ExoPlayer's {@code AdaptiveTrackSelection.evaluateQueueSize}: the evaluation is
   * rate-limited to {@link #minDurationForQualityIncreaseMs}, and nothing is discarded while the
   * buffer is shorter than {@link #getMinDurationToRetainAfterDiscardUs}.
   */
  bool evaluateQueueSize(int64_t playbackPositionUs, int64_t bufferedDurationUs) override;

  /**
   * Returns the duration of media that has to be kept behind the playback position when the back
   * buffer is discarded, which is what the source of a progressive stream uses to trim its queues.
   */
  [[nodiscard]] int64_t getMinDurationToRetainAfterDiscardUs() const override {
    return minDurationToRetainAfterDiscardUs;
  }

  /**
   * Returns whether a track of the selection is excluded for the time being.
   *
   * <p>A track is excluded when the chunk that was read from it failed, which is what keeps a
   * selection from switching back to a track the source cannot read.
   */
  [[nodiscard]] bool isTrackExcluded(int index, int64_t nowMs) const override;

  /**
   * Excludes a track until the given time.
   *
   * <p>Mirrors ExoPlayer's {@code AdaptiveTrackSelection.maybeBlacklistTrack}: the caller of the
   * selection reports a track whose read failed, and the selection avoids it until then.
   *
   * @param index The index of the track in the selection.
   * @param exclusionDurationMs How long the track is excluded for. A duration of zero or less ignores
   *                            the report.
   * @param nowMs The current time.
   */
  void maybeBlacklistTrack(int index, int64_t exclusionDurationMs, int64_t nowMs);
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_ADAPTIVE_TRACK_SELECTION_H
