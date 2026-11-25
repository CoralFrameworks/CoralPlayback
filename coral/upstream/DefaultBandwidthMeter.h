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

#ifndef CORAL_PLAYBACK_UPSTREAM_DEFAULT_BANDWIDTH_METER_H
#define CORAL_PLAYBACK_UPSTREAM_DEFAULT_BANDWIDTH_METER_H

#include <coral/upstream/BandwidthMeter.h>
#include <coral/upstream/SlidingPercentile.h>
#include <coral/upstream/TransferListener.h>

#include <memory>
#include <string>

namespace coral {

/**
 * Estimates the bandwidth of the device from the transfers the sources report.
 *
 * <p>This is the counterpart of ExoPlayer's {@code DefaultBandwidthMeter}: it observes the network
 * transfers through the {@link TransferListener} interface, keeps the observations in a
 * {@link SlidingPercentile} window, and reports the median of the window as the estimate.
 *
 * <p>Only network transfers are measured. A transfer that reports no bytes, or that took no time,
 * leaves the estimate untouched.
 */
class DefaultBandwidthMeter final : public BandwidthMeter,
                                    public TransferListener {
public:
  /**
   * @param initialBitrateEstimate The estimate that is reported until the transfers gave a better
   *                               one, in bits per second.
   */
  explicit DefaultBandwidthMeter(
      int64_t initialBitrateEstimate = DEFAULT_INITIAL_BITRATE_ESTIMATE) noexcept;

  /**
   * Stops reporting samples: a listener that is still posted when the meter is destroyed is not
   * called. The listener objects themselves are not owned by the meter.
   */
  ~DefaultBandwidthMeter() override;

  [[nodiscard]] int64_t getBitrateEstimate() override;

  void addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
      const std::shared_ptr<EventListener>& listener) override;

  void removeEventListener(const std::shared_ptr<EventListener>& listener) override;

  void onTransferInitializing(const std::string& url, bool isNetwork) override;

  void onTransferStart(const std::string& url, bool isNetwork) override;

  void onBytesTransferred(const std::string& url, bool isNetwork, int bytesTransferred) override;

  void onTransferEnd(const std::string& url, bool isNetwork) override;

  /**
   * Default initial bitrate estimate used when the device is offline or the
   * network type cannot be determined, in bits per second.
   */
  static constexpr int64_t DEFAULT_INITIAL_BITRATE_ESTIMATE = 1000000;

  /**
   * The maximum weight of the sliding window the estimate is taken over, in units of
   * {@code sqrt(bytes)} of the observed transfers.
   */
  static constexpr int DEFAULT_SLIDING_WINDOW_MAX_WEIGHT = 2000;

  /** The percentile of the window that is reported. */
  static constexpr float DEFAULT_PERCENTILE = 0.5F;

private:
  void maybeNotifyBandwidthSample(
      int elapsedMs, int64_t bytesTransferred, int64_t bitrateEstimate);

  /** Whether a transfer is in flight, i.e. whether the bytes of one are being counted. */
  bool transferInFlight = false;

  /** The window the observed bitrates are kept in. */
  SlidingPercentile slidingPercentile{ DEFAULT_SLIDING_WINDOW_MAX_WEIGHT };

  /** The listener dispatcher, created lazily so that a meter without listeners allocates nothing. */
  std::unique_ptr<EventDispatcher> eventDispatcher;

  int64_t bitrateEstimate;
  int64_t lastReportedBitrateEstimate;

  /** When the transfer in flight started, in milliseconds. */
  int64_t sampleStartTimeMs = 0;

  /** The number of bytes the transfer in flight delivered. */
  int64_t sampleBytesTransferred = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_UPSTREAM_DEFAULT_BANDWIDTH_METER_H
