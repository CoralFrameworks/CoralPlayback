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
#ifndef CORAL_PLAYBACK_DETAILS_LIVE_PLAYER_H
#define CORAL_PLAYBACK_DETAILS_LIVE_PLAYER_H

#include <coral/analytics/AnalyticsCollector.h>
#include <coral/playback/Player.h>

#include <coral/playback/DefaultMediaClock.h>
#include <coral/playback/LoadControl.h>
#include <coral/playback/PlaybackInfo.h>
#include <coral/playback/MetadataOutput.h>
#include <coral/playback/Renderer.h>
#include <coral/playback/TextOutput.h>
#include <coral/selection/TrackSelector.h>

#include <extractor/text/CueTimeline.h>
#include <coral/source/MediaPeriod.h>
#include <coral/source/MediaSourceFactory.h>
#include <core/task/CountDownLatch.h>
#include <core/task/Mutex.h>

#include <core/event/EventHandler.h>
#include <core/event/EventThread.h>

#include "MediaPeriodQueue.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace coral {

class MetadataRenderer;
class TextRenderer;
class EffectEngine;
class VideoEffectGraph;
class VideoOutputNodeFactory;
struct LottieOverlayConfig;
struct VideoEffectInput;

class AvLivePlayer final : public Player,
                           public DefaultMediaClock::PlaybackParametersListener,
                           public Renderer::VideoColorInfoListener,
                           public MediaPeriod::Callback,
                           public MediaPeriodHolder::Factory,
                           public MediaSourceListInfoRefreshListener,
                           public EventHandler::Callback,
                           public Renderer::VideoSizeListener,
                           public TrackSelector::InvalidationListener {
public:
  explicit AvLivePlayer(const std::vector<std::shared_ptr<Renderer>>& renderers) noexcept;

  ~AvLivePlayer() override;

  /**
   * Enables the EffectEngine video path.
   *
   * <p>Must be called before `prepare()`. The graph starts with a CPU input node and a headless
   * null output; a later `setOutputSurface` asks @p factory for the platform output node and
   * replaces it. Passing null keeps the graph headless, which is useful for tests and readback.
   */
  void enableVideoEffectPipeline(const std::shared_ptr<VideoOutputNodeFactory>& factory = nullptr);

  /**
   * Enables the effect pipeline with an aggregated platform input.
   *
   * <p>The input carries the graph's input node, the decoder surface the platform sink samples, the
   * sink itself, and an optional platform graphics context. A missing sink is filled with the CPU
   * sink when the input node is a `VideoFrameInputNode`; a missing input creates the default CPU
   * input.
   */
  void enableVideoEffectPipeline(const std::shared_ptr<VideoOutputNodeFactory>& factory,
                                 const VideoEffectInput& input);

  [[nodiscard]] bool isVideoEffectPipelineEnabled() const noexcept {
    return videoEffectGraph != nullptr;
  }

  [[nodiscard]] const std::shared_ptr<EffectEngine>& getEffectEngine() const noexcept {
    return effectEngine;
  }

  [[nodiscard]] bool isSubtitleOverlayActive() const;

  bool addLottieOverlay(const LottieOverlayConfig& config);

  void clearLottieOverlays();

  void setOutputSurface(const std::shared_ptr<VideoSurface>& surface) override;

  void setVideoScalingMode(int mode) override;

  void setVideoScaleMode(VideoScaleMode mode) override;

  [[nodiscard]] VideoScaleMode getVideoScaleMode() const override {
    return videoScaleMode.load(std::memory_order_relaxed);
  }

  void setVideoMirror(bool horizontal, bool vertical) override;

  void setTextOutput(const std::shared_ptr<TextOutput>& output) override;

  [[nodiscard]] Tracks getCurrentTracks() const override;

  [[nodiscard]] VideoFrameOutput getVideoFrameOutput() const override;

  [[nodiscard]] VideoColorInfo getVideoColorInfo() const override;

  [[nodiscard]] std::vector<Chapter> getChapters() const override;

  void setThumbnailProvider(std::shared_ptr<ThumbnailProvider> provider) override;

  [[nodiscard]] std::shared_ptr<ThumbnailProvider> getThumbnailProvider() const override;

  void requestThumbnail(int64_t positionUs, ThumbnailProvider::Callback callback) override;

  void prefetchThumbnail(int64_t positionUs) override;

  void setPreferredAudioLanguage(const std::string& language) override;

  [[nodiscard]] std::string getPreferredAudioLanguage() const override;

  [[nodiscard]] std::string getPreferredTextLanguage() const override;

  void setPreferredTextRoleFlags(int roleFlags) override;

  [[nodiscard]] int getPreferredTextRoleFlags() const override;

  void setSelectUndeterminedTextLanguage(bool select) override;

  [[nodiscard]] bool getSelectUndeterminedTextLanguage() const override;

  void selectTrack(int rendererIndex, int groupIndex, int trackIndex) override;

  void clearTrackSelection(int rendererIndex) override;

  void setPreferredTextLanguage(const std::string& language, bool selectUndetermined) override;

  void setSubtitleEnabled(bool enabled) override;

  [[nodiscard]] bool isSubtitleEnabled() const override;

  void setSubtitleFile(const std::string& path, const std::string& mimeType);

  void dispatchCueGroup(const AVCueGroup& group);

  [[nodiscard]] std::shared_ptr<TextOutput> getTextOutput() const override;

  void setMetadataOutput(const std::shared_ptr<MetadataOutput>& output) override;

  [[nodiscard]] std::shared_ptr<MetadataOutput> getMetadataOutput() const override;

  void setSubtitleRenderMode(SubtitleRenderMode mode) override;

  [[nodiscard]] SubtitleRenderMode getSubtitleRenderMode() const override {
    return subtitleRenderMode;
  }

  void setMediaItem(const std::shared_ptr<MediaItem>& mediaItem) override;

  void prepare() override;

  [[nodiscard]] const State& getPlaybackState() const override {
    return playbackInfo.playbackState;
  }

  [[nodiscard]] bool isPlaying() const override {
    return playbackInfo.isPlaying();
  }

  void play() override;

  void pause() override;

  void stop() override;

  void release() override;

  void setPlayWhenReady(bool enable) override;

  [[nodiscard]] bool getPlayWhenReady() const override {
    return playbackInfo.playWhenReady;
  }

  void setRepeatMode(RepeatMode mode) override;

  [[nodiscard]] const RepeatMode& getRepeatMode() const override {
    return repeatMode;
  }

  [[nodiscard]] bool isLoading() const override {
    return playbackInfo.isLoading;
  }

  void seekTo(int64_t timeUs) override;

  void seekToDefaultPosition() override;

  void setPlaybackParameters(const Parameters& parameters) override;

  [[nodiscard]] const Parameters& getPlaybackParameters() const override {
    return playbackInfo.parameters;
  }

  [[nodiscard]] int64_t getCurrentPositionUs() const override {
    return playbackInfo.positionUs;
  }

  [[nodiscard]] int64_t getBufferedPositionUs() const override {
    return playbackInfo.bufferedPositionUs;
  }

  [[nodiscard]] std::vector<BufferedRange> getBufferedRanges() const override {
    const auto positionUs = playbackInfo.positionUs;
    const auto bufferedPositionUs = playbackInfo.bufferedPositionUs;
    if (bufferedPositionUs > positionUs) {
      return { BufferedRange { positionUs, bufferedPositionUs } };
    }
    return {};
  }

  int64_t getDurationUs() const override {
    auto playingHolder = queue->getPlayingPeriod();
    if (playingHolder != nullptr) {
      return playingHolder->getDurationUs();
    }
    return 0;
  }

  const PlaybackInfo& getPlaybackInfo() const {
    return playbackInfo;
  }

  [[nodiscard]] float getVolume() const override {
    return mVolume.value();
  }

  void setVolume(const Volume& volume) override;

  [[nodiscard]] int getVideoWidth() const override {
    return mVideoWidth;
  }

  [[nodiscard]] int getVideoHeight() const override {
    return mVideoHeight;
  }

  [[nodiscard]] const std::string& getPlaybackErrorMessage() const override {
    return playbackInfo.playbackErrorMessage;
  }

  /**
   * Sets the factory that creates the {@link MediaSource} of the media items this player is given.
   *
   * <p>Mirrors ExoPlayer's {@code ExoPlayer#setMediaSourceFactory}. The factory is used by {@link
   * #setMediaItem}, so it has to be set before the item is.
   *
   * @param factory The factory to use, or null to keep the current one.
   */
  void setMediaSourceFactory(const std::shared_ptr<MediaSourceFactory>& factory) override;

  void addListener(const std::shared_ptr<EventListener>& listener) override;

  void removeListener(const std::shared_ptr<EventListener>& listener) override;

  void addMediaSourceEventListener(
    const std::shared_ptr<MediaSourceEventListener>& listener) override;

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

  void onPrepared(const std::shared_ptr<MediaPeriod>& mediaPeriod) override;

  void onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& period) override;

  void onTrackSelectionsInvalidated() override;

  /**
   * Records the size of the decoded video and reports it to the listeners of the player.
   *
   * <p>Called by a video renderer from the thread it renders on, so the size is only stored here:
   * the notification is posted to the playback thread.
   */
  void onVideoSizeChanged(int width, int height) override;

  void onVideoColorInfoChanged(const VideoColorInfo& colorInfo) override;

  void onPlaybackParametersChanged(const Parameters& newParameters) override;

  void onPlaylistUpdateRequested() override;

  bool handleMessage(Message* msg) override;

  std::shared_ptr<MediaPeriodHolder> createMediaPeriodHolder(
    const MediaPeriodInfo& info, int64_t rendererPositionOffsetUs) override;

private:
  void setMediaSource(const std::shared_ptr<MediaSource>& mediaSource);

  /**
   * Delivers one event to every registered listener of the player.
   *
   * <p>The listeners are called on the looper of the thread that created the player, or inline on
   * the calling thread when that thread had no looper. The dispatch never runs under the lock that
   * guards the listener list.
   *
   * @param event The closure that calls one listener.
   */
  /**
   * Collects a notification for the listeners of the player instead of delivering it right away.
   *
   * <p>The player changes its state several times while it does its work. Collecting the notifications
   * and delivering them together at the end of the iteration means a listener that reads the state of
   * the player from a callback sees the state the iteration ends in, which is what ExoPlayer's
   * `PlaybackInfoUpdate` does. {@link #flushPlayerEvents} delivers them.
   */
  void dispatchPlayerEvent(const std::function<void(EventListener&)>& event);

  /** Delivers the notifications collected by {@link #dispatchPlayerEvent}, in the order they arrived. */
  void flushPlayerEvents();

  void handleAddMediaSourceEventListener(
    const std::shared_ptr<MediaSourceEventListener>& listener);

  void handleRemoveMediaSourceEventListener(
    const std::shared_ptr<MediaSourceEventListener>& listener);

  /** Applies a new repeat mode. Called on the playback thread. */
  void setRepeatModeInternal(const RepeatMode& mode);

  /**
   * The parameters of a playlist change: the range it applies to and the items it adds.
   */
  struct MediaItemChange {
    int fromIndex = 0;
    int toIndex = 0;
    int newIndex = 0;
    std::vector<std::shared_ptr<MediaItem>> mediaItems;
  };

  void addMediaItemsInternal(const MediaItemChange& change);

  void removeMediaItemsInternal(const MediaItemChange& change);

  void moveMediaItemsInternal(const MediaItemChange& change);

  void clearMediaItemsInternal();

  /** Sends the renderer that receives the frames, wrapping it in the overlay when burning. */
  /** Records the size of the decoded video and reports it. Called on the playback thread. */
  void setVideoSizeInternal(int width, int height);

  void setState(State newState);

  void quitThread();

  void maybeNotifyPlaybackInfoChanged();

  void prepareInternal();

  /**
   * Applies a {@code play()} request on the playback thread.
   *
   * <p>A player that is idle is prepared, and a player that has ended goes back to the default position
   * of its media before the playback is resumed. That is what an application asks for with
   * {@code play()}, and it is what ExoPlayer expresses as {@code seekToDefaultPosition()} followed by
   * {@code play()}.
   */
  void playInternal();

  void mediaSourceListUpdateRequestedInternal();

  void setPlayWhenReadyInternal(bool enable);

  void seekToCurrentPosition(bool sendDiscontinuity);

  void seekToInternal(int64_t timeUs);

  /**
   * Applies {@code seekToDefaultPosition()} on the playback thread.
   *
   * <p>Mirrors ExoPlayer's {@code Player.seekToDefaultPosition}: the seek goes to the position the
   * media declares as its default one, which is the start of a normal media and the live edge of a live
   * one.
   */
  void seekToDefaultPositionInternal();

  void updatePlaybackPositions();

  void doSomeWork();

  void scheduleNextWork(TimePoint operationStartTime);

  void stopInternal(bool forceResetRenderers, bool acknowledgeStop);

  void releaseInternal();

  void updateVolumeInternal(const Message* msg);

  void setSurfaceInternal(const Message* msg);

  void applyVideoEffectSurface(const std::shared_ptr<VideoSurface>& surface);

  /** Runs on the engine's render thread after a frame; updates the UI hand-off sequence. */
  void onVideoFrameRendered(std::shared_ptr<Fence> fence);

  void setVideoScalingModeInternal(const Message* msg);

  void setVideoMirrorInternal(int flags);

  void setVideoColorInfoInternal(const VideoColorInfo& colorInfo);

  struct TrackSelectionRequest {
    int rendererIndex = 0;
    int groupIndex = 0;
    int trackIndex = 0;
  };

  void selectTrackInternal(const TrackSelectionRequest& request);

  void clearTrackSelectionInternal(int rendererIndex);

  /** Builds the public snapshot from the selector's current mapping and result. */
  [[nodiscard]] Tracks buildCurrentTracks() const;

  /** Delivers the current snapshot to the listeners. */
  void dispatchTracksChanged();

  [[nodiscard]] std::vector<Chapter> buildChapters() const;

  void dispatchChaptersChanged();

  void setMediaItemInternal(const std::shared_ptr<MediaSource>& mediaSource);

  int64_t seekToPeriodPosition(
    const MediaPeriodId& periodId, int64_t periodPositionUs, bool forceBufferingState);

  int64_t seekToPeriodPosition(
    const MediaPeriodId& periodId,
    int64_t periodPositionUs,
    bool forceDisableRenderers,
    bool forceBufferingState);

  void resetRendererPosition(int64_t positionUs);

  void setParametersInternal(const Parameters& parameters);

  bool shouldTransitionToReadyState(bool renderersReadyOrEnded) const;

  bool isTimelineReady() const;

  void handleMediaSourceListInfoRefreshed(const std::shared_ptr<Timeline>& timeline);

  int64_t getMaxRendererReadPositionUs() const;

  void updatePeriods();

  bool maybeUpdateLoadingPeriod();

  void maybeUpdateReadingPeriod();

  void maybeUpdateReadingRenderers();

  bool replaceStreamsOrDisableRendererForTransition();

  void maybeUpdatePlayingPeriod();

  void resetPendingPauseAtEndOfPeriod();

  bool shouldAdvancePlayingPeriod();

  bool hasReadingPeriodFinishedReading();

  void setAllRendererStreamsFinal();

  void handlePeriodPrepared(const std::shared_ptr<MediaPeriod>& mediaPeriod);

  void handleContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& mediaPeriod);

  /**
   * Handles a fatal source error: stops playback and records the error on the playback info.
   *
   * @param errorMessage The message of the error.
   */
  void handleSourceError(const std::string& errorMessage);

  /**
   * Asks the period of a holder whether it can be prepared, and reports the failure to the listeners.
   *
   * <p>Mirrors the way ExoPlayer's {@code ExoPlayerImplInternal} calls
   * {@code MediaPeriod.maybeThrowPrepareError()} and turns the thrown {@code IOException} into a
   * playback error: a period that cannot be prepared throws, and this is where the player finds out.
   *
   * @param holder The holder of the period to ask, or null.
   * @return Whether the period reported a fatal error, which stops the current iteration.
   */
  bool maybeThrowPeriodError(const std::shared_ptr<MediaPeriodHolder>& holder);

  void handlePlaybackParameters(const Parameters& parameters, bool acknowledgeCommand);

  void maybeContinueLoading();

  void updateIsLoading();

  PlaybackInfo handlePositionDiscontinuity(
    const MediaPeriodId& id, int64_t positionUs, int64_t contentPositionUs);

  void enableRenderers();

  void enableRenderers(const std::vector<bool>& rendererWasEnabledFlags);

  void enableRenderer(size_t rendererIndex, bool wasRendererEnabled);

  void startRenderers();

  void stopRenderers() const;

  void disableRenderer(const std::shared_ptr<Renderer>& renderer);

  /**
   * Unregisters the listeners that point back at this player from every renderer.
   *
   * <p>The renderers are owned by the caller and outlive the player, and they call their listeners
   * from the threads they render on, so a listener that points back at the player must be removed
   * before the player goes away.
   */
  void clearRendererListeners();

  std::pair<MediaPeriodId, int64_t>
  getPlaceholderFirstMediaPeriodPosition(const std::shared_ptr<Timeline>& timeline) const;

  void reselectTracksInternal();

  void updateTrackSelectionPlaybackSpeed(float playbackSpeed) const;

  void notifyTrackSelectionDiscontinuity() const;

  void resetInternal(bool resetRenderers, bool resetPosition, bool releaseMediaSourceList,
    bool resetError);

  void handleLoadingMediaPeriodChanged(bool loadingTrackSelectionChanged);

  int64_t getTotalBufferedDurationUs() {
    return getTotalBufferedDurationUs(playbackInfo.bufferedPositionUs);
  }

  int64_t getTotalBufferedDurationUs(int64_t bufferedPositionInLoadingPeriodUs) const;

  void updateLoadControlTrackSelection(
    const std::shared_ptr<TrackGroupArray>& trackGroups,
    const std::shared_ptr<TrackSelectorResult>& trackSelectorResult) const;

  void sendParametersChangedInternal(const Parameters& parameters, bool acknowledgeCommand);

  bool shouldPlayWhenReady() const { return playbackInfo.playWhenReady; }

  void updateRebufferingState(const bool rebuffering, const bool resetLastRebufferRealtimeMs) {
    isRebuffering = rebuffering;
    lastRebufferRealtimeMs = rebuffering && !resetLastRebufferRealtimeMs
                               ? SystemClock::ElapsedRealtime()
                               : Definitions::kTimeUnset;
  }

  static bool isLoadingPossible(const std::shared_ptr<MediaPeriodHolder>& mediaPeriodHolder);

  static void ensureStopped(const std::shared_ptr<Renderer>& renderer);

  static bool isRendererEnabled(const std::shared_ptr<Renderer>& renderer);

  static std::vector<std::shared_ptr<MediaFormat>>
  getFormats(const std::shared_ptr<TrackSelection>& trackSelection);

  static const char* getName() { return "LivePlayer"; }

  /**
   * Threshold under which a buffered duration is assumed to be empty. We cannot use zero to
   * account for buffers currently hold but not played by the renderer.
   */
  static constexpr int64_t PLAYBACK_BUFFER_EMPTY_THRESHOLD_US = 500000;

  struct SeekPosition {
    std::shared_ptr<Timeline> timeline;
    int windowIndex;
    int64_t windowPositionUs;

    SeekPosition()
      : windowIndex(Definitions::kIndexUnset),
        windowPositionUs(Definitions::kTimeUnset) {
    }

    explicit SeekPosition(const std::shared_ptr<Timeline>& timeline,
      int windowIndex,
      int64_t windowPositionUs) noexcept
      : timeline(timeline),
        windowIndex(windowIndex),
        windowPositionUs(windowPositionUs) {
    }

    SeekPosition(const SeekPosition& other) = default;
    SeekPosition& operator=(const SeekPosition& other) = default;
  };

  std::unique_ptr<EventThread> msgThread;
  /**
   * A non-owning reference to this player, kept alive for as long as the handler may dispatch to it.
   *
   * A handler holds its callback weakly, so this reference is what keeps the callback reachable. It
   * must not own the player, which belongs to the caller. Declared before `msgHandler`, so it
   * outlives it.
   */
  std::shared_ptr<EventHandler::Callback> callbackRef;
  std::shared_ptr<EventHandler> msgHandler;

  /**
   * A non-owning reference to this player as the renderers' video size listener.
   *
   * <p>Built with a no-op deleter like {@link #callbackRef}: the renderers hold it, and it must not
   * destroy the player, which is owned by its caller. It is cleared from the renderers before the
   * player is destroyed.
   */
  std::shared_ptr<Renderer::VideoSizeListener> videoSizeListenerRef;

  /**
   * The handler the application events are delivered on, or null when the thread that created the
   * player has no looper. Built from the looper of that thread, like ExoPlayer's listener handler.
   */
  std::shared_ptr<EventHandler> appHandler;

  /** Guards {@link #playerListeners}, which the application thread reads and adds to. */
  mutable Mutex playerListenersLock;
  std::vector<std::shared_ptr<EventListener>> playerListeners;

  /**
   * The notifications of the current iteration of the playback thread, in the order they happened.
   *
   * <p>Only touched from the playback thread, and delivered by {@link #flushPlayerEvents} at the end of
   * the iteration.
   */
  std::vector<std::function<void(EventListener&)>> pendingPlayerEvents;

  /** The listeners of the media source loads, only touched on the playback thread. */
  std::vector<std::shared_ptr<MediaSourceEventListener>> mediaSourceEventListeners;

  /**
   * The collector that fans the events of the player and of its media sources out to the analytics
   * listeners. It is registered as a media source event listener, so it receives both kinds.
   */
  std::shared_ptr<AnalyticsCollector> analyticsCollector;

  /** Creates the media source of the media items, see {@link #setMediaSourceFactory}. */
  std::shared_ptr<MediaSourceFactory> mediaSourceFactory;

  mutable CountDownLatch threadBarrier;
  std::atomic_bool released;

  /** The effect engine and graph when `enableVideoEffectPipeline` was called. */
  std::shared_ptr<EffectEngine> effectEngine;
  std::shared_ptr<VideoEffectGraph> videoEffectGraph;
  std::shared_ptr<VideoOutputNodeFactory> videoOutputFactory;
  /** The last surface handed to the player, so enabling the graph can apply it retroactively. */
  std::shared_ptr<VideoSurface> pendingOutputSurface;

  std::shared_ptr<ShuffleOrder> shuffleOrder;
  std::vector<std::shared_ptr<Renderer>> renderers;
  /** The renderer of the subtitle track, when the player was created with one. */
  std::shared_ptr<TextRenderer> textRenderer;
  std::mutex externalMutex;
  std::shared_ptr<AVCueTimeline> externalTimeline;
  AVCueGroup lastExternalGroup;
  /** The renderer of the timed metadata track, when the player was created with one. */
  std::shared_ptr<MetadataRenderer> metadataRenderer;
  /** Guards the outputs, which the application thread sets and the playback thread reads. */
  mutable Mutex outputLock;
  std::shared_ptr<TextOutput> textOutput;
  std::string preferredAudioLanguage;
  std::string preferredTextLanguage;
  bool selectUndeterminedTextLanguage = true;
  int preferredTextRoleFlags = 0;
  std::atomic<bool> subtitlesEnabled{true};
  std::shared_ptr<MetadataOutput> metadataOutput;
  /** The renderer of the application, before the subtitle overlay wraps it. */
  /** The last cue group delivered, so the effect pipeline can be enabled after it. */
  AVCueGroup currentCueGroup;
  VideoFrameOutput videoFrameOutput;
  VideoColorInfo videoColorInfo;
  std::vector<Chapter> chapters;
  std::shared_ptr<ThumbnailProvider> thumbnailProvider;
  std::shared_ptr<Renderer::VideoColorInfoListener> videoColorInfoListenerRef;
  std::atomic<int64_t> pendingVideoFramePositionUs { 0 };
  std::atomic<SubtitleRenderMode> subtitleRenderMode{ SubtitleRenderMode::kAuto };
  std::atomic<VideoScaleMode> videoScaleMode{ VideoScaleMode::Fit };
  std::vector<std::shared_ptr<SelectionCapabilities>> rendererCapabilities;
  std::shared_ptr<TrackSelector> trackSelector;
  std::shared_ptr<TrackSelectorResult> emptyTrackSelectorResult;
  std::shared_ptr<LoadControl> loadControl;
  std::shared_ptr<BandwidthMeter> bandwidthMeter;
  std::shared_ptr<Timeline::Window> window;
  std::shared_ptr<Timeline::Period> period;
  int64_t backBufferDurationUs;
  bool retainBackBufferFromKeyframe;
  std::unique_ptr<DefaultMediaClock> mediaClock;
  std::shared_ptr<MediaPeriodQueue> queue;
  std::shared_ptr<MediaSourceList> mediaSourceList;

  int mVideoWidth = 0;
  int mVideoHeight = 0;
  Volume mVolume{};
  PlaybackInfo playbackInfo{};
  bool pendingPauseAtEndOfPeriod;
  bool isRebuffering;
  int64_t lastRebufferRealtimeMs;
  bool shouldContinueLoading;
  RepeatMode repeatMode;
  bool shuffleModeEnabled;
  bool requestForRendererSleep;
  bool offloadSchedulingEnabled;

  int enabledRendererCount;
  std::optional<SeekPosition> pendingInitialSeekPosition;
  int64_t rendererPositionUs;

  //!< internal messages
  static constexpr int MSG_PREPARE = 0;
  static constexpr int MSG_SET_PLAY_WHEN_READY = 1;
  static constexpr int MSG_DO_SOME_WORK = 2;
  static constexpr int MSG_STOP = 3;
  static constexpr int MSG_RELEASE = 4;
  static constexpr int MSG_PLAYBACK_PARAMETERS = 5;
  static constexpr int MSG_UPDATE_VOLUME = 6;
  static constexpr int MSG_SEEK_TO_POSITION = 7;
  static constexpr int MSG_SET_SURFACE = 8;
  static constexpr int MSG_SET_VIDEO_SCALE_MODE = 9;
  static constexpr int MSG_SET_MEDIA_SOURCE = 11;
  static constexpr int MSG_PERIOD_PREPARED = 12;
  static constexpr int MSG_SOURCE_CONTINUE_LOADING_REQUESTED = 13;
  static constexpr int MSG_TRACK_SELECTION_INVALIDATED = 14;
  static constexpr int MSG_SET_PREFERRED_TEXT_LANGUAGE = 16;
  static constexpr int MSG_PLAYLIST_UPDATE_REQUESTED = 15;

  /** Carries the message of a fatal source error. */
  /**
   * The type of the message that carried a fatal source error.
   *
   * <p>Unused: a period reports its fatal error by throwing from
   * {@link MediaPeriod::maybeThrowPrepareError}, which the playback thread reads itself. The value is
   * kept so that the numbers of the other messages do not change.
   */
  static constexpr int MSG_SOURCE_ERROR = 17;

  /** Carries the listener of the media source loads to register. */
  static constexpr int MSG_ADD_MEDIA_SOURCE_EVENT_LISTENER = 18;

  /** Carries the listener of the media source loads to unregister. */
  static constexpr int MSG_REMOVE_MEDIA_SOURCE_EVENT_LISTENER = 19;

  /** Carries the new repeat mode. */
  static constexpr int MSG_SET_REPEAT_MODE = 20;

  /** Carries the size of the decoded video that a renderer reported. */
  static constexpr int MSG_VIDEO_SIZE_CHANGED = 21;

  /** Carries the media items to add and the index to add them at. */
  static constexpr int MSG_ADD_MEDIA_ITEMS = 22;

  /** Carries the range of media items to remove. */
  static constexpr int MSG_REMOVE_MEDIA_ITEMS = 23;

  /** Carries the range of media items to move and the index to move them to. */
  static constexpr int MSG_MOVE_MEDIA_ITEMS = 24;

  /** Has no data: clears the playlist. */
  static constexpr int MSG_CLEAR_MEDIA_ITEMS = 25;

  /** Has no data: seeks to the default position of the current media item. */
  static constexpr int MSG_SEEK_TO_DEFAULT_POSITION = 26;

  /** Has no data: resumes the playback, restarting an ended player from the default position. */
  static constexpr int MSG_PLAY = 27;
  static constexpr int MSG_SELECT_TRACK = 28;
  static constexpr int MSG_CLEAR_TRACK_SELECTION = 29;
  static constexpr int MSG_SET_VIDEO_MIRROR = 30;
  static constexpr int MSG_VIDEO_COLOR_INFO_CHANGED = 31;
  static constexpr int MSG_SET_PREFERRED_AUDIO_LANGUAGE = 32;

  static constexpr int64_t ACTIVE_INTERVAL_MS = 10;
  static constexpr int64_t IDLE_INTERVAL_MS = 1000;

  /**
   * Duration under which pausing the main DO_SOME_WORK loop is not expected to yield significant
   * power saving.
   *
   * This value is probably too high, power measurements are needed adjust it, but as renderer
   * sleep is currently only implemented for audio offload, which uses buffer much bigger than 2s,
   * this does not matter for now.
   */
  static constexpr auto MIN_RENDERER_SLEEP_DURATION_MS = 2000;

  struct PlaylistChange {
    MediaPeriodId periodId;
    int64_t periodPositionUs;
    int64_t requestedContentPositionUs;
    bool forceBufferingState;
    bool endPlayback;

    explicit PlaylistChange(MediaPeriodId periodId,
      const int64_t periodPositionUs,
      const int64_t requestedContentPositionUs,
      const bool forceBufferingState,
      const bool endPlayback) noexcept
      : periodId(std::move(periodId)),
        periodPositionUs(periodPositionUs),
        requestedContentPositionUs(requestedContentPositionUs),
        forceBufferingState(forceBufferingState),
        endPlayback(endPlayback) {
    }
  };

  static PlaylistChange resolvePositionForPlaylistChange(const std::shared_ptr<Timeline>& timeline,
    const PlaybackInfo& playbackInfo,
    const std::optional<SeekPosition>& pendingInitialSeekPosition,
    const std::shared_ptr<MediaPeriodQueue>& queue,
    const RepeatMode& repeatMode,
    bool shuffleModeEnabled,
    const std::shared_ptr<Timeline::Window>& window,
    const std::shared_ptr<Timeline::Period>& period);

  static bool shouldUseRequestedContentPosition(const PlaybackInfo& playbackInfo,
    const std::shared_ptr<Timeline::Period>& period,
    const std::shared_ptr<Timeline::Window>& window);

  /**
   * Converts a {@link SeekPosition} into the corresponding (periodUid, periodPositionUs) for the
   * internal timeline.
   *
   * @param seekPosition The position to resolve.
   * @param trySubsequentPeriods Whether the position can be resolved to a subsequent matching
   *     period if the original period is no longer available.
   * @return The resolved position, or null if resolution was not successful.
   */
  static std::pair<PeriodUid, int64_t>
  resolveSeekPositionUs(const std::shared_ptr<Timeline>& timeline,
    const std::optional<SeekPosition>& seekPosition,
    bool trySubsequentPeriods,
    const RepeatMode& repeatMode,
    bool shuffleModeEnabled,
    const std::shared_ptr<Timeline::Window>& window,
    const std::shared_ptr<Timeline::Period>& period);

  /**
   * Given a period index into an old timeline, finds the first subsequent period that also exists
   * in a new timeline. The uid of this period in the new timeline is returned.
   *
   * @param window A {@link Timeline.Window} to be used internally.
   * @param period A {@link Timeline.Period} to be used internally.
   * @param repeatMode The repeat mode to use.
   * @param shuffleModeEnabled Whether the shuffle mode is enabled.
   * @param oldPeriodUid The index of the period in the old timeline.
   * @param oldTimeline The old timeline.
   * @param newTimeline The new timeline.
   * @return The uid in the new timeline of the first subsequent period, or null if no such period
   *     was found.
   */
  static std::optional<PeriodUid>
  resolveSubsequentPeriod(const std::shared_ptr<Timeline::Window>& window,
    const std::shared_ptr<Timeline::Period>& period,
    const RepeatMode& repeatMode,
    bool shuffleModeEnabled,
    const PeriodUid& oldPeriodUid,
    const std::shared_ptr<Timeline>& oldTimeline,
    const std::shared_ptr<Timeline>& newTimeline);

  class WakeupListener final : public Renderer::WakeupListener {
  private:
    AvLivePlayer& player;

  public:
    explicit WakeupListener(AvLivePlayer& player) noexcept
      : player(player) {
    }

    void onSleep(int64_t wakeupDeadlineMs) override;

    void onWakeup() override;
  };
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_LIVE_PLAYER_H
