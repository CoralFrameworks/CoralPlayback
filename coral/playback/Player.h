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
#ifndef CORAL_PLAYBACK_PLAYER_H
#define CORAL_PLAYBACK_PLAYER_H

#include <coral/playback/Chapters.h>
#include <coral/playback/MetadataOutput.h>
#include <coral/playback/Parameters.h>
#include <coral/playback/TextOutput.h>
#include <coral/playback/Thumbnail.h>
#include <coral/playback/Tracks.h>
#include <coral/playback/VideoFrameOutput.h>
#include <coral/playback/Volume.h>
#include <coral/source/MediaItem.h>
#include <coral/source/MediaSourceEventListener.h>
#include <coral/source/RepeatMode.h>

#include <codec/MediaCodec.h>

#include <media/Types.h>

#include <memory>
#include <string>
#include <vector>

namespace coral {

class MediaSourceFactory;

// The analytics listener is a separate concern that needs the types this interface declares, so it is
// only declared here: a `shared_ptr` to an incomplete type is enough for these declarations.
class AnalyticsListener;

/**
 * A media player interface defining high-level functionality, such as the ability to play, pause,
 * seek and query properties of the currently playing media.
 */
class Player {
public:
  virtual ~Player() = default;

  /**
   * Playback state. One of {@link STATE_IDLE}, {@link STATE_BUFFERING}, {@link STATE_READY} or
   * {@link STATE_ENDED}.
   */
  enum class State : uint8_t {
    /**
     * The player is idle, meaning it holds only limited resources. The player must be {@link
     * #prepare() prepared} before it will play the media.
     */
    STATE_IDLE = 1,
    /**
     * The player is not able to immediately play the media, but is doing work toward being able to
     * do so. This state typically occurs when the player needs to buffer more data before playback
     * can start.
     */
    STATE_BUFFERING = 2,
    /**
     * The player is able to immediately play from its current position. The player will be playing
     * if {@link getPlayWhenReady()} is true, and paused otherwise.
     */
    STATE_READY = 3,
    /** The player has finished playing the media. */
    STATE_ENDED = 4,
  };

  /**
   * A listener of the high level state of the player.
   *
   * <p>Mirrors ExoPlayer's {@code Player.EventListener}, reduced to the events this player can
   * report. Every callback has an empty default implementation, so a listener only overrides the
   * events it is interested in.
   *
   * <p>The listener is called on the looper of the thread that created the player, or on the
   * playback thread when that thread had no looper.
   */
  class EventListener {
  public:
    virtual ~EventListener() = default;

    /** Called when the playback state changed. */
    virtual void onPlaybackStateChanged(const State& state) {}

    /** Called when whether the playback proceeds once ready changed. */
    virtual void onPlayWhenReadyChanged(bool playWhenReady) {}

    /** Called when whether the player is loading the media changed. */
    virtual void onIsLoadingChanged(bool isLoading) {}

    /**
     * Called when the position of the player changed without a seek, e.g. when the playback moved
     * to the next period of the playlist.
     *
     * @param positionUs The new position in microseconds.
     */
    virtual void onPositionDiscontinuity(int64_t positionUs) {}

    /** Called when the playback parameters changed. */
    virtual void onPlaybackParametersChanged(const Parameters& parameters) {}

    /** Called when the repeat mode changed. */
    virtual void onRepeatModeChanged(const RepeatMode& repeatMode) {}

    /** Called when the volume changed. */
    virtual void onVolumeChanged(float volume) {}

    /** Called when the size of the video changed. */
    virtual void onVideoSizeChanged(int width, int height) {}

    /** Called when the colour description of the video changed. */
    virtual void onVideoColorInfoChanged(const VideoColorInfo& colorInfo) {}

    /** Called when the chapters of the current media item changed. */
    virtual void onChaptersChanged(const std::vector<Chapter>& chapters) {}

    /**
     * Called when the available tracks or their selection changed.
     *
     * <p>The snapshot is complete: a listener can replace its menu model with it. The callback runs
     * on the thread the player's other events use.
     */
    virtual void onTracksChanged(const Tracks& tracks) {}

    /**
     * Called when an error stopped the playback.
     *
     * @param errorMessage The message of the error.
     */
    virtual void onPlayerError(const std::string& errorMessage) {}

    /** Called when the cues that the player renders change, including the empty group. */
    virtual void onCues(const AVCueGroup& cueGroup) {}

    /** Called with the timed metadata of one sample when the playback reaches its time. */
    virtual void onMetadata(const Metadata& metadata) {}
  };

  /**
   * Sets the surface to be used as the sink for the video portion of the media. Only available for
   * Android. The platform surface is translated by a VideoOutputNodeFactory.
   *
   * @param surface the output surface.
   */
  virtual void setOutputSurface(const std::shared_ptr<VideoSurface>& surface) = 0;

  /**
   * How the video is fitted into the output surface.
   *
   * <p>This is the platform-neutral form of {@link #setVideoScalingMode(int)}. `Fill` is a center
   * crop; `Fit` letterboxes; `Stretch` ignores the aspect ratio. The default is `Fit`.
   */
  enum class VideoScaleMode : uint8_t {
    /** Preserve the aspect ratio, letterboxing the remainder. */
    Fit,
    /** Preserve the aspect ratio, filling the output and cropping the overflow. */
    Fill,
    /** Fill the output without preserving the aspect ratio. */
    Stretch,
  };

  /**
   * Sets video scaling mode. To make the target video scaling mode effective during playback, this
   * method must be called after data source is set. If not called, the default video scaling mode
   * is VIDEO_SCALING_MODE_SCALE_TO_FIT.
   *
   * @param mode target video scaling mode.
   */
  virtual void setVideoScalingMode(int mode) = 0;

  /**
   * Sets how the video is fitted into the output surface.
   *
   * <p>The default implementation maps this to the platform scaling mode, so a player that only
   * implements {@link #setVideoScalingMode(int)} keeps working.
   */
  virtual void setVideoScaleMode(const VideoScaleMode mode) {
    switch (mode) {
      case VideoScaleMode::Fill:
        setVideoScalingMode(2);
        break;
      case VideoScaleMode::Stretch:
        setVideoScalingMode(3);
        break;
      case VideoScaleMode::Fit:
      default:
        setVideoScalingMode(1);
        break;
    }
  }

  /** Returns the scaling mode requested with {@link #setVideoScaleMode}. */
  [[nodiscard]] virtual VideoScaleMode getVideoScaleMode() const {
    return VideoScaleMode::Fit;
  }

  /**
   * Mirrors the video after rotation and fitting.
   *
   * <p>The default implementation ignores it; a platform that renders through an effect graph
   * applies it in the layout pass.
   */
  virtual void setVideoMirror(bool horizontal, bool vertical) {
    static_cast<void>(horizontal);
    static_cast<void>(vertical);
  }

  /**
   * Sets the receiver of the cues that the player renders, or clears it with null.
   *
   * <p>The counterpart of ExoPlayer's {@code Player.setTextOutput}. The cues are delivered on the
   * playback thread, and the same groups are reported to {@link EventListener#onCues}.
   */
  virtual void setTextOutput(const std::shared_ptr<TextOutput>& output) {}

  /** Selects the subtitle language; the default implementation ignores it. */
  virtual void setPreferredTextLanguage(const std::string& language, bool selectUndetermined = true) {}

  /** Enables or disables the subtitle renderer, without changing the available text tracks. */
  virtual void setSubtitleEnabled(bool enabled) {}

  /** Returns whether the subtitle renderer is enabled. */
  [[nodiscard]] virtual bool isSubtitleEnabled() const { return true; }

  /** Side-loads a subtitle file; the default implementation ignores it. */
  virtual void setSubtitleFile(const std::string& path, const std::string& mimeType = "") {}

  /** Sets the factory that turns a media item into a media source. */
  virtual void setMediaSourceFactory(const std::shared_ptr<MediaSourceFactory>& factory) {}

  /**
   * Returns the latest frame the effect graph rendered into the application's surface.
   *
   * <p>A UI bridge polls it and imports the surface when {@link VideoFrameOutput::sequence}
   * changes. The default implementation returns an empty value for players without an effect video
   * pipeline.
   */
  [[nodiscard]] virtual VideoFrameOutput getVideoFrameOutput() const {
    return {};
  }

  /** Returns the colour description of the current video, or defaults when unknown. */
  [[nodiscard]] virtual VideoColorInfo getVideoColorInfo() const {
    return {};
  }

  /** Returns the chapters of the current media item, or an empty list when it has none. */
  [[nodiscard]] virtual std::vector<Chapter> getChapters() const {
    return {};
  }

  /**
   * Installs the provider used by {@link requestThumbnail}, or clears it.
   *
   * <p>The provider is application/platform code; the player only calls it and cancels it when it is
   * released. A null provider disables thumbnail requests.
   */
  virtual void setThumbnailProvider(std::shared_ptr<ThumbnailProvider> provider) {
    static_cast<void>(provider);
  }

  [[nodiscard]] virtual std::shared_ptr<ThumbnailProvider> getThumbnailProvider() const {
    return nullptr;
  }

  /** Requests one thumbnail from the installed provider. Does nothing without one. */
  virtual void requestThumbnail(int64_t positionUs, ThumbnailProvider::Callback callback) {
    static_cast<void>(positionUs);
    static_cast<void>(callback);
  }

  /** Warms the installed provider for a position without delivering a frame. */
  virtual void prefetchThumbnail(int64_t positionUs) {
    static_cast<void>(positionUs);
  }

  /**
   * Sets the preferred audio language as an IETF BCP 47 tag.
   *
   * <p>An explicit {@code selectTrack} on an audio track still wins. Empty clears the preference.
   */
  virtual void setPreferredAudioLanguage(const std::string& language) {
    static_cast<void>(language);
  }

  [[nodiscard]] virtual std::string getPreferredAudioLanguage() const {
    return {};
  }

  [[nodiscard]] virtual std::string getPreferredTextLanguage() const {
    return {};
  }

  /**
   * Sets the preferred text role flags (forced, caption, commentary, ...). The selector uses them
   * as a preference, not as a hard filter; zero clears the preference.
   */
  virtual void setPreferredTextRoleFlags(int roleFlags) {
    static_cast<void>(roleFlags);
  }

  [[nodiscard]] virtual int getPreferredTextRoleFlags() const {
    return 0;
  }

  /**
   * Whether a text track with no language may be selected when no preferred language matches.
   * Defaults to true.
   */
  virtual void setSelectUndeterminedTextLanguage(bool select) {
    static_cast<void>(select);
  }

  [[nodiscard]] virtual bool getSelectUndeterminedTextLanguage() const {
    return true;
  }

  /**
   * Returns the tracks of the current media item and the current selection.
   *
   * <p>An empty value is returned before the first period is prepared. The default implementation
   * returns an empty snapshot for players that do not expose tracks.
   */
  [[nodiscard]] virtual Tracks getCurrentTracks() const {
    return {};
  }

  /**
   * Selects one track explicitly.
   *
   * @param rendererIndex the renderer index from {@link TrackInfo#rendererIndex}.
   * @param groupIndex the group index inside that renderer.
   * @param trackIndex the track index inside that group.
   */
  virtual void selectTrack(int rendererIndex, int groupIndex, int trackIndex) {
    static_cast<void>(rendererIndex);
    static_cast<void>(groupIndex);
    static_cast<void>(trackIndex);
  }

  /** Returns a renderer to automatic track selection. */
  virtual void clearTrackSelection(int rendererIndex) {
    static_cast<void>(rendererIndex);
  }

  [[nodiscard]] virtual std::shared_ptr<TextOutput> getTextOutput() const { return nullptr; }

  /** Sets the receiver of the timed metadata, or clears it with null. */
  virtual void setMetadataOutput(const std::shared_ptr<MetadataOutput>& output) {}

  [[nodiscard]] virtual std::shared_ptr<MetadataOutput> getMetadataOutput() const {
    return nullptr;
  }

  /**
   * Sets how the subtitles are shown on a platform that can draw them into the video frames.
   *
   * <p>The cue outputs are not affected: an output still receives every group. The mode only
   * decides whether the platform also burns the cues into the frames, which the player of this
   * module does on its CPU frame path.
   */
  virtual void setSubtitleRenderMode(SubtitleRenderMode mode) {}

  [[nodiscard]] virtual SubtitleRenderMode getSubtitleRenderMode() const {
    return SubtitleRenderMode::kAuto;
  }

  /**
   * Clears the playlist, adds the specified {@link MediaItem} and resets the position to
   * the default position.
   *
   * To replace a media item (possibly seamlessly) without clearing the playlist, use {@link
   * #replacePlayerItem}.
   *
   * @param mediaItem The new {@link MediaItem}.
   */
  virtual void setMediaItem(const std::shared_ptr<MediaItem>& mediaItem) = 0;

  /**
   * Prepares the player.
   *
   * This will move the player out of {@link STATE_IDLE idle state} and the player will start
   * loading media and acquire resources needed for playback.
   */
  virtual void prepare() = 0;

  /**
   * Returns the current playback state of the player.
   *
   * @return The current playback state.
   */
  [[nodiscard]] virtual const State& getPlaybackState() const = 0;

  /**
   * Returns the message of the source error that stopped playback, or an empty string when playback
   * has not been stopped by one.
   *
   * Mirrors ExoPlayer's {@code Player.getPlayerError()}, reduced to the message: an implementation
   * that has no error to report keeps the default empty message. The error is cleared when the
   * player is reset, prepared again, or stopped.
   *
   * @return The playback error message, or an empty string.
   */
  [[nodiscard]] virtual const std::string& getPlaybackErrorMessage() const {
    static const std::string sEmptyErrorMessage;
    return sEmptyErrorMessage;
  }

  /**
   * Returns whether the player is playing
   */
  [[nodiscard]] virtual bool isPlaying() const = 0;

  /**
   * Resumes playback as soon as {@link getPlaybackState()} == {@link STATE_READY}.
   *
   * <p>A player that is idle is prepared first. A player whose media has ended goes back to the default
   * position of its media and plays it again, so that an application that only offers a play button
   * does not have to know about {@link STATE_ENDED}. This is what ExoPlayer asks an application to write
   * as {@code seekToDefaultPosition()} followed by {@code play()}, done by {@link #play} itself; use
   * {@link setPlayWhenReady(boolean)} to only set whether the playback proceeds.
   */
  virtual void play() = 0;

  /**
   * Pauses playback. Equivalent to {@link setPlayWhenReady(boolean) setPlayWhenReady(false)}.
   */
  virtual void pause() = 0;

  /**
   * Stops playback without resetting the player. Use {@link pause()} rather than this method if
   * the intention is to pause playback.
   *
   * Calling this method will cause the playback state to transition to {@link STATE_IDLE}. The
   * player instance can still be used, and {@link release()} must still be called on the player if
   * it's no longer required.
   *
   * Calling this method does not clear the playlist, reset the playback position or the playback
   * error.
   */
  virtual void stop() = 0;

  /**
   * Releases the player. This method must be called when the player is no longer required. The
   * player must not be used after calling this method.
   */
  virtual void release() = 0;

  /**
   * Sets whether playback should proceed when {@link getPlaybackState()} == {@link STATE_READY}.
   *
   * If the player is already in the ready state then this method pauses and resumes playback.
   *
   * @param playWhenReady Whether playback should proceed when ready.
   */
  virtual void setPlayWhenReady(bool playWhenReady) = 0;

  /**
   * Whether playback will proceed when {@link getPlaybackState()} == {@link STATE_READY}.
   */
  [[nodiscard]] virtual bool getPlayWhenReady() const = 0;

  /**
   * Sets the {@link RepeatMode} to be used for playback.
   *
   * @param mode The repeat mode.
   */
  virtual void setRepeatMode(RepeatMode mode) = 0;

  /**
   * Returns the current {@link RepeatMode} used for playback.
   */
  [[nodiscard]] virtual const RepeatMode& getRepeatMode() const = 0;

  /**
   * Whether the player is currently loading the source.
   */
  [[nodiscard]] virtual bool isLoading() const = 0;

  /**
   * Seeks to a position specified in microseconds in the current {@link MediaItem}.
   *
   * @param positionUs The seek position in the current {@link MediaItem}
   */
  virtual void seekTo(int64_t positionUs) = 0;

  /**
   * Seeks to the default position of the current {@link MediaItem}.
   *
   * <p>Mirrors ExoPlayer's {@code Player.seekToDefaultPosition}: the default position depends on the
   * media. It is the start of a normal media, and typically the live edge of a live stream.
   */
  virtual void seekToDefaultPosition() = 0;

  /**
   * Attempts to set the playback parameters. Passing {@link Parameters()} resets the
   * player to the default, which means there is no speed or pitch adjustment.
   *
   * @param playbackParameters The playback parameters.
   */
  virtual void setPlaybackParameters(const Parameters& playbackParameters) = 0;

  /**
   * Returns the currently active playback parameters.
   */
  [[nodiscard]] virtual const Parameters& getPlaybackParameters() const = 0;

  /** A half-open interval of media that is buffered and can be seeked into without loading. */
  struct BufferedRange {
    int64_t startUs = 0;
    int64_t endUs = 0;
  };

  /**
   * Returns how far the player has buffered from the current position.
   *
   * <p>This is the end of the contiguous buffered range that contains the current position, or the
   * current position when nothing is buffered ahead. A buffered seek bar uses it together with
   * {@link #getCurrentPositionUs}.
   */
  [[nodiscard]] virtual int64_t getBufferedPositionUs() const {
    return getCurrentPositionUs();
  }

  /**
   * Returns the buffered intervals of the current media item.
   *
   * <p>The default implementation reports the single range between the current and buffered
   * positions, which is what a progressive source has. A player that knows several ranges (for
   * example after a seek into a cached part) overrides it.
   */
  [[nodiscard]] virtual std::vector<BufferedRange> getBufferedRanges() const {
    const auto positionUs = getCurrentPositionUs();
    const auto bufferedPositionUs = getBufferedPositionUs();
    if (bufferedPositionUs > positionUs) {
      return { BufferedRange { positionUs, bufferedPositionUs } };
    }
    return {};
  }

  /**
   * Gets the current playback position.
   *
   * @return the current position in microseconds
   */
  [[nodiscard]] virtual int64_t getCurrentPositionUs() const = 0;

  /**
   * Gets the duration of the file.
   *
   * @return the duration in microseconds, if no duration is available
   * (for example, if streaming live content), -1 is returned.
   */
  [[nodiscard]] virtual int64_t getDurationUs() const = 0;

  /**
   * Returns the volume of the player.
   */
  [[nodiscard]] virtual float getVolume() const = 0;

  /**
   * Sets the volume on this player.
   *
   * Note that the passed volume values are raw scalars in range 0.0 to 1.0.
   * UI controls should be scaled logarithmically.
   *
   * @param leftVolume left volume scalar
   * @param rightVolume right volume scalar
   */
  void setVolume(const float leftVolume, const float rightVolume) {
    setVolume({leftVolume, rightVolume});
  }

  /**
   * Sets the volume on this player.
   */
  virtual void setVolume(const Volume& volume) = 0;

  /**
   * Returns the width of the video
   *
   * @return If there is no video, or the width has not been determined yet, returns 0.
   */
  [[nodiscard]] virtual int getVideoWidth() const = 0;

  /**
   * Returns the height of the video.
   *
   * @return If there is no video, or the height has not been determined yet, returns 0.
   */
  [[nodiscard]] virtual int getVideoHeight() const = 0;

  /**
   * Registers a listener of the events of this player.
   *
   * @param listener The listener to register.
   */
  virtual void addListener(const std::shared_ptr<EventListener>& listener) = 0;

  /**
   * Unregisters a listener that was registered with {@link #addListener}.
   *
   * @param listener The listener to unregister.
   */
  virtual void removeListener(const std::shared_ptr<EventListener>& listener) = 0;

  /**
   * Registers a listener of the load events of the media sources this player loads.
   *
   * <p>Mirrors ExoPlayer's {@code Player#addMediaSourceEventListener}. The window index and the
   * media period identifier of the events are the ones of the timeline the player reports, which
   * the player remaps for a playlist of several sources.
   *
   * @param listener The listener to register.
   */
  virtual void addMediaSourceEventListener(
      const std::shared_ptr<MediaSourceEventListener>& listener) = 0;

  /**
   * Unregisters a listener that was registered with {@link #addMediaSourceEventListener}.
   *
   * @param listener The listener to unregister.
   */
  virtual void removeMediaSourceEventListener(
      const std::shared_ptr<MediaSourceEventListener>& listener) = 0;

  /**
   * Registers a listener of the events of the player and of its media sources.
   *
   * <p>The listener is called on the playback thread, with every event of both kinds and the
   * {@link EventTime} that puts it in context.
   *
   * @param listener The listener to register.
   */
  virtual void addAnalyticsListener(const std::shared_ptr<AnalyticsListener>& listener) = 0;

  /**
   * Unregisters an analytics listener.
   *
   * @param listener The listener to unregister.
   */
  virtual void removeAnalyticsListener(const std::shared_ptr<AnalyticsListener>& listener) = 0;

  // ================================================================================================
  // The playlist
  // ================================================================================================
  // Mirrors ExoPlayer's playlist API. The media items form the playlist the player plays in order;
  // `setMediaItem` replaces the whole list with a single item.

  /**
   * Returns the number of media items in the playlist.
   *
   * <p>The playlist of a player that was not given an item is empty, or holds one placeholder item.
   */
  [[nodiscard]] virtual int getMediaItemCount() const = 0;

  /**
   * Returns the media item at the given index in the playlist.
   *
   * @param index The index of the item. Must be in the range
   *              {@code [0, getMediaItemCount() - 1]}.
   *
   * @return The media item, or null when the index is out of range.
   */
  [[nodiscard]] virtual const std::shared_ptr<MediaItem>& getMediaItemAt(int index) const = 0;

  /**
   * Adds media items to the playlist, starting at the given index.
   *
   * <p>The added items are played in the order they are given, after the item that precedes the
   * index.
   *
   * @param index The index at which the items are inserted, in the range
   *              {@code [0, getMediaItemCount()]}.
   * @param mediaItems The items to add.
   */
  virtual void addMediaItems(int index,
      const std::vector<std::shared_ptr<MediaItem>>& mediaItems) = 0;

  /**
   * Removes a range of media items from the playlist.
   *
   * @param fromIndex The index of the first item to remove.
   * @param toIndex The index after the last item to remove.
   */
  virtual void removeMediaItems(int fromIndex, int toIndex) = 0;

  /**
   * Moves a range of media items to a new index.
   *
   * @param fromIndex The index of the first item to move.
   * @param toIndex The index after the last item to move.
   * @param newIndex The index the range is inserted at, after it was removed.
   */
  virtual void moveMediaItems(int fromIndex, int toIndex, int newIndex) = 0;

  /** Removes every media item from the playlist. */
  virtual void clearMediaItems() = 0;

  /** Adds one media item to the end of the playlist. */
  void addMediaItem(const std::shared_ptr<MediaItem>& mediaItem) {
    addMediaItems(getMediaItemCount(), { mediaItem });
  }

  /** Adds one media item at the given index. */
  void addMediaItem(int index, const std::shared_ptr<MediaItem>& mediaItem) {
    addMediaItems(index, { mediaItem });
  }

  /** Removes the media item at the given index. */
  void removeMediaItem(int index) {
    removeMediaItems(index, index + 1);
  }

  /** Moves the media item at {@code currentIndex} to {@code newIndex}. */
  void moveMediaItem(int currentIndex, int newIndex) {
    moveMediaItems(currentIndex, currentIndex + 1, newIndex);
  }
};

} // namespace coral

#endif //CORAL_PLAYBACK_PLAYER_H
