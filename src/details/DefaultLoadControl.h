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

#ifndef CORAL_PLAYBACK_DETAILS_DEFAULT_LOAD_CONTROL_H
#define CORAL_PLAYBACK_DETAILS_DEFAULT_LOAD_CONTROL_H

#include <coral/playback/LoadControl.h>
#include <coral/upstream/DefaultAllocator.h>

#include <map>

namespace coral {

/**
 * The default {@link LoadControl} implementation.
 *
 * <p>Mirrors ExoPlayer's {@code DefaultLoadControl}, including the thresholds it uses for the media
 * that is read from the device itself rather than over a network, which it recognizes from the url of
 * the media item.
 *
 * <p>Unlike ExoPlayer's, this control holds the loading state of a single player rather than one state
 * per player: a Coral player owns its load control, and {@link LoadControl::Parameters} does not carry
 * the identity of the player that reported it.
 */
class DefaultLoadControl final : public LoadControl {
public:
  /**
   * The default minimum duration of media that the player will attempt to  ensure is buffered at all
   * times, in milliseconds.
   */
  static constexpr int DEFAULT_MIN_BUFFER_MS = 50000;

  /**
   * The default minimum duration of media that the player will attempt to ensure is buffered at all
   * times for local on-device playback, in milliseconds.
   */
  // Load at least as much as needed for DEFAULT_BUFFER_FOR_PLAYBACK_FOR_LOCAL_PLAYBACK_MS
  static constexpr int DEFAULT_MIN_BUFFER_FOR_LOCAL_PLAYBACK_MS = 1000;

  /**
   * The default maximum duration of media that the player will attempt to buffer, in milliseconds.
   */
  static constexpr int DEFAULT_MAX_BUFFER_MS = 50000;

  /**
   * The default maximum duration of media that the player will attempt to buffer for local
   * on-device playback, in milliseconds.
   */
  // Upper limit for low-bitrate streams that is also high enough to cover most reasonable timestamp
  // gaps in the media as well.
  static constexpr int DEFAULT_MAX_BUFFER_FOR_LOCAL_PLAYBACK_MS = 50000;

  /**
   * The default duration of media that must be buffered for playback to start or resume following a
   * user action such as a seek, in milliseconds.
   */
  static constexpr int DEFAULT_BUFFER_FOR_PLAYBACK_MS = 1000;

  /**
   * The default duration of media that must be buffered for playback to start or resume following a
   * user action such as a seek for local on-device playback, in milliseconds.
   */
  static constexpr int DEFAULT_BUFFER_FOR_PLAYBACK_FOR_LOCAL_PLAYBACK_MS = 1000;

  /**
   * The default duration of media that must be buffered for playback to resume after a rebuffer, in
   * milliseconds. A rebuffer is defined to be caused by buffer depletion rather than a user action.
   */
  static constexpr int DEFAULT_BUFFER_FOR_PLAYBACK_AFTER_REBUFFER_MS = 2000;

  /**
   * The default duration of media that must be buffered for playback to resume after a rebuffer for
   * local on-device playback, in milliseconds. A rebuffer is defined to be caused by buffer
   * depletion rather than a user action.
   */
  static constexpr int DEFAULT_BUFFER_FOR_PLAYBACK_AFTER_REBUFFER_FOR_LOCAL_PLAYBACK_MS = 1000;

  /**
   * The default target buffer size in bytes. The value -1 means that the load
   * control will calculate the target buffer size based on the selected tracks.
   */
  static constexpr int DEFAULT_TARGET_BUFFER_BYTES = -1;

  /**
   * The default prioritization of buffer time constraints oversize constraints.
   */
  static constexpr int DEFAULT_PRIORITIZE_TIME_OVER_SIZE_THRESHOLDS = false;

  /**
   * The default prioritization of buffer time constraints over size constraints for local on-device
   * playback.
   */
  // Ensure DEFAULT_MIN_BUFFER_FOR_LOCAL_PLAYBACK_MS even if target buffer size in bytes is reached
  // for extremely high-bitrate content.
  static constexpr bool DEFAULT_PRIORITIZE_TIME_OVER_SIZE_THRESHOLDS_FOR_LOCAL_PLAYBACK = true;

  /**
   * The default back buffer duration in milliseconds.
   */
  static constexpr int DEFAULT_BACK_BUFFER_DURATION_MS = 0;

  /**
   * The default for whether the back buffer is retained from the previous keyframe.
   */
  static constexpr bool DEFAULT_RETAIN_BACK_BUFFER_FROM_KEYFRAME = false;

  /**
   * A default size in bytes for an individual allocation that forms part of a larger buffer.
   */
  static constexpr int DEFAULT_BUFFER_SEGMENT_SIZE = 64 * 1024;

  /**
   * A default size in bytes for a video buffer.
   */
  static constexpr int DEFAULT_VIDEO_BUFFER_SIZE = 2000 * DEFAULT_BUFFER_SEGMENT_SIZE;

  /** A default size in bytes for a video buffer for local playback. */
  // Sufficient size for 1 second at even 150 Mbit/s.
  static constexpr int DEFAULT_VIDEO_BUFFER_SIZE_FOR_LOCAL_PLAYBACK = 300 * DEFAULT_BUFFER_SEGMENT_SIZE;

  /**
   * A default size in bytes for an audio buffer
   */
  static constexpr int DEFAULT_AUDIO_BUFFER_SIZE = 200 * DEFAULT_BUFFER_SEGMENT_SIZE;

  /**
   * A default size in bytes for a text buffer.
   */
  static constexpr int DEFAULT_TEXT_BUFFER_SIZE = 2 * DEFAULT_BUFFER_SEGMENT_SIZE;

  /**
   * A default size in bytes for a metadata buffer.
   */
  static constexpr int DEFAULT_METADATA_BUFFER_SIZE = 2 * DEFAULT_BUFFER_SEGMENT_SIZE;

  /**
   * A default size in bytes for an image buffer.
   */
  static constexpr int DEFAULT_IMAGE_BUFFER_SIZE = 2 * DEFAULT_BUFFER_SEGMENT_SIZE;

  /**
   * A default size in bytes for a muxed buffer (e.g. containing video, audio and text).
   */
  static constexpr int DEFAULT_MUXED_BUFFER_SIZE =
    DEFAULT_VIDEO_BUFFER_SIZE + DEFAULT_AUDIO_BUFFER_SIZE + DEFAULT_TEXT_BUFFER_SIZE;

  /**
   * The buffer size in bytes that will be used as a minimum target buffer in all cases.
   * This is also the default target buffer before tracks are selected.
   */
  static constexpr int DEFAULT_MIN_BUFFER_SIZE = 200 * DEFAULT_BUFFER_SEGMENT_SIZE;

  /**
   * The buffer size in bytes that will be used as a maximum target buffer in all cases unless an
   * {@linkplain Builder#setTargetBufferBytes(int) explicit buffer size is set} or {@link
   * #calculateTargetBufferBytes} is overridden with custom behavior.
   */
  // Assuming a muxed file with video, multiple audio and text languages and an image track.
  static constexpr int DEFAULT_MAX_BUFFER_SIZE =
    DEFAULT_VIDEO_BUFFER_SIZE
    + 4 * DEFAULT_AUDIO_BUFFER_SIZE
    + 4 * DEFAULT_TEXT_BUFFER_SIZE
    + DEFAULT_IMAGE_BUFFER_SIZE;

  /**
   * The default target buffer size in bytes that will be used for preloading media outside of
   * player, see {@link PlayerId#PRELOAD}.
   */
  static constexpr int DEFAULT_TARGET_BUFFER_BYTES_FOR_PRELOAD =
    DEFAULT_VIDEO_BUFFER_SIZE + DEFAULT_AUDIO_BUFFER_SIZE;

  DefaultLoadControl();

  explicit DefaultLoadControl(const std::shared_ptr<Allocator>& allocator) noexcept;

  void onPrepared() override;

  void onTracksSelected(Parameters parameters,
    const std::shared_ptr<TrackGroupArray>& trackGroups,
    const std::shared_ptr<TrackSelectionArray>& selections) override;

  void onStopped() override;

  void onReleased() override;

  [[nodiscard]] const std::shared_ptr<Allocator>& getAllocator() const override {
    return allocator;
  }

  int64_t getBackBufferDurationUs() override;

  bool retainBackBufferFromKeyframe() override;

  bool shouldContinueLoading(Parameters parameters) override;

  bool shouldStartPlayback(Parameters parameters) override;

private:
  explicit DefaultLoadControl(std::shared_ptr<Allocator> allocator,
    int minBufferMs,
    int minBufferForLocalPlaybackMs,
    int maxBufferMs,
    int maxBufferForLocalPlaybackMs,
    int bufferForPlaybackMs,
    int bufferForPlaybackForLocalPlaybackMs,
    int bufferForPlaybackAfterRebufferMs,
    int bufferForPlaybackAfterRebufferForLocalPlaybackMs,
    int targetBufferBytes,
    bool prioritizeTimeOverSizeThresholds,
    bool prioritizeTimeOverSizeThresholdsForLocalPlayback,
    int backBufferDurationMs,
    bool retainBackBufferFromKeyframe,
    std::map<std::string, int> playerTargetBufferBytes = {}) noexcept;

  int calculateTargetBufferBytes(const Parameters& parameters, const std::shared_ptr<TrackSelectionArray>& selections);

  void resetPlayerLoadingState();

  /**
   * Applies the current target buffer size to the allocator. Called whenever the target buffer size
   * can change, so that the allocator never holds on to a size that no longer matches the selected
   * tracks.
   */
  void updateAllocator();

  bool isLocalPlayback(const Parameters& parameters);

  static int getDefaultBufferSize(MediaType trackType, bool isLocalPlayback);

  static int64_t getMediaDurationForPlayoutDuration(int64_t playoutDuration, float speed);

  static int64_t getPlayoutDurationForMediaDuration(int64_t mediaDuration, float speed);

  static const char* getName() { return "DefaultLoadControl"; }

  /**
   * Determine whether the heap has enough free space to respect {@link
   * #prioritizeTimeOverSizeThresholds(boolean)}.
   *
   * <p>Ignoring {@link #prioritizeTimeOverSizeThresholds(boolean)} when heap space is very limited
   * helps to avoid {@link OutOfMemoryError} when playing extremely high bitrate content.
   *
   * <p>'Enough space' is defined as 4% of the max heap size, because the system will start throwing
   * {@link OutOfMemoryError} when available heap space drops below 1%.
   */
  bool heapHasEnoughHeadroomForPrioritizeTimeOverSizeThreshold();

  /**
   * Returns whether the media of the given url is read from the device rather than over a network.
   *
   * <p>Mirrors ExoPlayer's {@code DefaultLoadControl.LOCAL_PLAYBACK_SCHEMES}: a url without a scheme,
   * which is a path on the device, and the schemes of the media that is stored on or shipped with the
   * device are local playback. Everything else, in particular an http url, is streaming.
   *
   * @param url The url of the media item.
   */
  static bool isLocalUrl(const std::string& url);

  /**
   * The window and the period that the timeline lookups of {@link isLocalPlayback} populate.
   *
   * <p>They are created once and reused because a timeline populates the objects it is given rather
   * than returning new ones, and a null object is a null dereference: see {@link
   * Timeline::getWindow} and {@link Timeline::getPeriod}, which require them not to be null. The
   * player reports the parameters of a load from its playback thread only, so sharing them between
   * the calls is safe.
   */
  std::shared_ptr<Timeline::Window> window = std::make_shared<Timeline::Window>();
  std::shared_ptr<Timeline::Period> period = std::make_shared<Timeline::Period>();

  std::shared_ptr<Allocator> allocator;
  int64_t minBufferUs;
  int64_t minBufferForLocalPlaybackUs;
  int64_t maxBufferUs;
  int64_t maxBufferForLocalPlaybackUs;
  int64_t bufferForPlaybackUs;
  int64_t bufferForPlaybackForLocalPlaybackUs;
  int64_t bufferForPlaybackAfterRebufferUs;
  int64_t bufferForPlaybackAfterRebufferForLocalPlaybackUs;
  int64_t targetBufferBytesOverwrite;
  bool prioritizeTimeOverSizeThresholds;
  bool prioritizeTimeOverSizeThresholdsForLocalPlayback;
  int64_t backBufferDurationUs;
  bool mRetainBackBufferFromKeyframe;
  std::map<std::string, int> playerTargetBufferBytesOverwrites;
  int64_t targetBufferBytes;
  bool isLoading;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_DEFAULT_LOAD_CONTROL_H
