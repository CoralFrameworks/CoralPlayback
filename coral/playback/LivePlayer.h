/*
 * Copyright (C) 2022 The Coral Project
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
#ifndef CORAL_PLAYBACK_LIVE_PLAYER_H
#define CORAL_PLAYBACK_LIVE_PLAYER_H

#include <coral/playback/Player.h>
#include <coral/playback/LottieOverlay.h>

#include <coral/source/MediaItem.h>

#include <memory>
#include <string>

namespace coral {

class VideoOutputNodeFactory;
class EffectEngine;
class EffectInputNode;
class VideoSurface;

/**
 * LivePlayer offers a playback interface for playback that's sufficient for the
 * implementation of playback controllers and playback user interfaces.
 */
class LivePlayer : public Player {
public:
  /**
   * The renderers a player is built with.
   *
   * <p>A video preview that does not want to open an audio device disables `audioEnabled`; the
   * player then runs the remaining renderers on its standalone clock. `create()` still builds every
   * renderer by default, so an application that wants sound does not have to configure anything.
   */
  struct Configuration {
    /** Build the audio renderer and open an output device. */
    bool audioEnabled = true;
    /** Build the video renderer. */
    bool videoEnabled = true;
    /** Build the still-image renderer. */
    bool imageEnabled = true;
    /** Build the text renderer that delivers cues. */
    bool textEnabled = true;
    /** Build the metadata renderer. */
    bool metadataEnabled = true;
  };

  /**
   * Creates a newly LivePlayer with url
   *
   * @param url the url to playback
   * @param configuration the renderers to build
   * @return a newly LivePlayer
   */
  static std::unique_ptr<LivePlayer> create(const std::string& url);
  static std::unique_ptr<LivePlayer> create(const std::string& url,
                                            const Configuration& configuration);

  /** Creates a player for an item and side-loads the subtitle files it names. */
  static std::unique_ptr<LivePlayer> create(const std::shared_ptr<MediaItem>& mediaItem);
  static std::unique_ptr<LivePlayer> create(const std::shared_ptr<MediaItem>& mediaItem,
                                            const Configuration& configuration);

  explicit LivePlayer(std::unique_ptr<Player> player) noexcept;

  ~LivePlayer() override;

  /**
   * Enables the EffectEngine video path.
   *
   * <p>Must be called before `prepare()`. The graph starts headless and waits for
   * `setOutputSurface`; the factory turns the platform surface into an output node. Passing null
   * keeps the graph headless (tests/readback).
   */
  void enableVideoEffectPipeline(
      const std::shared_ptr<VideoOutputNodeFactory>& factory = nullptr);

  /**
   * Enables the pipeline with a platform input node and the decoder surface that feeds it.
   *
   * <p>This is the Android SurfaceTexture zero-copy entry point: the application creates the
   * input node and a `VideoSurface` over the decoder's Java Surface, then hands both here.
   */
  void enableVideoEffectPipeline(
      const std::shared_ptr<VideoOutputNodeFactory>& factory,
      const std::shared_ptr<EffectInputNode>& input,
      const std::shared_ptr<VideoSurface>& decoderSurface,
      void* sharedContext = nullptr);

  [[nodiscard]] bool isVideoEffectPipelineEnabled() const;

  /** The effect engine when the pipeline is enabled, or null otherwise. */
  [[nodiscard]] std::shared_ptr<EffectEngine> getEffectEngine() const;

  /** True when the effect graph currently composites burned-in subtitles. */
  [[nodiscard]] bool isSubtitleOverlayActive() const;

  /** Adds a pre-rendered Lottie overlay to the effect graph. Requires the effect pipeline. */
  bool addLottieOverlay(const LottieOverlayConfig& config);

  /** Removes every Lottie overlay from the effect graph. */
  void clearLottieOverlays();

  /**
   * Sets the surface to be used as the sink for the video portion of the media. Only available for
   * Android. The platform surface is translated by a VideoOutputNodeFactory.
   *
   * @param surface the output surface.
   */
  void setOutputSurface(const std::shared_ptr<VideoSurface>& surface) override;

  /**
   * Sets video scaling mode. To make the target video scaling mode effective during playback, this
   * method must be called after data source is set. If not called, the default video scaling mode
   * is VIDEO_SCALING_MODE_SCALE_TO_FIT.
   * @param mode target video scaling mode.
   */
  void setVideoScalingMode(int mode) override;

  void setTextOutput(const std::shared_ptr<TextOutput>& output) override;

  /**
   * Sets the preferred language of the selected subtitle track, or clears it with an empty string.
   *
   * <p>When @p selectUndetermined is true, a text track whose language is unknown is still a
   * candidate; that is what makes an ASS/SSA track without a language tag selectable.
   */
  void setPreferredTextLanguage(const std::string& language, bool selectUndetermined = true);

  void setSubtitleEnabled(bool enabled) override;

  [[nodiscard]] bool isSubtitleEnabled() const override;

  /**
   * Loads a standalone subtitle file (SRT, ASS/SSA or WebVTT) and shows its cues over the video.
   *
   * <p>This is the side-load path for an application that has a subtitle file next to the media.
   * The format is inferred from @p path unless @p mimeType is given.
   */
  void setSubtitleFile(const std::string& path, const std::string& mimeType = "");

  [[nodiscard]] std::shared_ptr<TextOutput> getTextOutput() const override;

  void setMetadataOutput(const std::shared_ptr<MetadataOutput>& output) override;

  [[nodiscard]] std::shared_ptr<MetadataOutput> getMetadataOutput() const override;

  void setSubtitleRenderMode(SubtitleRenderMode mode) override;

  [[nodiscard]] SubtitleRenderMode getSubtitleRenderMode() const override;

  /**
   * Clears the playlist, adds the specified {@link MediaItem} and resets the position to
   * the default position.
   *
   * To replace a media item (possibly seamlessly) without clearing the playlist, use {@link
   * #replaceMediaItem}.
   *
   * @param playerItem The new {@link MediaItem}.
   */
  void setMediaItem(const std::shared_ptr<MediaItem>& playerItem) override;

  /**
   * Prepares the player for playback
   */
  void prepare() override;

  /**
   * Returns the current playback state of the player.
   *
   * @return The current playback state.
   */
  [[nodiscard]] const State& getPlaybackState() const override;

  /**
   * Returns the message of the source error that stopped playback, or an empty string when playback
   * has not been stopped by one.
   *
   * @return The playback error message, or an empty string.
   */
  [[nodiscard]] const std::string& getPlaybackErrorMessage() const override;

  /**
   * Indicates whether the MediaPlayer is playing.
   */
  [[nodiscard]] bool isPlaying() const override;

  /**
   * Starts or resumes playback. If playback had previously been paused, playback will continue
   * from where it was paused. If playback had been stopped, or never started before, playback
   * will start at the beginning.
   */
  void play() override;

  /**
   * Pauses playback. Call start() to resume.
   */
  void pause() override;

  /**
   * Stops playback after playback has been started or paused.
   */
  void stop() override;

  /**
   * Releases the player. This method must be called when the player is no longer required. The
   * player must not be used after calling this method.
   */
  void release() override;

  /**
   * Sets whether playback should proceed when {@link getPlaybackState()} == {@link STATE_READY}.
   *
   * If the player is already in the ready state then this method pauses and resumes playback.
   *
   * @param playWhenReady Whether playback should proceed when ready.
   */
  void setPlayWhenReady(bool playWhenReady) override;

  /**
   * Whether playback will proceed when {@link getPlaybackState()} == {@link STATE_READY}.
   */
  [[nodiscard]] bool getPlayWhenReady() const override;

  /**
   * Sets the {@link RepeatMode} to be used for playback.
   *
   * @param mode The repeat mode.
   */
  void setRepeatMode(RepeatMode mode) override;

  /**
   * Returns the current {@link RepeatMode} used for playback.
   */
  [[nodiscard]] const RepeatMode& getRepeatMode() const override;

  /**
   * Whether the player is currently loading the source.
   */
  [[nodiscard]] bool isLoading() const override;

  /**
   * Seeks to a position specified in microseconds in the current {@link MediaItem}.
   *
   * @param positionUs The seek position in the current {@link MediaItem}
   */
  void seekTo(int64_t positionUs) override;

  /**
   * Seeks to the default position of the current {@link MediaItem}.
   */
  void seekToDefaultPosition() override;

  /**
   * Attempts to set the playback parameters. Passing {@link Parameters()} resets the
   * player to the default, which means there is no speed or pitch adjustment.
   *
   * @param playbackParameters The playback parameters.
   */
  void setPlaybackParameters(const Parameters& playbackParameters) override;

  /**
   * Returns the currently active playback parameters.
   */
  [[nodiscard]] const Parameters& getPlaybackParameters() const override;

  /**
   * Gets the current playback position.
   *
   * @return the current position in microseconds
   */
  [[nodiscard]] int64_t getCurrentPositionUs() const override;

  /**
   * Gets the duration of the file.
   * @return the duration in microseconds, if no duration is available (for example, if streaming
   * live content), -1 is returned.
   */
  [[nodiscard]] int64_t getDurationUs() const override;

  /**
   * Indicates whether the player is looping or non-looping.
   * 
   * @return true if the player is currently looping, false otherwise
   */
  [[nodiscard]] bool isLooping() const {
    return getRepeatMode() == RepeatMode::REPEAT_MODE_ALL;
  }

  /**
   * Sets the player to be looping or non-looping.
   *
   * @param looping whether to loop or not
   */
  void setLooping(bool looping);

  /**
   * Returns the volume of the player.
   */
  [[nodiscard]] float getVolume() const override;

  /**
   * Sets the volume on this player.
   */
  void setVolume(const Volume& volume) override;

  /**
   * Returns the height of the video.
   *
   * @return If there is no video, or the height has not been determined yet, returns 0.
   */
  [[nodiscard]] int getVideoHeight() const override;

  /**
   * Returns the width of the video
   *
   * @return If there is no video, or the width has not been determined yet, returns 0.
   */
  [[nodiscard]] int getVideoWidth() const override;

  void addListener(const std::shared_ptr<EventListener>& listener) override;

  void removeListener(const std::shared_ptr<EventListener>& listener) override;

  void addMediaSourceEventListener(const std::shared_ptr<MediaSourceEventListener>& listener) override;

  void removeMediaSourceEventListener(
      const std::shared_ptr<MediaSourceEventListener>& listener) override;

  void addAnalyticsListener(const std::shared_ptr<AnalyticsListener>& listener) override;

  void removeAnalyticsListener(const std::shared_ptr<AnalyticsListener>& listener) override;

  [[nodiscard]] int getMediaItemCount() const override;

  [[nodiscard]] const std::shared_ptr<MediaItem>& getMediaItemAt(int index) const override;

  void addMediaItems(int index,
      const std::vector<std::shared_ptr<MediaItem>>& mediaItems) override;

  void removeMediaItems(int fromIndex, int toIndex) override;

  void moveMediaItems(int fromIndex, int toIndex, int newIndex) override;

  void clearMediaItems() override;

private:
  std::unique_ptr<Player> mImpl;
};

} // namespace coral

#endif // CORAL_PLAYBACK_LIVE_PLAYER_H
