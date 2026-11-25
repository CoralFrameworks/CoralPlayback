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

#include <coral/upstream/DefaultBandwidthMeter.h>

#include <chrono>
#include <cmath>

namespace coral {

namespace {

/** Milliseconds from the clock the transfer observations are timed with. */
int64_t elapsedRealtimeMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace anonymous

DefaultBandwidthMeter::DefaultBandwidthMeter(const int64_t initialBitrateEstimate) noexcept
    : bitrateEstimate(initialBitrateEstimate),
      lastReportedBitrateEstimate(0) {
}

DefaultBandwidthMeter::~DefaultBandwidthMeter() {
  if (eventDispatcher != nullptr) {
    // Posted samples hold the listener alive but must not be delivered once the meter is gone.
    eventDispatcher->release();
  }
}

int64_t DefaultBandwidthMeter::getBitrateEstimate() {
  return bitrateEstimate;
}

void DefaultBandwidthMeter::addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
    const std::shared_ptr<EventListener>& listener) {
  if (eventDispatcher == nullptr) {
    eventDispatcher = std::make_unique<EventDispatcher>();
  }
  eventDispatcher->addListener(eventHandler, listener);
}

void DefaultBandwidthMeter::removeEventListener(const std::shared_ptr<EventListener>& listener) {
  if (eventDispatcher != nullptr) {
    eventDispatcher->removeListener(listener);
  }
}

void DefaultBandwidthMeter::onTransferInitializing(const std::string& url, const bool isNetwork) {
  static_cast<void>(url);
  static_cast<void>(isNetwork);
  // Nothing is measured until the transfer actually starts, like ExoPlayer.
}

void DefaultBandwidthMeter::onTransferStart(const std::string& url, const bool isNetwork) {
  static_cast<void>(url);
  if (!isNetwork) {
    return;
  }
  if (!transferInFlight) {
    transferInFlight = true;
    sampleBytesTransferred = 0;
  }
  sampleStartTimeMs = elapsedRealtimeMs();
}

void DefaultBandwidthMeter::onBytesTransferred(const std::string& url,
    const bool isNetwork,
    const int bytesTransferred) {
  static_cast<void>(url);
  if (!isNetwork) {
    return;
  }
  sampleBytesTransferred += bytesTransferred;
}

void DefaultBandwidthMeter::onTransferEnd(const std::string& url, const bool isNetwork) {
  static_cast<void>(url);
  if (!isNetwork) {
    return;
  }
  if (!transferInFlight) {
    // The transfer never started: there is nothing to measure.
    return;
  }
  transferInFlight = false;

  const auto nowMs = elapsedRealtimeMs();
  const auto sampleElapsedMs = static_cast<int>(nowMs - sampleStartTimeMs);
  const auto transferred = sampleBytesTransferred;
  sampleBytesTransferred = 0;
  sampleStartTimeMs = nowMs;

  if (sampleElapsedMs <= 0 || transferred <= 0) {
    // A transfer that moved no bytes, or that took no measurable time, says nothing about the
    // bandwidth: the previous estimate is kept, like ExoPlayer does.
    return;
  }
  // The sample is weighted by the square root of the number of bytes, so that a long transfer is
  // trusted more than a short one without dominating the window.
  const auto weight = static_cast<int>(std::sqrt(static_cast<double>(transferred)));
  const auto bitsPerSecond =
      static_cast<float>(static_cast<double>(transferred) * 8000.0 / sampleElapsedMs);
  slidingPercentile.addSample(weight, bitsPerSecond);
  bitrateEstimate = static_cast<int64_t>(slidingPercentile.getPercentile(DEFAULT_PERCENTILE));
  maybeNotifyBandwidthSample(sampleElapsedMs, transferred, bitrateEstimate);
}

void DefaultBandwidthMeter::maybeNotifyBandwidthSample(
    const int elapsedMs, const int64_t bytesTransferred, const int64_t estimate) {
  if (elapsedMs == 0 && bytesTransferred == 0 && estimate == lastReportedBitrateEstimate) {
    return;
  }
  lastReportedBitrateEstimate = estimate;
  if (eventDispatcher != nullptr) {
    eventDispatcher->bandwidthSample(elapsedMs, bytesTransferred, estimate);
  }
}

} // namespace coral
