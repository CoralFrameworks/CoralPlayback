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
#include "AvLivePlayer.h"

#include "CpuVideoFrameSink.h"
#include "DecodeVideoRenderer.h"
#include "ImageRenderer.h"
#include "MetadataRenderer.h"
#include "SubtitleOverlayNode.h"
#include "TextRenderer.h"
#include "VideoEffectGraph.h"

#include <effect/EffectEngine.h>
#include <effect/NullOutputNode.h>
#include <effect/VideoFrameInputNode.h>

#include <coral/playback/VideoFrameSink.h>
#include <coral/playback/VideoOutput.h>

#include <extractor/text/DefaultSubtitleParserFactory.h>
#include <extractor/text/SubtitleParser.h>

#include <fstream>
#include <iterator>
#include "DefaultLoadControl.h"
#include "PlaylistTimeline.h"
#include "TraceUtil.h"

#include <coral/selection/DefaultTrackSelector.h>
#include <coral/selection/MappingTrackSelector.h>
#include <coral/selection/SelectionCapabilities.h>
#include <coral/selection/SelectionOverride.h>
#include <core/time/SystemClock.h>
#include <coral/upstream/DefaultBandwidthMeter.h>

#include <minilog/log.h>

#include <utility>

namespace coral {

AvLivePlayer::AvLivePlayer(const std::vector<std::shared_ptr<Renderer>>& renderers) noexcept
  : threadBarrier(1),
    released(false),
    renderers(renderers),
    pendingPauseAtEndOfPeriod(false),
    isRebuffering(false),
    lastRebufferRealtimeMs(Definitions::kTimeUnset),
    shouldContinueLoading(false),
    repeatMode(RepeatMode::REPEAT_MODE_OFF),
    shuffleModeEnabled(false),
    requestForRendererSleep(false),
    offloadSchedulingEnabled(false),
    enabledRendererCount(0),
    pendingInitialSeekPosition(std::nullopt),
    rendererPositionUs(0) {
  window = std::make_shared<Timeline::Window>();
  period = std::make_shared<Timeline::Period>();
  auto allocator =
    std::make_shared<DefaultAllocator>(true, DefaultLoadControl::DEFAULT_MUXED_BUFFER_SIZE);
  loadControl = std::make_shared<DefaultLoadControl>(allocator);
  backBufferDurationUs = loadControl->getBackBufferDurationUs();
  retainBackBufferFromKeyframe = loadControl->retainBackBufferFromKeyframe();

  auto configurations = std::vector<std::shared_ptr<Configuration>>(renderers.size());
  auto selections = std::vector<std::shared_ptr<TrackSelection>>(renderers.size());
  emptyTrackSelectorResult =
    std::make_shared<TrackSelectorResult>(configurations, selections, nullptr);

  bandwidthMeter = std::make_shared<DefaultBandwidthMeter>();
  mediaClock = std::make_unique<DefaultMediaClock>(this);
  shuffleOrder = std::make_shared<DefaultShuffleOrder>(0);

  rendererCapabilities.resize(renderers.size());
  for (auto i = 0; i < renderers.size(); ++i) {
    renderers[i]->setIndex(i);
    rendererCapabilities[i] = renderers[i]->getCapabilities();
    if (auto text = std::dynamic_pointer_cast<TextRenderer>(renderers[i]); text != nullptr) {
      textRenderer = std::move(text);
      // The renderer calls this from the playback thread, which is where its render is called.
      textRenderer->setCueListener([this](const AVCueGroup& group) {
        dispatchCueGroup(group);
      });
    } else if (auto metadata = std::dynamic_pointer_cast<MetadataRenderer>(renderers[i]);
               metadata != nullptr) {
      metadataRenderer = std::move(metadata);
      metadataRenderer->setMetadataListener([this](const Metadata& metadata) {
        {
          std::lock_guard<Mutex> lock(outputLock);
          if (metadataOutput != nullptr) {
            metadataOutput->onMetadata(metadata);
          }
        }
        const Metadata copy = metadata;
        dispatchPlayerEvent([copy](EventListener& listener) { listener.onMetadata(copy); });
        flushPlayerEvents();
      });
    }
  }
  trackSelector = std::make_shared<DefaultTrackSelector>();
  trackSelector->init(this, bandwidthMeter);

  queue = std::make_shared<MediaPeriodQueue>(this);
  mediaSourceList = std::make_shared<MediaSourceList>(this);

  // The analytics collector receives the high level events as a player listener and the media source
  // events as a listener of the playlist, so both kinds reach it with the same context. It is
  // registered before anything can be played, so it never misses an event.
  analyticsCollector = std::make_shared<AnalyticsCollector>();
  playerListeners.push_back(analyticsCollector);
  mediaSourceList->addEventListener(nullptr, analyticsCollector);

  msgThread = std::make_unique<EventThread>("PlayBackThread");
  msgThread->start();
  // Register a non-owning reference because a handler holds its callback weakly: the reference is kept
  // alive in `callbackRef` for as long as the handler may dispatch to it, and the no-op deleter stops
  // it from destroying this player, which is owned by its caller.
  callbackRef = std::shared_ptr<EventHandler::Callback>(this, [](EventHandler::Callback*) {
    MINILOG_DEBUG("The player's EventHandler is released");
  });
  msgHandler = EventHandler::create(msgThread->getLooper(), callbackRef);

  // The renderers hold a reference to this player as their video size listener. It is non-owning for
  // the same reason as `callbackRef`: the player belongs to its caller.
  videoSizeListenerRef = std::shared_ptr<VideoSizeListener>(this, [](VideoSizeListener*) {
  });

  // The events the application listens to are delivered on the thread it created the player on,
  // which is where ExoPlayer's listener handler lives too. A thread without a looper, e.g. a test
  // thread, receives them inline instead.
  mediaSourceFactory = std::make_shared<DefaultMediaSourceFactory>();
  if (auto const& looper = EventLooper::myLooper()) {
    appHandler = EventHandler::create(looper, nullptr);
  }
}

AvLivePlayer::~AvLivePlayer() {
  AvLivePlayer::release();
  // The renderers belong to the caller and outlive the player, and they call their listeners from
  // the threads they render on. A listener that points back at this object must not stay registered
  // on them, whatever state the release above left the player in.
  clearRendererListeners();
}

void AvLivePlayer::enableVideoEffectPipeline(const std::shared_ptr<VideoOutputNodeFactory>& factory) {
  VideoEffectInput input;
  const auto node = VideoFrameInputNode::create(2, 2, 8);
  input.inputNode = node;
  input.frameSink = std::make_shared<CpuVideoFrameSink>(node);
  enableVideoEffectPipeline(factory, input);
}

void AvLivePlayer::enableVideoEffectPipeline(const std::shared_ptr<VideoOutputNodeFactory>& factory,
    const VideoEffectInput& input) {
  if (videoEffectGraph != nullptr) {
    return;
  }

  // The aggregate is allowed to omit the pieces the player can supply: a default CPU input, or the
  // CPU sink over an application-created VideoFrameInputNode. A platform input node must bring its
  // own sink, because only the platform knows how its frames are submitted.
  VideoEffectInput resolved = input;
  if (resolved.inputNode == nullptr) {
    const auto node = VideoFrameInputNode::create(2, 2, 8);
    resolved.inputNode = node;
    if (resolved.frameSink == nullptr) {
      resolved.frameSink = std::make_shared<CpuVideoFrameSink>(node);
    }
  }
  if (resolved.frameSink == nullptr) {
    if (const auto cpuInput = std::dynamic_pointer_cast<VideoFrameInputNode>(resolved.inputNode);
        cpuInput != nullptr) {
      resolved.frameSink = std::make_shared<CpuVideoFrameSink>(cpuInput);
    } else {
      MINILOG_ERROR("the effect input has no VideoFrameSink for its platform input node");
      return;
    }
  }

  EffectEngineConfig config;
  config.renderMode = EffectRenderMode::Continuous;
  // Windows/Linux follow the platform priority (Vulkan first). Android LivePlayer stays on OpenGL:
  // its decoder input is the SurfaceTexture/OES path, and the Vulkan AHB path is currently removed.
#if defined(__ANDROID__)
  config.backend = EffectBackend::OpenGL;
#else
  config.backend = resolved.sharedContext != nullptr ? EffectBackend::OpenGL : EffectBackend::Default;
#endif
  config.renderWidth = 2;
  config.renderHeight = 2;
  config.timeBase = 1'000'000;
  config.maxFramesInFlight = 2;
  config.backpressureMode = EffectBackpressureMode::DropOldest;
  config.sharedContext = resolved.sharedContext;

  effectEngine = EffectEngine::create(config);
  if (effectEngine != nullptr) {
    effectEngine->setOnAfterFrame([this](backend::Device&) { onVideoFrameRendered(); });
  }
  const std::shared_ptr<EffectInputNode> graphInput = resolved.inputNode;
  const std::shared_ptr<NullOutputNode> nullOutput = NullOutputNode::create();
  if (effectEngine == nullptr || graphInput == nullptr || nullOutput == nullptr) {
    MINILOG_ERROR("could not create the effect engine or its input/output nodes");
    effectEngine.reset();
    return;
  }

  videoEffectGraph = VideoEffectGraph::create(effectEngine, graphInput, nullOutput);
  if (videoEffectGraph == nullptr) {
    MINILOG_ERROR("could not build the effect graph: {}", effectEngine->diagnostics());
    effectEngine.reset();
    return;
  }

  videoOutputFactory = factory;
  for (const auto& renderer : renderers) {
    if (auto video = std::dynamic_pointer_cast<DecodeVideoRenderer>(renderer); video != nullptr) {
      video->setVideoEffectGraph(videoEffectGraph);
      video->setVideoFrameSink(resolved.frameSink, resolved.decoderSurface);
    } else if (auto image = std::dynamic_pointer_cast<ImageRenderer>(renderer); image != nullptr) {
      image->setVideoEffectGraph(videoEffectGraph);
    }
  }

  {
    std::lock_guard<Mutex> lock(outputLock);
    const auto mode = subtitleRenderMode.load(std::memory_order_acquire);
    const bool burn = mode == SubtitleRenderMode::kBurnIn ||
                      (mode == SubtitleRenderMode::kAuto && textOutput == nullptr);
    videoEffectGraph->setSubtitleEnabled(burn);
    videoEffectGraph->setCueGroup(currentCueGroup);
  }

  // A surface may have been set before the pipeline was enabled. Re-post it so the playback thread
  // creates the platform output node now that the factory exists.
  std::shared_ptr<VideoSurface> pending;
  {
    std::lock_guard<Mutex> lock(outputLock);
    pending = pendingOutputSurface;
  }
  if (pending != nullptr) {
    msgHandler->sendMessage(
        Message::obtain(msgHandler, MSG_SET_SURFACE, WrapSharedMsgData(pending)));
  }
  MINILOG_INFO("the effect video pipeline is enabled");
}

bool AvLivePlayer::isSubtitleOverlayActive() const {
  return videoEffectGraph != nullptr && videoEffectGraph->subtitle() != nullptr &&
         videoEffectGraph->subtitle()->isEnabled();
}

bool AvLivePlayer::addLottieOverlay(const LottieOverlayConfig& config) {
  if (videoEffectGraph == nullptr) {
    MINILOG_WARN("addLottieOverlay: the effect pipeline is not enabled");
    return false;
  }
  return videoEffectGraph->addLottieOverlay(config) != nullptr;
}

void AvLivePlayer::clearLottieOverlays() {
  if (videoEffectGraph != nullptr) {
    videoEffectGraph->clearLottieOverlays();
  }
}

void AvLivePlayer::clearRendererListeners() {
  for (const auto& renderer: renderers) {
    if (renderer != nullptr) {
      renderer->setWakeupListener(nullptr);
      renderer->setVideoSizeListener(nullptr);
      if (auto text = std::dynamic_pointer_cast<TextRenderer>(renderer); text != nullptr) {
        // The cue listener captures this player. A renderer outlives the player, so a late cue must
        // not call back into a destroyed transport after release()/destruction.
        text->setCueListener(nullptr);
      }
      if (auto metadata = std::dynamic_pointer_cast<MetadataRenderer>(renderer); metadata != nullptr) {
        metadata->setMetadataListener(nullptr);
      }
    }
  }
}


void AvLivePlayer::dispatchCueGroup(const AVCueGroup& group) {
  if (!subtitlesEnabled.load(std::memory_order_acquire) && !group.isEmpty()) {
    return;
  }

  bool burn = false;
  {
    std::lock_guard<Mutex> lock(outputLock);
    currentCueGroup = group;
    const auto mode = subtitleRenderMode.load(std::memory_order_acquire);
    burn = mode == SubtitleRenderMode::kBurnIn ||
           (mode == SubtitleRenderMode::kAuto && textOutput == nullptr);
  }

  if (videoEffectGraph != nullptr) {
    videoEffectGraph->setSubtitleEnabled(burn);
    videoEffectGraph->setCueGroup(group);
  }

  {
    std::lock_guard<Mutex> lock(outputLock);
    if (textOutput != nullptr) {
      textOutput->onCues(group);
    }
  }
  const AVCueGroup copy = group;
  dispatchPlayerEvent([copy](EventListener& listener) { listener.onCues(copy); });
  flushPlayerEvents();
}

void AvLivePlayer::setSubtitleFile(const std::string& path, const std::string& mimeType) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    MINILOG_ERROR("setSubtitleFile: cannot open {}", path);
    return;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
    std::istreambuf_iterator<char>());
  if (bytes.empty()) {
    MINILOG_ERROR("setSubtitleFile: {} is empty", path);
    return;
  }

  std::string mime = mimeType;
  if (mime.empty()) {
    const auto dot = path.find_last_of('.');
    const std::string extension = dot == std::string::npos ? "" : path.substr(dot);
    if (extension == ".srt") mime = MimeTypes::APPLICATION_SUBRIP;
    else if (extension == ".ass" || extension == ".ssa") mime = MimeTypes::TEXT_SSA;
    else if (extension == ".vtt") mime = MimeTypes::TEXT_VTT;
  }
  if (mime.empty()) {
    MINILOG_ERROR("setSubtitleFile: no mime type for {}", path);
    return;
  }

  auto format = MediaFormat::CreateSubtitleFormat(mime);
  auto factory = createDefaultSubtitleParserFactory();
  if (format == nullptr || factory == nullptr || !factory->supportsFormat(*format)) {
    MINILOG_ERROR("setSubtitleFile: unsupported format {}", mime);
    return;
  }
  auto parser = factory->create(*format);
  if (parser == nullptr) {
    return;
  }
  auto timeline = std::make_shared<AVCueTimeline>();
  const auto behavior = format->getCueReplacementBehavior();
  const bool parsed = parser->parse(bytes.data(), bytes.size(),
    AVSubtitleParser::OutputOptions::allCues(),
    [&](const AVCuesWithTiming& cues) {
      timeline->add(cues, 0, Definitions::kTimeUnset, behavior,
        MediaFormat::OFFSET_SAMPLE_RELATIVE);
    });
  if (!parsed) {
    MINILOG_ERROR("setSubtitleFile: {} could not be parsed", path);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(externalMutex);
    externalTimeline = std::move(timeline);
    lastExternalGroup = AVCueGroup();
  }
}
void AvLivePlayer::setPreferredTextLanguage(const std::string& language,
    const bool selectUndetermined) {
  {
    std::lock_guard<Mutex> lock(outputLock);
    preferredTextLanguage = language;
    selectUndeterminedTextLanguage = selectUndetermined;
  }
  // The selector belongs to the playback thread; changing its parameters while that thread selects
  // tracks is a data race. Apply the latest values there instead.
  msgHandler->sendEmptyMessage(MSG_SET_PREFERRED_TEXT_LANGUAGE);
}

void AvLivePlayer::setSubtitleEnabled(const bool enabled) {
  subtitlesEnabled.store(enabled, std::memory_order_release);
  if (!enabled) {
    // Clear the cue that is on screen; the text track stays selected so enabling again needs no
    // track reselection, and the next cue group is delivered normally.
    dispatchCueGroup(AVCueGroup());
  } else if (videoEffectGraph != nullptr) {
    // The next cue group may be a while away; restore the last one immediately.
    bool burn = false;
    AVCueGroup group;
    {
      std::lock_guard<Mutex> lock(outputLock);
      const auto mode = subtitleRenderMode.load(std::memory_order_acquire);
      burn = mode == SubtitleRenderMode::kBurnIn ||
             (mode == SubtitleRenderMode::kAuto && textOutput == nullptr);
      group = currentCueGroup;
    }
    videoEffectGraph->setSubtitleEnabled(burn);
    videoEffectGraph->setCueGroup(group);
  }
}

bool AvLivePlayer::isSubtitleEnabled() const {
  return subtitlesEnabled.load(std::memory_order_acquire);
}

void AvLivePlayer::setTextOutput(const std::shared_ptr<TextOutput>& output) {
  {
    std::lock_guard<Mutex> lock(outputLock);
    textOutput = output;
  }
  if (videoEffectGraph != nullptr) {
    const auto mode = subtitleRenderMode.load(std::memory_order_acquire);
    const bool burn = mode == SubtitleRenderMode::kBurnIn ||
                      (mode == SubtitleRenderMode::kAuto && textOutput == nullptr);
    videoEffectGraph->setSubtitleEnabled(burn);
  }
}

std::shared_ptr<TextOutput> AvLivePlayer::getTextOutput() const {
  std::lock_guard<Mutex> lock(outputLock);
  return textOutput;
}

void AvLivePlayer::setMetadataOutput(const std::shared_ptr<MetadataOutput>& output) {
  std::lock_guard<Mutex> lock(outputLock);
  metadataOutput = output;
}

std::shared_ptr<MetadataOutput> AvLivePlayer::getMetadataOutput() const {
  std::lock_guard<Mutex> lock(outputLock);
  return metadataOutput;
}

void AvLivePlayer::setSubtitleRenderMode(const SubtitleRenderMode mode) {
  subtitleRenderMode.store(mode, std::memory_order_release);
  if (videoEffectGraph != nullptr) {
    const bool burn = mode == SubtitleRenderMode::kBurnIn ||
                      (mode == SubtitleRenderMode::kAuto && textOutput == nullptr);
    videoEffectGraph->setSubtitleEnabled(burn);
  }
}
void AvLivePlayer::setOutputSurface(const std::shared_ptr<VideoSurface>& surface) {
  {
    std::lock_guard<Mutex> lock(outputLock);
    pendingOutputSurface = surface;
  }
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_SET_SURFACE, WrapSharedMsgData(surface)));
}

void AvLivePlayer::setVideoScalingMode(int mode) {
  if (mode == 2) {
    videoScaleMode.store(VideoScaleMode::Fill, std::memory_order_relaxed);
  } else if (mode == 3) {
    videoScaleMode.store(VideoScaleMode::Stretch, std::memory_order_relaxed);
  } else {
    videoScaleMode.store(VideoScaleMode::Fit, std::memory_order_relaxed);
  }
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_SET_VIDEO_SCALE_MODE, WrapMessageData(mode)));
}

void AvLivePlayer::setVideoScaleMode(const VideoScaleMode mode) {
  videoScaleMode.store(mode, std::memory_order_relaxed);
  const int platformMode = mode == VideoScaleMode::Fill ? 2 : (mode == VideoScaleMode::Stretch ? 3 : 1);
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_SET_VIDEO_SCALE_MODE,
    WrapMessageData(platformMode)));
}

void AvLivePlayer::setVideoMirror(const bool horizontal, const bool vertical) {
  const int flags = (horizontal ? 1 : 0) | (vertical ? 2 : 0);
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_SET_VIDEO_MIRROR, flags, 0));
}

void AvLivePlayer::setVideoMirrorInternal(const int flags) {
  if (videoEffectGraph == nullptr) {
    return;
  }
  if (videoEffectGraph->setVideoMirror((flags & 1) != 0, (flags & 2) != 0) != EffectResult::Success) {
    MINILOG_WARN("could not set the effect mirror mode");
  }
}

void AvLivePlayer::setMediaItem(const std::shared_ptr<MediaItem>& mediaItem) {
  // The source is created by the factory, which is what lets an application choose how the media is
  // read without the player having to know about the kinds of media it supports.
  const auto& factory = mediaSourceFactory != nullptr
                          ? mediaSourceFactory
                          : std::make_shared<DefaultMediaSourceFactory>();
  setMediaSource(factory->createMediaSource(mediaItem));
}

void AvLivePlayer::setRepeatMode(const RepeatMode mode) {
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_SET_REPEAT_MODE, WrapMessageData(mode)));
}

void AvLivePlayer::setMediaSourceFactory(const std::shared_ptr<MediaSourceFactory>& factory) {
  if (factory != nullptr) {
    mediaSourceFactory = factory;
  }
}

void AvLivePlayer::addListener(const std::shared_ptr<EventListener>& listener) {
  if (listener == nullptr) {
    return;
  }
  Lock lock(playerListenersLock);
  if (std::find(playerListeners.begin(), playerListeners.end(), listener) == playerListeners.end()) {
    playerListeners.push_back(listener);
  }
}

void AvLivePlayer::removeListener(const std::shared_ptr<EventListener>& listener) {
  Lock lock(playerListenersLock);
  if (const auto position = std::ranges::find(playerListeners, listener); position != playerListeners.end()) {
    playerListeners.erase(position);
  }
}

void AvLivePlayer::addMediaSourceEventListener(const std::shared_ptr<MediaSourceEventListener>& listener) {
  if (listener == nullptr) {
    return;
  }
  // The listener has to be registered on the media sources, which only the playback thread may
  // touch, so the registration is posted like the other commands.
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_ADD_MEDIA_SOURCE_EVENT_LISTENER,
    WrapSharedMsgData(listener)));
}

void AvLivePlayer::removeMediaSourceEventListener(
  const std::shared_ptr<MediaSourceEventListener>& listener) {
  if (listener == nullptr) {
    return;
  }
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_REMOVE_MEDIA_SOURCE_EVENT_LISTENER,
    WrapSharedMsgData(listener)));
}

void AvLivePlayer::handleAddMediaSourceEventListener(const std::shared_ptr<MediaSourceEventListener>& listener) {
  if (std::ranges::find(mediaSourceEventListeners, listener) != mediaSourceEventListeners.end()) {
    return;
  }
  mediaSourceEventListeners.push_back(listener);
  if (mediaSourceList != nullptr) {
    mediaSourceList->addEventListener(appHandler, listener);
  }
}

void AvLivePlayer::handleRemoveMediaSourceEventListener(const std::shared_ptr<MediaSourceEventListener>& listener) {
  const auto position = std::ranges::find(mediaSourceEventListeners, listener);
  if (position != mediaSourceEventListeners.end()) {
    mediaSourceEventListeners.erase(position);
  }
  if (mediaSourceList != nullptr) {
    mediaSourceList->removeEventListener(listener);
  }
}

void AvLivePlayer::addAnalyticsListener(const std::shared_ptr<AnalyticsListener>& listener) {
  if (analyticsCollector != nullptr) {
    analyticsCollector->addListener(listener);
  }
}

void AvLivePlayer::removeAnalyticsListener(const std::shared_ptr<AnalyticsListener>& listener) {
  if (analyticsCollector != nullptr) {
    analyticsCollector->removeListener(listener);
  }
}

// ==================================================================================================
// The playlist
// ==================================================================================================

int AvLivePlayer::getMediaItemCount() const {
  return playbackInfo.timeline != nullptr ? playbackInfo.timeline->getWindowCount() : 0;
}

const std::shared_ptr<MediaItem>& AvLivePlayer::getMediaItemAt(const int index) const {
  static const std::shared_ptr<MediaItem> sEmptyItem = nullptr;
  if (playbackInfo.timeline == nullptr ||
      index < 0 ||
      index >= playbackInfo.timeline->getWindowCount()) {
    return sEmptyItem;
  }
  // The media item of a window is the one its source was created with, so the playlist does not have
  // to be kept in a second place.
  const auto timelineWindow =
    playbackInfo.timeline->getWindow(index, std::make_shared<Timeline::Window>(), 0);
  return timelineWindow != nullptr ? timelineWindow->mediaItem : sEmptyItem;
}

void AvLivePlayer::addMediaItems(int index,
  const std::vector<std::shared_ptr<MediaItem>>& mediaItems) {
  if (mediaItems.empty()) {
    return;
  }
  MediaItemChange change;
  change.fromIndex = index;
  change.mediaItems = mediaItems;
  msgHandler->sendMessage(
    Message::obtain(msgHandler, MSG_ADD_MEDIA_ITEMS, WrapMessageData(change)));
}

void AvLivePlayer::removeMediaItems(int fromIndex, int toIndex) {
  if (fromIndex >= toIndex) {
    return;
  }
  MediaItemChange change;
  change.fromIndex = fromIndex;
  change.toIndex = toIndex;
  msgHandler->sendMessage(
    Message::obtain(msgHandler, MSG_REMOVE_MEDIA_ITEMS, WrapMessageData(change)));
}

void AvLivePlayer::moveMediaItems(int fromIndex, int toIndex, int newIndex) {
  if (fromIndex >= toIndex) {
    return;
  }
  MediaItemChange change;
  change.fromIndex = fromIndex;
  change.toIndex = toIndex;
  change.newIndex = newIndex;
  msgHandler->sendMessage(
    Message::obtain(msgHandler, MSG_MOVE_MEDIA_ITEMS, WrapMessageData(change)));
}

void AvLivePlayer::clearMediaItems() {
  msgHandler->sendEmptyMessage(MSG_CLEAR_MEDIA_ITEMS);
}

void AvLivePlayer::addMediaItemsInternal(const MediaItemChange& change) {
  if (change.mediaItems.empty()) {
    return;
  }
  auto holders = std::vector<std::shared_ptr<MediaSourceHolder>>();
  holders.reserve(change.mediaItems.size());
  for (const auto& item: change.mediaItems) {
    auto const& factory = mediaSourceFactory != nullptr
                            ? mediaSourceFactory
                            : std::make_shared<DefaultMediaSourceFactory>();
    holders.push_back(std::make_shared<MediaSourceHolder>(factory->createMediaSource(item), true));
  }
  const auto index = std::clamp(change.fromIndex, 0, mediaSourceList->getSize());
  shuffleOrder = shuffleOrder->cloneAndInsert(index, static_cast<int>(holders.size()));
  auto timeline = mediaSourceList->addMediaSources(index, holders, shuffleOrder);
  handleMediaSourceListInfoRefreshed(timeline);
}

void AvLivePlayer::removeMediaItemsInternal(const MediaItemChange& change) {
  const auto size = mediaSourceList->getSize();
  const auto fromIndex = std::clamp(change.fromIndex, 0, size);
  const auto toIndex = std::clamp(change.toIndex, fromIndex, size);
  if (fromIndex == toIndex) {
    return;
  }
  shuffleOrder = shuffleOrder->cloneAndRemove(fromIndex, toIndex);
  auto timeline = mediaSourceList->removeMediaSources(fromIndex, toIndex, shuffleOrder);
  handleMediaSourceListInfoRefreshed(timeline);
}

void AvLivePlayer::moveMediaItemsInternal(const MediaItemChange& change) {
  const auto size = mediaSourceList->getSize();
  const auto fromIndex = std::clamp(change.fromIndex, 0, size);
  const auto toIndex = std::clamp(change.toIndex, fromIndex, size);
  const auto newIndex = std::clamp(change.newIndex, 0, size);
  if (fromIndex == toIndex || fromIndex == newIndex || toIndex == newIndex) {
    // Nothing would move.
    return;
  }
  // Moving an item does not change the shuffle order of the child sources, so it is passed as it is.
  auto timeline =
    mediaSourceList->moveMediaSources(fromIndex, toIndex, newIndex, shuffleOrder);
  handleMediaSourceListInfoRefreshed(timeline);
}

void AvLivePlayer::clearMediaItemsInternal() {
  shuffleOrder = shuffleOrder->cloneAndClear();
  auto timeline = mediaSourceList->clear(shuffleOrder);
  handleMediaSourceListInfoRefreshed(timeline);
}

void AvLivePlayer::setRepeatModeInternal(const RepeatMode& mode) {
  if (repeatMode == mode) {
    return;
  }
  repeatMode = mode;
  dispatchPlayerEvent([mode](EventListener& listener) { listener.onRepeatModeChanged(mode); });
}

void AvLivePlayer::prepare() {
  msgHandler->removeMessages(MSG_PREPARE);
  msgHandler->sendEmptyMessage(MSG_PREPARE);
}

void AvLivePlayer::setPlayWhenReady(bool enable) {
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_SET_PLAY_WHEN_READY, enable ? 1 : 0, 0));
}

void AvLivePlayer::play() {
  // The playback state is read on the playback thread, so the whole call is a message: restarting an
  // ended player has to seek to the default position and start the playback in the same iteration.
  msgHandler->sendEmptyMessage(MSG_PLAY);
}

void AvLivePlayer::playInternal() {
  if (playbackInfo.playbackState == State::STATE_IDLE) {
    msgHandler->removeMessages(MSG_PREPARE);
    prepareInternal();
  } else if (playbackInfo.playbackState == State::STATE_ENDED) {
    // ExoPlayer's `play()` only sets playWhenReady, so an ended player that is played again stays
    // where it is and plays nothing; the application is expected to call `seekToDefaultPosition()`
    // first. Doing it here gives the effect an application asks for with `play()`, by using the
    // ExoPlayer primitive that means "go back to where the media starts".
    seekToDefaultPositionInternal();
  }
  setPlayWhenReadyInternal(true);
}

void AvLivePlayer::pause() {
  setPlayWhenReady(false);
}

void AvLivePlayer::stop() {
  msgHandler->removeMessages(MSG_DO_SOME_WORK);
  msgHandler->sendEmptyMessage(MSG_STOP);
}

void AvLivePlayer::release() {

  if (released || !msgThread || !msgThread->isAlive()) {
    return;
  }
  threadBarrier.reset(1);
  if (!msgHandler->sendEmptyMessage(MSG_RELEASE)) {
    // The release message could not be queued, so the latch would never be tripped and awaiting it
    // would hang the caller. The playback thread is either gone or on its way out, so the player is
    // released inline instead.
    releaseInternal();
    // The release changed the state of the player but the message that would normally deliver it was
    // never handled, so the listeners are told here.
    flushPlayerEvents();
    quitThread();
    return;
  }
  threadBarrier.await();
  quitThread();
}

void AvLivePlayer::seekTo(int64_t timeUs) {
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_SEEK_TO_POSITION, WrapMessageData(timeUs)));
}

void AvLivePlayer::seekToDefaultPosition() {
  msgHandler->sendEmptyMessage(MSG_SEEK_TO_DEFAULT_POSITION);
}

void AvLivePlayer::seekToDefaultPositionInternal() {
  // `kTimeUnset` is what tells the seek resolution to use the default position of the window, which is
  // the start of a normal media and the live edge of a live one, like ExoPlayer's
  // `seekToDefaultPosition`.
  seekToInternal(Definitions::kTimeUnset);
}

void AvLivePlayer::seekToInternal(const int64_t timeUs) {
  // The player holds a single-item playlist, so a seek always targets the first window.
  pendingInitialSeekPosition = SeekPosition(playbackInfo.timeline, 0, timeUs);
  if (playbackInfo.timeline->isEmpty()) {
    // Nothing to resolve yet: the pending seek is resolved as soon as the timeline is available.
    return;
  }
  // Re-resolving the playlist change with the pending seek position applies the seek: it resolves
  // the position in the timeline, seeks the media period and resets the renderer positions.
  handleMediaSourceListInfoRefreshed(mediaSourceList->createTimeline());
}

void AvLivePlayer::setPlaybackParameters(const Parameters& parameters) {
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_PLAYBACK_PARAMETERS, WrapMessageData(parameters)));
}

void AvLivePlayer::setVolume(const Volume& volume) {
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_UPDATE_VOLUME, WrapMessageData(volume)));
}

void AvLivePlayer::onPrepared(const std::shared_ptr<MediaPeriod>& mediaPeriod) {
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_PERIOD_PREPARED, WrapSharedMsgData(mediaPeriod)));
}

void AvLivePlayer::onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& mediaPeriod) {
  msgHandler->sendMessage(Message::obtain(msgHandler,
    MSG_SOURCE_CONTINUE_LOADING_REQUESTED,
    WrapSharedMsgData(mediaPeriod)));
}

bool AvLivePlayer::maybeThrowPeriodError(const std::shared_ptr<MediaPeriodHolder>& holder) {
  if (holder == nullptr) {
    return false;
  }
  // The period is owned for the duration of the call: handling the error stops the playback, which
  // releases the periods of the queue, so the period must not be reachable only through its holder.
  const auto mediaPeriod = holder->getMediaPeriod();
  if (mediaPeriod == nullptr) {
    return false;
  }
  try {
    // The reason is read as a value first: a throw cannot be relied on to cross every frame the
    // platform compiles (an Android build without unwinding tables aborts the process instead of
    // reaching this handler), and the player only needs the reason of the failure, not the exception.
    // The throwing form is kept for a period that only implements it.
    const auto prepareError = mediaPeriod->getPrepareError();
    if (!prepareError.empty()) {
      handleSourceError(prepareError);
      return true;
    }
    // The period reports its fatal error by throwing, like ExoPlayer's media periods, and the player
    // turns it into a playback error: this is where the player learns that the media cannot be played.
    mediaPeriod->maybeThrowPrepareError();
  } catch (const std::exception& e) {
    handleSourceError(e.what());
    return true;
  }
  return false;
}

void AvLivePlayer::onTrackSelectionsInvalidated() {
  msgHandler->sendEmptyMessage(MSG_TRACK_SELECTION_INVALIDATED);
}

void AvLivePlayer::onVideoSizeChanged(const int width, const int height) {
  // The size is reported by the renderer thread, so it is posted like the other renderer events and
  // handled on the playback thread.
  if (msgHandler != nullptr) {
    msgHandler->sendMessage(Message::obtain(msgHandler, MSG_VIDEO_SIZE_CHANGED,
      width, height));
  }
}

void AvLivePlayer::setVideoSizeInternal(const int width, const int height) {
  if (mVideoWidth == width && mVideoHeight == height) {
    return;
  }
  mVideoWidth = width;
  mVideoHeight = height;
  dispatchPlayerEvent([width, height](EventListener& listener) {
    listener.onVideoSizeChanged(width, height);
  });
}

void AvLivePlayer::onPlaybackParametersChanged(const Parameters& newParameters) {
  sendParametersChangedInternal(newParameters, false);
}

void AvLivePlayer::onPlaylistUpdateRequested() {
  msgHandler->removeMessages(MSG_DO_SOME_WORK);
  msgHandler->sendEmptyMessage(MSG_PLAYLIST_UPDATE_REQUESTED);
}

std::shared_ptr<MediaPeriodHolder>
AvLivePlayer::createMediaPeriodHolder(const MediaPeriodInfo& info, const int64_t positionOffsetUs) {
  return std::make_shared<MediaPeriodHolder>(rendererCapabilities,
    positionOffsetUs,
    trackSelector,
    loadControl->getAllocator(),
    mediaSourceList,
    info,
    emptyTrackSelectorResult);
}

bool AvLivePlayer::handleMessage(Message* msg) {
  switch (msg->what) {
    case MSG_PREPARE: {
      prepareInternal();
      break;
    }
    case MSG_SET_PLAY_WHEN_READY: {
      setPlayWhenReadyInternal(msg->arg1 != 0);
      break;
    }
    case MSG_DO_SOME_WORK: {
      doSomeWork();
      break;
    }
    case MSG_STOP: {
      stopInternal(/* forceResetRenderers= */ false, /* acknowledgeStop= */ true);
      break;
    }
    case MSG_RELEASE: {
      releaseInternal();
      // Return immediately to not send playback info updates after release.
      return true;
    }
    case MSG_PLAYBACK_PARAMETERS: {
      setParametersInternal(UseMessageData<Parameters>(msg->msgData));
      break;
    }
    case MSG_UPDATE_VOLUME: {
      updateVolumeInternal(msg);
      break;
    }
    case MSG_SEEK_TO_POSITION: {
      seekToInternal(UseMessageData<int64_t>(msg->msgData));
      break;
    }
    case MSG_SEEK_TO_DEFAULT_POSITION: {
      seekToDefaultPositionInternal();
      break;
    }
    case MSG_PLAY: {
      playInternal();
      break;
    }
    case MSG_SET_SURFACE: {
      setSurfaceInternal(msg);
      break;
    }
    case MSG_SET_VIDEO_SCALE_MODE: {
      setVideoScalingModeInternal(msg);
      break;
    }
    case MSG_SET_MEDIA_SOURCE: {
      setMediaItemInternal(UseSharedMsgData<MediaSource>(msg->msgData));
      break;
    }
    case MSG_PERIOD_PREPARED: {
      handlePeriodPrepared(UseSharedMsgData<MediaPeriod>(msg->msgData));
      break;
    }
    case MSG_SOURCE_CONTINUE_LOADING_REQUESTED: {
      handleContinueLoadingRequested(UseSharedMsgData<MediaPeriod>(msg->msgData));
      break;
    }
    case MSG_TRACK_SELECTION_INVALIDATED: {
      reselectTracksInternal();
      break;
    }
    case MSG_SET_VIDEO_MIRROR: {
      setVideoMirrorInternal(msg->arg1);
      break;
    }
    case MSG_SELECT_TRACK: {
      selectTrackInternal(UseMessageData<TrackSelectionRequest>(msg->msgData));
      break;
    }
    case MSG_CLEAR_TRACK_SELECTION: {
      clearTrackSelectionInternal(UseMessageData<int>(msg->msgData));
      break;
    }
    case MSG_SET_PREFERRED_TEXT_LANGUAGE: {
      std::string language;
      bool selectUndetermined = true;
      {
        std::lock_guard<Mutex> lock(outputLock);
        language = preferredTextLanguage;
        selectUndetermined = selectUndeterminedTextLanguage;
      }
      if (auto selector = std::dynamic_pointer_cast<DefaultTrackSelector>(trackSelector);
          selector != nullptr) {
        auto parameters = selector->getParameters();
        parameters.preferredTextLanguage = language;
        parameters.selectUndeterminedTextLanguage = selectUndetermined;
        selector->setParameters(parameters);
      }
      // Queue the reselection after any seek that is already queued: the seek must apply the new
      // language while the old text period is still valid, and the invalidation then rebuilds streams.
      if (msgHandler != nullptr) {
        msgHandler->sendEmptyMessage(MSG_TRACK_SELECTION_INVALIDATED);
      }
      break;
    }
    case MSG_PLAYLIST_UPDATE_REQUESTED: {
      mediaSourceListUpdateRequestedInternal();
      break;
    }
    case MSG_ADD_MEDIA_SOURCE_EVENT_LISTENER: {
      handleAddMediaSourceEventListener(UseSharedMsgData<MediaSourceEventListener>(msg->msgData));
      break;
    }
    case MSG_REMOVE_MEDIA_SOURCE_EVENT_LISTENER: {
      handleRemoveMediaSourceEventListener(
        UseSharedMsgData<MediaSourceEventListener>(msg->msgData));
      break;
    }
    case MSG_SET_REPEAT_MODE: {
      setRepeatModeInternal(UseMessageData<RepeatMode>(msg->msgData));
      break;
    }
    case MSG_VIDEO_SIZE_CHANGED: {
      setVideoSizeInternal(msg->arg1, msg->arg2);
      break;
    }
    case MSG_ADD_MEDIA_ITEMS: {
      addMediaItemsInternal(UseMessageData<MediaItemChange>(msg->msgData));
      break;
    }
    case MSG_REMOVE_MEDIA_ITEMS: {
      removeMediaItemsInternal(UseMessageData<MediaItemChange>(msg->msgData));
      break;
    }
    case MSG_MOVE_MEDIA_ITEMS: {
      moveMediaItemsInternal(UseMessageData<MediaItemChange>(msg->msgData));
      break;
    }
    case MSG_CLEAR_MEDIA_ITEMS: {
      clearMediaItemsInternal();
      break;
    }
    default: {
      return false;
    }
  }
  // Everything the message changed has happened, so the listeners are told once, in the order the
  // changes happened. This is the one notification per iteration that ExoPlayer's `PlaybackInfoUpdate`
  // produces, and it is what makes a listener that reads the player from a callback see the state this
  // message left behind instead of an intermediate one.
  flushPlayerEvents();
  return true;
}

//------------------------------------------------------------------------------

void AvLivePlayer::quitThread() {
  if (msgHandler) {
    msgHandler->removeAllCallbacksAndMessages();
  }
  if (msgThread) {
    msgThread->quit();
  }
  msgThread.reset();
  msgHandler.reset();
}

void AvLivePlayer::setMediaSource(const std::shared_ptr<MediaSource>& mediaSource) {
  msgHandler->sendMessage(
    Message::obtain(msgHandler, MSG_SET_MEDIA_SOURCE, WrapSharedMsgData(mediaSource)));
}

void AvLivePlayer::setState(const State newState) {
  if (playbackInfo.playbackState != newState) {
    if (newState != State::STATE_BUFFERING) {
      //      playbackMaybeBecameStuckAtMs = Definitions::kTimeUnset;
    }
    playbackInfo.playbackState = newState;
    dispatchPlayerEvent(
      [newState](EventListener& listener) { listener.onPlaybackStateChanged(newState); });
  }
}

void AvLivePlayer::dispatchPlayerEvent(const std::function<void(EventListener&)>& event) {
  // The events of one iteration of the playback thread are collected rather than delivered one by one,
  // so that the listeners are told about the state the player ends the iteration in: see
  // `flushPlayerEvents`. This is ExoPlayer's `PlaybackInfoUpdate`, whose one notification per
  // iteration coalesces the changes of that iteration.
  pendingPlayerEvents.push_back(event);
}

void AvLivePlayer::flushPlayerEvents() {
  if (pendingPlayerEvents.empty()) {
    return;
  }
  auto events = std::move(pendingPlayerEvents);
  pendingPlayerEvents.clear();

  // The listeners are copied out of the list before they are called: a listener that removes itself
  // while it handles an event must not invalidate the iteration, and the dispatch must not run with
  // the lock held.
  std::vector<std::shared_ptr<EventListener>> listeners;
  {
    Lock lock(playerListenersLock);
    listeners = playerListeners;
  }
  if (listeners.empty()) {
    return;
  }
  const auto dispatch = [events, listeners] {
    for (const auto& event: events) {
      for (const auto& listener: listeners) {
        if (listener != nullptr) {
          event(*listener);
        }
      }
    }
  };
  if (appHandler != nullptr) {
    appHandler->post(dispatch);
    return;
  }
  dispatch();
}

void AvLivePlayer::maybeNotifyPlaybackInfoChanged() {
  //  playbackInfoUpdate.setPlaybackInfo(playbackInfo);
  //  if (playbackInfoUpdate.hasPendingChange) {
  //    playbackInfoUpdateListener.onPlaybackInfoUpdate(playbackInfoUpdate);
  //    playbackInfoUpdate = new PlaybackInfoUpdate(playbackInfo);
  //  }
}

void AvLivePlayer::prepareInternal() {
  if (playbackInfo.playbackState != State::STATE_IDLE) {
    return;
  }
  //  playbackInfoUpdate.incrementPendingOperationAcks(/* operationAcks= */ 1);
  resetInternal(
    /* resetRenderers= */ false,
    /* resetPosition= */ false,
    /* releaseMediaSourceList= */ false,
    /* resetError= */ true);
  if (videoEffectGraph != nullptr) {
    const EffectResult result = videoEffectGraph->start();
    if (result != EffectResult::Success) {
      MINILOG_ERROR("the effect video pipeline could not start: {}",
        effectEngine != nullptr ? effectEngine->diagnostics() : std::string("no engine"));
    }
  }
  loadControl->onPrepared();
  setState(playbackInfo.timeline->isEmpty() ? State::STATE_ENDED : State::STATE_BUFFERING);
  mediaSourceList->prepare();
  msgHandler->sendEmptyMessage(MSG_DO_SOME_WORK);
}

void AvLivePlayer::mediaSourceListUpdateRequestedInternal() {
  handleMediaSourceListInfoRefreshed(mediaSourceList->createTimeline());
}

void AvLivePlayer::setPlayWhenReadyInternal(bool enable) {
  if (playbackInfo.playWhenReady != enable) {
    playbackInfo.playWhenReady = enable;
    dispatchPlayerEvent(
      [enable](EventListener& listener) { listener.onPlayWhenReadyChanged(enable); });
  }
  isRebuffering = false;
  if (!shouldPlayWhenReady()) {
    stopRenderers();
    updatePlaybackPositions();
  } else {
    if (playbackInfo.playbackState == State::STATE_READY) {
      startRenderers();
      msgHandler->sendEmptyMessage(MSG_DO_SOME_WORK);
    } else if (playbackInfo.playbackState == State::STATE_BUFFERING) {
      msgHandler->sendEmptyMessage(MSG_DO_SOME_WORK);
    }
  }
}

void AvLivePlayer::seekToCurrentPosition(bool sendDiscontinuity) {
  auto periodId = queue->getPlayingPeriod()->info.id;
  auto newPositionUs =
    seekToPeriodPosition(periodId, playbackInfo.positionUs, true, false);
  if (newPositionUs != playbackInfo.positionUs) {
    playbackInfo = handlePositionDiscontinuity(
      periodId, newPositionUs, playbackInfo.requestedContentPositionUs);
    if (sendDiscontinuity) {
      //      playbackInfoUpdate.setPositionDiscontinuity(Player.DISCONTINUITY_REASON_INTERNAL);
    }
  }
}

void AvLivePlayer::updatePlaybackPositions() {
  auto playingPeriodHolder = queue->getPlayingPeriod();
  if (playingPeriodHolder == nullptr) {
    return;
  }

  // Update the playback position.
  auto discontinuityPositionUs = playingPeriodHolder->isPrepared()
                                   ? playingPeriodHolder->getMediaPeriod()->readDiscontinuity()
                                   : Definitions::kTimeUnset;
  if (discontinuityPositionUs != Definitions::kTimeUnset) {
    if (!playingPeriodHolder->isFullyBuffered()) {
      // The discontinuity caused the period to not be fully buffered. Continue loading from this
      // period again and discard all other periods we already started loading.
      queue->removeAfter(playingPeriodHolder);
      handleLoadingMediaPeriodChanged(/* loadingTrackSelectionChanged= */ false);
      maybeContinueLoading();
    }
    resetRendererPosition(discontinuityPositionUs);
    // A MediaPeriod may report a discontinuity at the current playback position to ensure the
    // renderers are flushed. Only report the discontinuity externally if the position changed.
    if (discontinuityPositionUs != playbackInfo.positionUs) {
      playbackInfo =
        handlePositionDiscontinuity(
          playbackInfo.periodId,
          discontinuityPositionUs,
          playbackInfo.requestedContentPositionUs);
      //      playbackInfoUpdate.setPositionDiscontinuity(Player.DISCONTINUITY_REASON_INTERNAL);
    }
  } else {
    rendererPositionUs =
      mediaClock->syncAndGetPositionUs(playingPeriodHolder != queue->getReadingPeriod());
    auto periodPositionUs = playingPeriodHolder->toPeriodTime(rendererPositionUs);
    //    maybeTriggerPendingMessages(playbackInfo.positionUs, periodPositionUs);
    playbackInfo.positionUs = periodPositionUs;
  }

  // Update the buffered position and total buffered duration.
  auto const& loadingPeriod = queue->getLoadingPeriod();
  playbackInfo.bufferedPositionUs = loadingPeriod->getBufferedPositionUs();
  playbackInfo.totalBufferedDurationUs = getTotalBufferedDurationUs();
}

void AvLivePlayer::doSomeWork() {
  const auto operationStartTime = std::chrono::steady_clock::now();
  // Remove other pending DO_SOME_WORK requests that are handled by this invocation.
  //  msgHandler->removeMessages(MSG_DO_SOME_WORK);

  updatePeriods();

  std::shared_ptr<AVCueTimeline> external;
  {
    std::lock_guard<std::mutex> lock(externalMutex);
    external = externalTimeline;
  }
  if (external != nullptr) {
    const AVCueGroup group = external->cuesAt(getCurrentPositionUs());
    if (group != lastExternalGroup) {
      lastExternalGroup = group;
      dispatchCueGroup(group);
    }
  }

  if (playbackInfo.playbackState == State::STATE_IDLE ||
      playbackInfo.playbackState == State::STATE_ENDED) {
    // Nothing to do. Prepare (in case of IDLE) or seek (in case of ENDED) will resume.
    return;
  }

  const auto playingPeriodHolder = queue->getPlayingPeriod();
  if (!playingPeriodHolder) {
    // We're still waiting until the playing item is available.
    scheduleNextWork(operationStartTime);
    return;
  }

  TraceUtil::beginSection("doSomeWork");

  // A period that cannot be prepared reports it by throwing, so the player checks before it does any
  // work with the periods: the fatal error stops the playback and reaches the listeners as a player
  // error, which is what ExoPlayer's ExoPlayerImplInternal does with the IOException of a media period.
  if (maybeThrowPeriodError(queue->getLoadingPeriod()) ||
      maybeThrowPeriodError(queue->getPlayingPeriod())) {
    TraceUtil::endSection();
    return;
  }

  // update playback position
  updatePlaybackPositions();

  auto renderersEnded = true;
  auto renderersAllowPlayback = true;
  if (playingPeriodHolder->isPrepared()) {
    const auto rendererPositionElapsedRealtimeUs = SystemClock::ElapsedRealtime() * 1000;
    playingPeriodHolder->getMediaPeriod()
                       ->discardBuffer(playbackInfo.positionUs - backBufferDurationUs,
                         retainBackBufferFromKeyframe);

    // rendering operation
    for (size_t i = 0; i < renderers.size(); ++i) {
      auto const& renderer = renderers[i];
      if (!isRendererEnabled(renderer)) {
        continue;
      }
      // TODO: Each renderer should return the maximum delay before which it
      // wishes to be called again. The minimum of these values should then be used as
      // the delay before the next invocation of this method.
      renderer->render(rendererPositionUs, rendererPositionElapsedRealtimeUs);
      renderersEnded = renderersEnded && renderer->isEnded();
      // Determine whether the renderer allows playback to continue. Playback
      // can continue if the renderer is ready or ended. Also continue playback
      // if the renderer is reading ahead into the next stream or is waiting for
      // the next stream. This is to avoid getting stuck if tracks in the
      // current item have uneven durations and are still being read by another
      // renderer.
      const auto isReadingAhead = playingPeriodHolder->getSampleStream(i) != renderer->getStream();
      const auto isWaitingForNextStream =
        !isReadingAhead &&
        playingPeriodHolder->getNext() != nullptr &&
        renderer->hasReadStreamToEnd();
      const auto allowsPlayback =
        isReadingAhead ||
        isWaitingForNextStream ||
        renderer->isReady() ||
        renderer->isEnded();
      renderersAllowPlayback = renderersAllowPlayback && allowsPlayback;
      if (!allowsPlayback) {
        renderer->maybeThrowStreamError();
      }
    }
  }

  const auto playingPeriodDurationUs = playingPeriodHolder->getDurationUs();
  const auto finishedRendering =
    renderersEnded &&
    playingPeriodHolder->isPrepared() &&
    (playingPeriodDurationUs == Definitions::kTimeUnset || playingPeriodDurationUs <= playbackInfo.positionUs);
  if (finishedRendering && pendingPauseAtEndOfPeriod) {
    pendingPauseAtEndOfPeriod = false;
    setPlayWhenReadyInternal(false);
  }
  if (finishedRendering && playingPeriodHolder->info.isFinal) {
    setState(State::STATE_ENDED);
    stopRenderers();
  } else if (playbackInfo.playbackState == State::STATE_BUFFERING &&
             shouldTransitionToReadyState(renderersAllowPlayback)) {
    setState(State::STATE_READY);
    if (shouldPlayWhenReady()) {
      updateRebufferingState(
        /* isRebuffering= */ false, /* resetLastRebufferRealtimeMs= */ false);
      startRenderers();
    }
  } else if (playbackInfo.playbackState == State::STATE_READY &&
             !(enabledRendererCount == 0 ? isTimelineReady() : renderersAllowPlayback)) {
    updateRebufferingState(/* rebuffering= */ shouldPlayWhenReady(), /* resetLastRebufferRealtimeMs= */ false);
    setState(State::STATE_BUFFERING);
    if (isRebuffering) {
      //      notifyTrackSelectionRebuffer();
      //      livePlaybackSpeedControl.notifyRebuffer();
    }
    stopRenderers();
  }

  auto playbackMaybeStuck = false;
  if (playbackInfo.playbackState == State::STATE_BUFFERING) {
    for (size_t i = 0; i < renderers.size(); ++i) {
      if (isRendererEnabled(renderers[i]) &&
          renderers[i]->getStream() == playingPeriodHolder->getSampleStream(i)) {
        renderers[i]->maybeThrowStreamError();
      }
    }
    if (!playbackInfo.isLoading
        && playbackInfo.totalBufferedDurationUs < PLAYBACK_BUFFER_EMPTY_THRESHOLD_US
        && isLoadingPossible(playingPeriodHolder)
        && shouldPlayWhenReady()) {
      // The renderers are not ready, there is more media available to load, and the LoadControl
      // is refusing to load it (indicated by !playbackInfo.isLoading). This could be because the
      // renderers are still transitioning to their ready states, but it could also indicate a
      // stuck playback. The playbackInfo.totalBufferedDurationUs check further isolates the
      // cause to a lack of media for the renderers to consume, to avoid classifying playbacks as
      // stuck when they're waiting for other reasons (in particular, loading DRM keys).
      playbackMaybeStuck = true;
    }
  }
  if (offloadSchedulingEnabled != playbackInfo.offloadSchedulingEnabled) {
    playbackInfo.offloadSchedulingEnabled = offloadSchedulingEnabled;
  }

  const auto isPlaying = shouldPlayWhenReady() && playbackInfo.playbackState == State::STATE_READY;
  auto sleepingForOffload = offloadSchedulingEnabled && requestForRendererSleep && isPlaying;

  requestForRendererSleep = false; // A sleep request is only valid for the current doSomeWork.

  // schedule refresh interval or remove refresh message
  if (sleepingForOffload || playbackInfo.playbackState == State::STATE_ENDED) {
    // No need to schedule next work.
    MINILOG_DEBUG("sleepingForOffload: {}", sleepingForOffload);
  } else if (isPlaying ||
             playbackInfo.playbackState == State::STATE_BUFFERING ||
             (playbackInfo.playbackState == State::STATE_READY && enabledRendererCount != 0)) {
    // Schedule next work as either we are actively playing, buffering, or we
    // are ready but not playing.
    scheduleNextWork(operationStartTime);
  }

  TraceUtil::endSection();

  // The analytics collector builds the context of every event from the state of the player, so it is
  // given the state once per iteration. This is the information ExoPlayer's `PlaybackInfoUpdate`
  // carries to its listeners.
  if (analyticsCollector != nullptr && playbackInfo.timeline != nullptr) {
    const auto periodIndex = playbackInfo.timeline->getIndexOfPeriod(playbackInfo.periodId.periodUid());
    auto analyticsWindowIndex = Definitions::kIndexUnset;
    if (periodIndex != Definitions::kIndexUnset) {
      const auto currentPeriod =
        playbackInfo.timeline->getPeriod(periodIndex, period, /* setIds= */ false);
      if (currentPeriod != nullptr) {
        analyticsWindowIndex = currentPeriod->windowIndex;
      }
    }
    analyticsCollector->setState(playbackInfo.timeline,
      analyticsWindowIndex,
      periodIndex != Definitions::kIndexUnset
        ? std::optional<MediaPeriodId>(playbackInfo.periodId)
        : std::nullopt,
      playbackInfo.positionUs / Definitions::kMicrosPerMillisecond,
      playbackInfo.totalBufferedDurationUs / Definitions::kMicrosPerMillisecond);
  }
}

void AvLivePlayer::scheduleNextWork(TimePoint operationStartTime) {
  auto wakeUpTimeIntervalMs =
    playbackInfo.playbackState == State::STATE_READY
    && (!shouldPlayWhenReady())
      ? IDLE_INTERVAL_MS
      : ACTIVE_INTERVAL_MS;
  // Cancel the pending work before posting the next one: removeMessages() withdraws only the
  // messages that were already queued when it is called, so posting first and removing afterwards
  // would withdraw the message this method just posted and stop the work loop for good.
  msgHandler->removeMessages(MSG_DO_SOME_WORK);
  msgHandler->sendEmptyMessageAtTime(MSG_DO_SOME_WORK,
    operationStartTime + std::chrono::milliseconds(wakeUpTimeIntervalMs));
}

void AvLivePlayer::stopInternal(const bool forceResetRenderers, bool acknowledgeStop) {
  resetInternal(/* resetRenderers= */ forceResetRenderers,
    /*resetPosition= */ false,
    /*releaseMediaSourceList= */ true,
    /*resetError= */ false);
  //  playbackInfoUpdate.incrementPendingOperationAcks(acknowledgeStop ? 1 : 0);
  loadControl->onStopped();
  if (videoEffectGraph != nullptr) {
    videoEffectGraph->stop();
  }
  setState(State::STATE_IDLE);
}

void AvLivePlayer::releaseInternal() {
  resetInternal(/* resetRenderers= */ true,
    /*resetPosition= */ false,
    /*releaseMediaSourceList= */ true,
    /*resetError= */false);
  loadControl->onReleased();
  if (videoEffectGraph != nullptr) {
    videoEffectGraph->stop();
  }
  setState(State::STATE_IDLE);
  released = true;
  threadBarrier.latch();
}

void AvLivePlayer::updateVolumeInternal(const Message* msg) {
  const auto volume = UseMessageData<Volume>(msg->msgData);
  const auto changed = mVolume.value() != volume.value();
  mVolume = volume;
  for (const auto& renderer: renderers) {
    if (renderer->getMediaType() == MediaType::kMediaTypeAudio) {
      renderer->handleMessage(Renderer::MSG_SET_VOLUME, msg);
    }
  }
  if (changed) {
    dispatchPlayerEvent([volume](EventListener& listener) {
      listener.onVolumeChanged(volume.value());
    });
  }
}

void AvLivePlayer::setSurfaceInternal(const Message* msg) {
  const auto surface = UseSharedMsgData<VideoSurface>(msg->msgData);
  if (videoEffectGraph != nullptr) {
    applyVideoEffectSurface(surface);
    return;
  }
  for (const auto& renderer: renderers) {
    if (renderer->getMediaType() == MediaType::kMediaTypeVideo) {
      renderer->handleMessage(Renderer::MSG_SET_SURFACE, msg);
    }
  }
}

void AvLivePlayer::applyVideoEffectSurface(const std::shared_ptr<VideoSurface>& surface) {
  if (videoEffectGraph == nullptr) {
    return;
  }
  // Swapping the terminal while the engine is running lets the executor submit into a dying
  // swapchain; it then disables the input side as well. Rebuild the output with the engine paused.
  const bool wasRunning = effectEngine != nullptr && effectEngine->isRunning();
  if (wasRunning) {
    effectEngine->pause();
  }
  const auto resume = [this, wasRunning] {
    if (wasRunning && effectEngine != nullptr) {
      effectEngine->resume();
    }
  };
  if (surface == nullptr || !surface->isValid()) {
    const auto nullOutput = NullOutputNode::create();
    if (nullOutput != nullptr &&
        videoEffectGraph->setOutput(nullOutput) != EffectResult::Success) {
      MINILOG_ERROR("could not detach the effect output");
    }
    resume();
    return;
  }
  if (videoOutputFactory == nullptr) {
    MINILOG_WARN("the effect pipeline has no platform output factory; the surface is ignored");
    resume();
    return;
  }

  uint32_t width = 0;
  uint32_t height = 0;
  if (videoOutputFactory->querySize(surface, &width, &height)) {
    if (videoEffectGraph->setRenderSize(width, height) != EffectResult::Success) {
      MINILOG_WARN("could not resize the effect engine to {}x{}", width, height);
    }
  }

  const auto output = videoOutputFactory->create(surface);
  if (output == nullptr) {
    MINILOG_ERROR("the platform output factory could not create an output node");
    resume();
    return;
  }
  if (videoEffectGraph->setOutput(output) != EffectResult::Success) {
    MINILOG_ERROR("the effect graph rejected the platform output node: {}",
      effectEngine != nullptr ? effectEngine->diagnostics() : std::string("no engine"));
  } else if (effectEngine != nullptr) {
    // Run time output replacement bypasses start(), so the new swapchain has to be created here.
    effectEngine->prepareOutputs();
  }
  resume();
}

void AvLivePlayer::setVideoScalingModeInternal(const Message* msg) {
  if (videoEffectGraph != nullptr) {
    const int mode = UseMessageData<int>(msg->msgData);
    // Android's SCALE_TO_FIT_WITH_CROPPING is 2; 3 is this player's stretch extension. The
    // platform constants live in the MediaCodec headers, which the playback core does not include.
    const EffectScaleMode scaleMode = mode == 2 ? EffectScaleMode::Crop
      : (mode == 3 ? EffectScaleMode::Stretch : EffectScaleMode::Fit);
    if (videoEffectGraph->setScaleMode(scaleMode) != EffectResult::Success) {
      MINILOG_WARN("could not set the effect layout mode");
    }
    return;
  }
  for (const auto& renderer: renderers) {
    if (renderer->getMediaType() == MediaType::kMediaTypeVideo) {
      renderer->handleMessage(Renderer::MSG_SET_SCALING_MODE, msg);
    }
  }
}

Tracks AvLivePlayer::getCurrentTracks() const {
  return buildCurrentTracks();
}

VideoFrameOutput AvLivePlayer::getVideoFrameOutput() const {
  const std::lock_guard<Mutex> lock(outputLock);
  return videoFrameOutput;
}

void AvLivePlayer::onVideoFrameRendered() {
  const auto positionUs = pendingVideoFramePositionUs.load(std::memory_order_relaxed);
  const std::lock_guard<Mutex> lock(outputLock);
  videoFrameOutput.surface = pendingOutputSurface;
  videoFrameOutput.positionUs = positionUs;
  ++videoFrameOutput.sequence;
}

void AvLivePlayer::selectTrack(const int rendererIndex, const int groupIndex, const int trackIndex) {
  TrackSelectionRequest request;
  request.rendererIndex = rendererIndex;
  request.groupIndex = groupIndex;
  request.trackIndex = trackIndex;
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_SELECT_TRACK, WrapMessageData(request)));
}

void AvLivePlayer::clearTrackSelection(const int rendererIndex) {
  msgHandler->sendMessage(Message::obtain(msgHandler, MSG_CLEAR_TRACK_SELECTION,
    WrapMessageData(rendererIndex)));
}

void AvLivePlayer::selectTrackInternal(const TrackSelectionRequest& request) {
  const auto selector = std::dynamic_pointer_cast<DefaultTrackSelector>(trackSelector);
  if (selector == nullptr || request.rendererIndex < 0 ||
      request.rendererIndex >= static_cast<int>(renderers.size())) {
    return;
  }
  const auto mapped = selector->getCurrentMappedTrackInfo();
  if (mapped == nullptr) {
    return;
  }
  const auto groups = mapped->getTrackGroups(request.rendererIndex);
  if (groups == nullptr || request.groupIndex < 0 || request.groupIndex >= groups->length()) {
    return;
  }
  const auto group = groups->get(request.groupIndex);
  if (group == nullptr || request.trackIndex < 0 || request.trackIndex >= group->length()) {
    return;
  }
  selector->setSelectionOverride(request.rendererIndex,
    SelectionOverride(request.groupIndex, request.trackIndex));
}

void AvLivePlayer::clearTrackSelectionInternal(const int rendererIndex) {
  const auto selector = std::dynamic_pointer_cast<DefaultTrackSelector>(trackSelector);
  if (selector == nullptr || rendererIndex < 0 ||
      rendererIndex >= static_cast<int>(renderers.size())) {
    return;
  }
  selector->setSelectionOverride(rendererIndex, std::nullopt);
}

Tracks AvLivePlayer::buildCurrentTracks() const {
  Tracks tracks;
  const auto selector = std::dynamic_pointer_cast<MappingTrackSelector>(trackSelector);
  if (selector == nullptr) {
    return tracks;
  }
  const auto mapped = selector->getCurrentMappedTrackInfo();
  if (mapped == nullptr) {
    return tracks;
  }
  const auto result = playbackInfo.trackSelectorResult;
  for (int rendererIndex = 0; rendererIndex < mapped->getRendererCount(); ++rendererIndex) {
    const auto groups = mapped->getTrackGroups(rendererIndex);
    if (groups == nullptr) {
      continue;
    }
    const auto selection = result != nullptr ? result->getSelection(rendererIndex) : nullptr;
    const auto type = trackTypeOf(mapped->getRendererType(rendererIndex));
    for (int groupIndex = 0; groupIndex < groups->length(); ++groupIndex) {
      const auto group = groups->get(groupIndex);
      if (group == nullptr) {
        continue;
      }
      TrackGroupInfo groupInfo;
      groupInfo.type = type;
      groupInfo.rendererIndex = rendererIndex;
      groupInfo.groupIndex = groupIndex;
      for (int trackIndex = 0; trackIndex < group->length(); ++trackIndex) {
        Track track;
        track.type = type;
        track.rendererIndex = rendererIndex;
        track.groupIndex = groupIndex;
        track.trackIndex = trackIndex;
        track.format = group->getFormat(trackIndex);
        track.supported = mapped->getTrackSupport(rendererIndex, groupIndex, trackIndex) ==
                          SelectionCapabilities::FORMAT_HANDLED;
        track.selected = selection != nullptr && selection->getTrackGroup() == group &&
                         selection->indexOf(trackIndex) != Definitions::kIndexUnset;
        groupInfo.selected = groupInfo.selected || track.selected;
        groupInfo.tracks.push_back(std::move(track));
      }
      tracks.groups.push_back(std::move(groupInfo));
    }
  }
  return tracks;
}

void AvLivePlayer::dispatchTracksChanged() {
  const Tracks tracks = buildCurrentTracks();
  dispatchPlayerEvent([tracks](EventListener& listener) { listener.onTracksChanged(tracks); });
}

void AvLivePlayer::setMediaItemInternal(const std::shared_ptr<MediaSource>& mediaSource) {
  auto holders = std::vector<std::shared_ptr<MediaSourceHolder>>();
  holders.push_back(std::make_shared<MediaSourceHolder>(mediaSource, true));
  shuffleOrder = shuffleOrder->cloneAndInsert(0, static_cast<int>(holders.size()));
  pendingInitialSeekPosition = SeekPosition();
  pendingInitialSeekPosition->timeline = std::make_shared<PlaylistTimeline>(holders, shuffleOrder);
  pendingInitialSeekPosition->windowIndex = 0;
  const auto timeline = mediaSourceList->setMediaSources(holders, shuffleOrder);
  handleMediaSourceListInfoRefreshed(timeline);
}

int64_t AvLivePlayer::seekToPeriodPosition(const MediaPeriodId& periodId,
  const int64_t periodPositionUs,
  const bool forceBufferingState) {
  // Force disable renderers if they are reading from a period other than the one being played.
  return seekToPeriodPosition(
    periodId,
    periodPositionUs,
    queue->getPlayingPeriod() != queue->getReadingPeriod(),
    forceBufferingState);
}

int64_t AvLivePlayer::seekToPeriodPosition(const MediaPeriodId& periodId,
  int64_t periodPositionUs,
  const bool forceDisableRenderers,
  const bool forceBufferingState) {
  stopRenderers();
  updateRebufferingState(/* isRebuffering= */ false, /* resetLastRebufferRealtimeMs= */ true);
  if (forceBufferingState || playbackInfo.playbackState == State::STATE_READY) {
    setState(State::STATE_BUFFERING);
  }

  // Find the requested period if it already exists.
  const auto oldPlayingPeriodHolder = queue->getPlayingPeriod();
  auto newPlayingPeriodHolder = oldPlayingPeriodHolder;
  while (newPlayingPeriodHolder != nullptr) {
    if (periodId == newPlayingPeriodHolder->info.id) {
      break;
    }
    newPlayingPeriodHolder = newPlayingPeriodHolder->getNext();
  }

  // Disable all renderers if the period being played is changing, if the seek results in negative
  // renderer timestamps, or if forced.
  if (forceDisableRenderers ||
      oldPlayingPeriodHolder != newPlayingPeriodHolder ||
      (newPlayingPeriodHolder != nullptr &&
       newPlayingPeriodHolder->toRendererTime(periodPositionUs) < 0)) {
    for (auto const& renderer: renderers) {
      disableRenderer(renderer);
    }
    if (newPlayingPeriodHolder != nullptr) {
      // Update the queue and re-enable renderers if the requested media period already exists.
      while (queue->getPlayingPeriod() != newPlayingPeriodHolder) {
        queue->advancePlayingPeriod();
      }
      queue->removeAfter(newPlayingPeriodHolder);
      newPlayingPeriodHolder->setRendererOffset(/* rendererPositionOffsetUs= */ 0);
      enableRenderers();
    }
  }

  // Do the actual seeking.
  if (newPlayingPeriodHolder != nullptr) {
    queue->removeAfter(newPlayingPeriodHolder);
    if (!newPlayingPeriodHolder->isPrepared()) {
      auto info = newPlayingPeriodHolder->info;
      newPlayingPeriodHolder->info = info.copyWithStartPositionUs(periodPositionUs);
    } else {
      if (newPlayingPeriodHolder->info.durationUs != Definitions::kTimeUnset &&
          periodPositionUs > newPlayingPeriodHolder->info.durationUs) {
        // Make sure seek position doesn't exceed period duration.
        periodPositionUs = std::max(static_cast<int64_t>(0), newPlayingPeriodHolder->info.durationUs - 1);
      }
      if (newPlayingPeriodHolder->hasEnabledTracks()) {
        periodPositionUs = newPlayingPeriodHolder->getMediaPeriod()->seekToUs(periodPositionUs);
        newPlayingPeriodHolder
          ->getMediaPeriod()
          ->discardBuffer(periodPositionUs - backBufferDurationUs, retainBackBufferFromKeyframe);
      }
      resetRendererPosition(periodPositionUs);
      maybeContinueLoading();
    }
  } else {
    // New period has not been prepared.
    queue->clear();
    resetRendererPosition(periodPositionUs);
  }

  handleLoadingMediaPeriodChanged(/* loadingTrackSelectionChanged= */ false);
  msgHandler->sendEmptyMessage(MSG_DO_SOME_WORK);
  return periodPositionUs;
}

void AvLivePlayer::resetRendererPosition(const int64_t positionUs) {
  const auto playingMediaPeriod = queue->getPlayingPeriod();
  rendererPositionUs = playingMediaPeriod == nullptr
                         ? positionUs
                         : playingMediaPeriod->toRendererTime(positionUs);
  mediaClock->resetPosition(rendererPositionUs);
  for (const auto& renderer: renderers) {
    if (isRendererEnabled(renderer)) {
      renderer->resetPosition(rendererPositionUs);
    }
  }
  notifyTrackSelectionDiscontinuity();
}

void AvLivePlayer::setParametersInternal(const Parameters& parameters) {
  mediaClock->setPlaybackParameters(parameters);
  sendParametersChangedInternal(mediaClock->getPlaybackParameters(), true);
}

bool AvLivePlayer::shouldTransitionToReadyState(const bool renderersReadyOrEnded) const {
  if (enabledRendererCount == 0) {
    // If there are no enabled renderers, determine whether we're ready based on the timeline.
    return isTimelineReady();
  }
  if (!renderersReadyOrEnded) {
    return false;
  }
  if (!playbackInfo.isLoading) {
    // Renderers are ready and we're not loading. Transition to ready, since the alternative is
    // getting stuck waiting for additional media that's not being loaded.
    return true;
  }
  // Renderers are ready and we're loading. Ask the LoadControl whether to transition.
  //
  // The period the load control decides about is the one that is being loaded, not the one that is
  // playing: it is the loading period that has buffered the media the player is waiting for, and its
  // buffered position is the one `getTotalBufferedDurationUs` measures the buffer with. Asking about the
  // playing period reports a buffer that is behind (or empty) once the two differ, which keeps the
  // player buffering while the media it waits for is already there.
  const auto loadingPeriodHolder = queue->getLoadingPeriod();
  if (loadingPeriodHolder == nullptr) {
    return false;
  }
  // int64_t targetLiveOffsetUs =
  //       shouldUseLivePlaybackSpeedControl(playbackInfo.timeline, loadingPeriodHolder->info.id)
  //           ? livePlaybackSpeedControl.getTargetLiveOffsetUs()
  //           : Definitions::kTimeUnset;
  const auto targetLiveOffsetUs = Definitions::kTimeUnset;
  if (loadingPeriodHolder->isFullyBuffered() && loadingPeriodHolder->info.isFinal) {
    return true;
  }
  // Get updated buffered duration as it may have changed since the start of the renderer loop.
  const auto bufferedDurationUs = getTotalBufferedDurationUs(loadingPeriodHolder->getBufferedPositionUs());

  return loadControl->shouldStartPlayback(
    LoadControl::Parameters(playbackInfo.timeline,
      loadingPeriodHolder->info.id,
      loadingPeriodHolder->toPeriodTime(rendererPositionUs),
      bufferedDurationUs,
      mediaClock->getPlaybackParameters().speed,
      playbackInfo.playWhenReady,
      isRebuffering,
      targetLiveOffsetUs,
      lastRebufferRealtimeMs
      ));
}

bool AvLivePlayer::isTimelineReady() const {
  const auto& playingPeriodHolder = queue->getPlayingPeriod();
  const auto playingPeriodDurationUs = playingPeriodHolder->getDurationUs();
  return playingPeriodHolder->isPrepared() &&
         (playingPeriodDurationUs == Definitions::kTimeUnset ||
          playbackInfo.positionUs < playingPeriodDurationUs ||
          !shouldPlayWhenReady());
}

void AvLivePlayer::handleMediaSourceListInfoRefreshed(const std::shared_ptr<Timeline>& timeline) {
  auto positionUpdate =
    resolvePositionForPlaylistChange(timeline,
      playbackInfo,
      pendingInitialSeekPosition,
      queue,
      repeatMode,
      shuffleModeEnabled,
      window,
      period);

  const auto newPeriodId = positionUpdate.periodId;
  const auto newRequestedContentPositionUs = positionUpdate.requestedContentPositionUs;
  const auto forceBufferingState = positionUpdate.forceBufferingState;
  auto newPositionUs = positionUpdate.periodPositionUs;
  const auto periodPositionChanged =
    playbackInfo.periodId != newPeriodId || newPositionUs != playbackInfo.positionUs;

  if (positionUpdate.endPlayback) {
    if (playbackInfo.playbackState != State::STATE_IDLE) {
      setState(State::STATE_ENDED);
    }
    resetInternal(
      /* resetRenderers= */ false,
      /* resetPosition= */ false,
      /* releaseMediaSourceList= */ false,
      /* resetError= */ true);
    MINILOG_DEBUG("positionUpdate.endPlayback");
  }
  if (!periodPositionChanged) {
    // We can keep the current playing period. Update the rest of the queued periods.
    if (!queue->updateQueuedPeriods(timeline, rendererPositionUs, getMaxRendererReadPositionUs())) {
      seekToCurrentPosition(/* sendDiscontinuity= */ false);
    }
  } else if (!timeline->isEmpty()) {
    // Something changed. Seek to new start position.
    auto periodHolder = queue->getPlayingPeriod();
    while (periodHolder != nullptr) {
      // Update the new playing media period info if it already exists.
      if (periodHolder->info.id == newPeriodId) {
        auto newInfo = queue->getUpdatedMediaPeriodInfo(timeline, periodHolder->info);
        if (newInfo != std::nullopt) {
          periodHolder->info = newInfo.value();
        }
      }
      periodHolder = periodHolder->getNext();
    }
    newPositionUs = seekToPeriodPosition(newPeriodId, newPositionUs, forceBufferingState);
  }

  if (periodPositionChanged ||
      newRequestedContentPositionUs != playbackInfo.requestedContentPositionUs) {
    playbackInfo = handlePositionDiscontinuity(newPeriodId, newPositionUs, newRequestedContentPositionUs);
  }
  resetPendingPauseAtEndOfPeriod();
  //  resolvePendingMessagePositions(
  //      /* newTimeline= */ timeline, /* previousTimeline= */ playbackInfo.timeline);
  playbackInfo = playbackInfo.copyWithTimeline(timeline);
  if (!timeline->isEmpty()) {
    // Retain pending seek position only while the timeline is still empty.
    pendingInitialSeekPosition = std::nullopt;
  }
  handleLoadingMediaPeriodChanged(/* loadingTrackSelectionChanged= */ false);
  msgHandler->sendEmptyMessage(MSG_DO_SOME_WORK);
}

int64_t AvLivePlayer::getMaxRendererReadPositionUs() const {
  const auto readingHolder = queue->getReadingPeriod();
  if (!readingHolder) {
    return 0;
  }
  auto maxReadPositionUs = readingHolder->getRendererOffset();
  if (!readingHolder->isPrepared()) {
    return maxReadPositionUs;
  }
  for (auto i = 0; i < renderers.size(); ++i) {
    if (!isRendererEnabled(renderers[i]) ||
        renderers[i]->getStream() != readingHolder->getSampleStream(i)) {
      // Ignore disabled renderers and renderers with sample streams from previous periods.
      continue;
    }
    const auto readingPositionUs = renderers[i]->getReadingPositionUs();
    if (readingPositionUs == Definitions::kTimeEndOfSource) {
      return Definitions::kTimeEndOfSource;
    }
    maxReadPositionUs = std::max(readingPositionUs, maxReadPositionUs);
  }
  return maxReadPositionUs;
}

void AvLivePlayer::updatePeriods() {
  if (playbackInfo.timeline->isEmpty() || !mediaSourceList->isPrepared()) {
    // No periods available.
    return;
  }
  maybeUpdateLoadingPeriod();
  maybeUpdateReadingPeriod();
  maybeUpdateReadingRenderers();
  maybeUpdatePlayingPeriod();
}

bool AvLivePlayer::maybeUpdateLoadingPeriod() {
  auto loadingPeriodChanged = false;
  queue->reevaluateBuffer(rendererPositionUs);
  if (queue->shouldLoadNextMediaPeriod()) {
    const auto periodInfo = queue->getNextMediaPeriodInfo(rendererPositionUs, playbackInfo);
    if (periodInfo != std::nullopt) {
      const auto nextPeriodHolder = queue->enqueueNextMediaPeriodHolder(periodInfo.value());
      nextPeriodHolder->getMediaPeriod()->prepare(this, periodInfo->startPositionUs);
      if (queue->getPlayingPeriod() == nextPeriodHolder) {
        resetRendererPosition(nextPeriodHolder->getStartPositionRendererTime());
      }
      handleLoadingMediaPeriodChanged(/* loadingTrackSelectionChanged= */ false);
      loadingPeriodChanged = true;
    }
  }
  if (shouldContinueLoading) {
    // We should still be loading, except in the case that it's no longer possible (i.e., because
    // we've loaded the current playlist to the end).
    shouldContinueLoading = isLoadingPossible(queue->getLoadingPeriod());
    updateIsLoading();
  } else {
    maybeContinueLoading();
  }
  return loadingPeriodChanged;
}

void AvLivePlayer::maybeUpdateReadingPeriod() {
  auto readingPeriodHolder = queue->getReadingPeriod();
  if (readingPeriodHolder == nullptr) {
    return;
  }
  if (readingPeriodHolder->getNext() == nullptr || pendingPauseAtEndOfPeriod) {
    // We don't have a successor to advance the reading period to or we want to let them end
    // intentionally to pause at the end of the period.
    if (readingPeriodHolder->info.isFinal || pendingPauseAtEndOfPeriod) {
      for (size_t i = 0; i < renderers.size(); ++i) {
        auto const& renderer = renderers[i];
        auto const& sampleStream = readingPeriodHolder->getSampleStream(i);
        if (sampleStream != nullptr
            && renderer->getStream() == sampleStream
            && renderer->hasReadStreamToEnd()) {
          renderer->setCurrentStreamFinal();
        }
      }
    }
    return;
  }

  if (!hasReadingPeriodFinishedReading()) {
    return;
  }

  if (!readingPeriodHolder->getNext()->isPrepared()
      && rendererPositionUs < readingPeriodHolder->getNext()->getStartPositionRendererTime()) {
    // The successor is not prepared yet and playback hasn't reached the transition point.
    return;
  }
  auto oldTrackSelectorResult = readingPeriodHolder->getTrackSelectorResult();
  readingPeriodHolder = queue->advanceReadingPeriod();
  auto newTrackSelectorResult = readingPeriodHolder->getTrackSelectorResult();

  if (readingPeriodHolder->isPrepared()
      && readingPeriodHolder->getMediaPeriod()->readDiscontinuity() != Definitions::kTimeUnset) {
    // The new period starts with a discontinuity, so the renderers will play out all data, then
    // be disabled and re-enabled when they start playing the next period.
    setAllRendererStreamsFinal();
    return;
  }

  for (size_t i = 0; i < renderers.size(); ++i) {
    auto oldRendererEnabled = oldTrackSelectorResult->isEnabled(i);
    auto newRendererEnabled = newTrackSelectorResult->isEnabled(i);
    if (oldRendererEnabled && !renderers[i]->isCurrentStreamFinal()) {
      auto isNoSampleRenderer =
        rendererCapabilities[i]->getMediaType() == MediaType::kMediaTypeInvalid;
      auto oldConfig = oldTrackSelectorResult->getConfiguration(i);
      auto newConfig = newTrackSelectorResult->getConfiguration(i);
      if (!newRendererEnabled || newConfig != oldConfig || isNoSampleRenderer) {
        renderers[i]->setCurrentStreamFinal();
      }
    }
  }
}

void AvLivePlayer::maybeUpdateReadingRenderers() {
  auto readingPeriod = queue->getReadingPeriod();
  if (readingPeriod == nullptr
      || queue->getPlayingPeriod() == readingPeriod
      || readingPeriod->isAllRenderersEnabled()) {
    // Not reading ahead or all renderers updated.
    return;
  }
  if (replaceStreamsOrDisableRendererForTransition()) {
    enableRenderers();
  }
}

bool AvLivePlayer::replaceStreamsOrDisableRendererForTransition() {
  auto readingPeriodHolder = queue->getReadingPeriod();
  auto newTrackSelectorResult = readingPeriodHolder->getTrackSelectorResult();
  auto needsToWaitForRendererToEnd = false;
  for (size_t i = 0; i < renderers.size(); ++i) {
    auto const& renderer = renderers[i];
    if (!isRendererEnabled(renderer)) {
      continue;
    }
    auto rendererIsReadingOldStream =
      renderer->getStream() != readingPeriodHolder->getSampleStream(i);
    auto rendererShouldBeEnabled = newTrackSelectorResult->isEnabled(i);
    if (rendererShouldBeEnabled && !rendererIsReadingOldStream) {
      // All done.
      continue;
    }
    if (!renderer->isCurrentStreamFinal()) {
      auto formats = getFormats(newTrackSelectorResult->getSelection(i));
      renderer->replaceStream(
        formats,
        readingPeriodHolder->getSampleStream(i),
        readingPeriodHolder->getStartPositionRendererTime(),
        readingPeriodHolder->getRendererOffset());
    } else if (renderer->isEnded()) {
      disableRenderer(renderer);
    } else {
      // We need to wait until rendering finished before disabling the renderer.
      needsToWaitForRendererToEnd = true;
    }
  }
  return !needsToWaitForRendererToEnd;
}

void AvLivePlayer::maybeUpdatePlayingPeriod() {
  auto advancedPlayingPeriod = false;
  while (shouldAdvancePlayingPeriod()) {
    if (advancedPlayingPeriod) {
      // If we advance more than one period at a time, notify listeners after each update.
      maybeNotifyPlaybackInfoChanged();
    }
    auto oldPlayingPeriodHolder = queue->getPlayingPeriod();
    auto newPlayingPeriodHolder = queue->advancePlayingPeriod();
    playbackInfo = handlePositionDiscontinuity(
      newPlayingPeriodHolder->info.id,
      newPlayingPeriodHolder->info.startPositionUs,
      newPlayingPeriodHolder->info.requestedContentPositionUs);
    resetPendingPauseAtEndOfPeriod();
    updatePlaybackPositions();
    advancedPlayingPeriod = true;
  }
}

void AvLivePlayer::resetPendingPauseAtEndOfPeriod() {
  auto playingPeriod = queue->getPlayingPeriod();
  pendingPauseAtEndOfPeriod = playingPeriod != nullptr && playingPeriod->info.isFinal;
}

bool AvLivePlayer::shouldAdvancePlayingPeriod() {
  if (!shouldPlayWhenReady()) {
    return false;
  }
  if (pendingPauseAtEndOfPeriod) {
    return false;
  }
  auto playingPeriodHolder = queue->getPlayingPeriod();
  if (playingPeriodHolder == nullptr) {
    return false;
  }
  auto nextPlayingHolder = playingPeriodHolder->getNext();
  return nextPlayingHolder != nullptr
         && rendererPositionUs >= nextPlayingHolder->getStartPositionRendererTime()
         && nextPlayingHolder->isAllRenderersEnabled();
}

bool AvLivePlayer::hasReadingPeriodFinishedReading() {
  auto readingPeriodHolder = queue->getReadingPeriod();
  if (!readingPeriodHolder->isPrepared()) {
    return false;
  }
  for (size_t i = 0; i < renderers.size(); ++i) {
    auto const& renderer = renderers[i];
    auto const& sampleStream = readingPeriodHolder->getSampleStream(i);
    if (renderer->getStream() != sampleStream
        || (sampleStream != nullptr && !renderer->hasReadStreamToEnd())) {
      // The current reading period is still being read by at least one renderer.
      return false;
    }
  }
  return true;
}

void AvLivePlayer::setAllRendererStreamsFinal() {
  for (auto& renderer: renderers) {
    renderer->setCurrentStreamFinal();
  }
}

void AvLivePlayer::handlePeriodPrepared(const std::shared_ptr<MediaPeriod>& mediaPeriod) {
  if (!queue->isLoading(mediaPeriod)) {
    // Stale event.
    return;
  }

  auto loadingPeriodHolder = queue->getLoadingPeriod();
  if (!loadingPeriodHolder->isPrepared()) {
    loadingPeriodHolder->handlePrepared(
      mediaClock->getPlaybackParameters().speed,
      playbackInfo.timeline,
      playbackInfo.playWhenReady);
  }
  updateLoadControlTrackSelection(loadingPeriodHolder->getTrackGroups(),
    loadingPeriodHolder->getTrackSelectorResult());
  if (loadingPeriodHolder == queue->getPlayingPeriod()) {
    auto const& info = loadingPeriodHolder->info;
    // This is the first prepared period, so update the position and the renderers.
    resetRendererPosition(info.startPositionUs);
    enableRenderers();
    playbackInfo =
      handlePositionDiscontinuity(
        playbackInfo.periodId,
        info.startPositionUs,
        playbackInfo.requestedContentPositionUs);
  }
  maybeContinueLoading();
  dispatchTracksChanged();
}

void AvLivePlayer::handleContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& mediaPeriod) {
  if (!queue->isLoading(mediaPeriod)) {
    // Stale event.
    return;
  }
  queue->reevaluateBuffer(rendererPositionUs);
  maybeContinueLoading();
}

void AvLivePlayer::handleSourceError(const std::string& errorMessage) {
  // Same as ExoPlayer's ExoPlayerImplInternal when a source error escapes a load: stop playback and
  // record the error on the playback info, which is where the player interface reads it from.
  MINILOG_ERROR("Source error: {}", errorMessage);
  stopInternal(/* forceResetRenderers= */ false, /* acknowledgeStop= */ false);
  playbackInfo = playbackInfo.copyWithPlaybackError(errorMessage);
  dispatchPlayerEvent(
    [errorMessage](EventListener& listener) { listener.onPlayerError(errorMessage); });
}

void AvLivePlayer::handlePlaybackParameters(const Parameters& parameters, bool acknowledgeCommand) {
  //  playbackInfoUpdate.incrementPendingOperationAcks(acknowledgeCommand ? 1 : 0);
  //  playbackInfo = playbackInfo.copyWithPlaybackParameters(parameters);
  updateTrackSelectionPlaybackSpeed(parameters.speed);
  for (auto const& renderer: renderers) {
    if (renderer != nullptr) {
      renderer->setOperatingRate(parameters.speed);
      // An audio renderer is also the media clock. It may not have been enabled when the player
      // applied the parameters to the clock, so apply them to the renderer itself as well; when it
      // becomes the clock source it otherwise keeps its default 1x rate.
      if (auto* clock = dynamic_cast<MediaClock*>(renderer.get()); clock != nullptr) {
        clock->setPlaybackParameters(parameters);
      }
    }
  }
  dispatchPlayerEvent([parameters](EventListener& listener) {
    listener.onPlaybackParametersChanged(parameters);
  });
}

void AvLivePlayer::maybeContinueLoading() {
  const auto loadingPeriodHolder = queue->getLoadingPeriod();
  if (!isLoadingPossible(loadingPeriodHolder)) {
    shouldContinueLoading = false;
  }
  if (loadingPeriodHolder) {
    auto bufferedDurationUs =
      getTotalBufferedDurationUs(loadingPeriodHolder->getNextLoadPositionUs());
    auto playbackPositionUs =
      loadingPeriodHolder == queue->getPlayingPeriod()
        ? loadingPeriodHolder->toPeriodTime(rendererPositionUs)
        : loadingPeriodHolder->toPeriodTime(rendererPositionUs)
          - loadingPeriodHolder->info.startPositionUs;

    // int64_t targetLiveOffsetUs =
    //       shouldUseLivePlaybackSpeedControl(playbackInfo.timeline, playingPeriodHolder->info.id)
    //           ? livePlaybackSpeedControl.getTargetLiveOffsetUs()
    //           : Definitions::kTimeUnset;
    const auto targetLiveOffsetUs = Definitions::kTimeUnset;
    auto loadParameters = LoadControl::Parameters(playbackInfo.timeline,
      loadingPeriodHolder->info.id,
      playbackPositionUs,
      bufferedDurationUs,
      mediaClock->getPlaybackParameters().speed,
      playbackInfo.playWhenReady,
      isRebuffering,
      targetLiveOffsetUs,
      lastRebufferRealtimeMs);

    shouldContinueLoading = loadControl->shouldContinueLoading(loadParameters);
    const auto playingPeriodHolder = queue->getPlayingPeriod();
    if (!shouldContinueLoading
        && playingPeriodHolder->isPrepared()
        && bufferedDurationUs < PLAYBACK_BUFFER_EMPTY_THRESHOLD_US
        && (backBufferDurationUs > 0 || retainBackBufferFromKeyframe)) {
      // LoadControl doesn't want to continue loading despite no buffered data. Clear back buffer
      // and try again in case it's blocked on memory usage of the back buffer.
      playingPeriodHolder->getMediaPeriod()->discardBuffer(
        playbackInfo.positionUs, /* toKeyframe= */ false);
      shouldContinueLoading = loadControl->shouldContinueLoading(loadParameters);
    }
    if (!shouldContinueLoading &&
        playbackInfo.playbackState == State::STATE_BUFFERING &&
        playbackInfo.playWhenReady &&
        loadingPeriodHolder == queue->getPlayingPeriod()) {
      // The player waits for media to play, so it has to keep loading whatever the load control measures.
      // The duration it measures is the smallest buffered duration over the enabled tracks, and the tracks
      // do not run out at the same time: the queue of one renderer is exhausted while the duration of
      // another one still looks large, the load control refuses to continue, the player has to buffer
      // because that renderer is not ready, and it stops the renderers: the exhausted queue is never
      // filled again and the playback stops at an arbitrary position, which is where the media that the
      // first load read ends.
      shouldContinueLoading = true;
    }
  }
  if (shouldContinueLoading) {
    queue->getLoadingPeriod()->continueLoading(rendererPositionUs);
  }
  updateIsLoading();
}

void AvLivePlayer::updateIsLoading() {
  auto const& loadingPeriod = queue->getLoadingPeriod();
  auto isLoading = shouldContinueLoading
                   || (loadingPeriod != nullptr && loadingPeriod->getMediaPeriod()->isLoading());
  if (isLoading != playbackInfo.isLoading) {
    playbackInfo.isLoading = isLoading;
    dispatchPlayerEvent([isLoading](EventListener& listener) {
      listener.onIsLoadingChanged(isLoading);
    });
  }
}

PlaybackInfo AvLivePlayer::handlePositionDiscontinuity(
  const MediaPeriodId& mediaPeriodId, int64_t positionUs, int64_t contentPositionUs) {
  resetPendingPauseAtEndOfPeriod();
  auto trackGroupArray = playbackInfo.trackGroups;
  auto trackSelectorResult = playbackInfo.trackSelectorResult;
  if (mediaSourceList->isPrepared()) {
    auto playingPeriodHolder = queue->getPlayingPeriod();
    trackGroupArray =
      playingPeriodHolder == nullptr
        ? std::make_shared<TrackGroupArray>()
        : playingPeriodHolder->getTrackGroups();
    trackSelectorResult =
      playingPeriodHolder == nullptr
        ? emptyTrackSelectorResult
        : playingPeriodHolder->getTrackSelectorResult();
  } else if (mediaPeriodId != playbackInfo.periodId) {
    // Reset previously kept track info if unprepared and the period changes.
    trackGroupArray = std::make_shared<TrackGroupArray>();
    trackSelectorResult = emptyTrackSelectorResult;
  }
  auto newPlaybackInfo = playbackInfo.copyWithNewPosition(
    mediaPeriodId,
    positionUs,
    contentPositionUs,
    getTotalBufferedDurationUs(),
    trackGroupArray,
    trackSelectorResult);
  if (positionUs != playbackInfo.positionUs || mediaPeriodId != playbackInfo.periodId) {
    // The position moved without the application asking for it, e.g. because the playback advanced
    // to the next period or because a seek was resolved to a different position. ExoPlayer reports
    // it so that an application can follow the playback position.
    dispatchPlayerEvent(
      [positionUs](EventListener& listener) { listener.onPositionDiscontinuity(positionUs); });
  }
  return newPlaybackInfo;
}

void AvLivePlayer::enableRenderers() {
  auto rendererWasEnabledFlags = std::vector<bool>(renderers.size(), false);
  enableRenderers(rendererWasEnabledFlags);
}

void AvLivePlayer::enableRenderers(const std::vector<bool>& rendererWasEnabledFlags) {
  const auto readingMediaPeriod = queue->getReadingPeriod();
  auto& trackSelectorResult = readingMediaPeriod->getTrackSelectorResult();
  // Reset all disabled renderers before enabling any new ones. This makes sure resources released
  // by the disabled renderers will be available to renderers that are being enabled.
  for (size_t i = 0; i < renderers.size(); ++i) {
    if (!trackSelectorResult->isEnabled(i)) {
      renderers[i]->reset();
    }
  }
  // Enable the renderers
  for (size_t i = 0; i < renderers.size(); ++i) {
    if (trackSelectorResult->isEnabled(i)) {
      enableRenderer(i, rendererWasEnabledFlags[i]);
    }
  }
  readingMediaPeriod->setAllRenderersEnabled();
}

void AvLivePlayer::enableRenderer(const size_t rendererIndex, const bool wasRendererEnabled) {
  if (rendererIndex >= renderers.size()) {
    return;
  }
  const auto renderer = renderers[rendererIndex];
  if (isRendererEnabled(renderer)) {
    return;
  }

  const auto periodHolder = queue->getReadingPeriod();
  const auto mayRenderStartOfStream = periodHolder == queue->getPlayingPeriod();
  auto const& trackSelectorResult = periodHolder->getTrackSelectorResult();
  auto const& rendererConfiguration =
    trackSelectorResult->getConfiguration(rendererIndex);
  auto const& newSelection = trackSelectorResult->getSelection(rendererIndex);
  auto const& formats = getFormats(newSelection);
  // The renderer needs enabling with its new track selection.
  const auto playing = shouldPlayWhenReady() && playbackInfo.playbackState == State::STATE_READY;
  // Consider as joining only if the renderer was previously disabled.
  const auto joining = !wasRendererEnabled && playing;
  // Enable the renderer
  ++enabledRendererCount;
  renderer->enable(rendererConfiguration,
    newSelection,
    periodHolder->getSampleStream(rendererIndex),
    rendererPositionUs,
    joining,
    mayRenderStartOfStream,
    periodHolder->getStartPositionRendererTime(),
    periodHolder->getRendererOffset());
  renderer->setWakeupListener(std::make_shared<WakeupListener>(*this));
  // The size of the video is read from the renderer, which reports it as soon as it decodes a frame
  // and remembers the last one it reported.
  renderer->setVideoSizeListener(videoSizeListenerRef);
  // Update the media clock enabled renderer
  mediaClock->onRendererEnabled(renderer);
  // Start the renderer if playing.
  if (playing) {
    renderer->start();
  }
}

void AvLivePlayer::startRenderers() {
  const auto playingPeriodHolder = queue->getPlayingPeriod();
  if (!playingPeriodHolder) {
    return;
  }
  isRebuffering = false;
  mediaClock->start();
  const auto trackSelectorResult = playingPeriodHolder->getTrackSelectorResult();
  for (auto i = 0; i < renderers.size(); ++i) {
    if (!trackSelectorResult->isEnabled(i)) {
      continue;
    }
    renderers[i]->start();
  }
}

void AvLivePlayer::stopRenderers() const {
  mediaClock->stop();
  for (const auto& renderer: renderers) {
    renderer->stop();
  }
}

void AvLivePlayer::disableRenderer(const std::shared_ptr<Renderer>& renderer) {
  if (!isRendererEnabled(renderer)) {
    return;
  }
  mediaClock->onRendererDisabled(renderer);
  ensureStopped(renderer);
  renderer->disable();
  // The renderer may wake the player from another thread: it must not keep a listener that points
  // back at the player once it is not driven by it any more.
  renderer->setWakeupListener(nullptr);
  renderer->setVideoSizeListener(nullptr);
  --enabledRendererCount;
}

std::pair<MediaPeriodId, int64_t>
AvLivePlayer::getPlaceholderFirstMediaPeriodPosition(const std::shared_ptr<Timeline>& timeline) const {
  if (timeline->isEmpty()) {
    return { PlaybackInfo::getDummyPeriodForEmptyTimeline(), static_cast<int64_t>(0) };
  }
  const auto firstWindowIndex = timeline->getFirstWindowIndex(shuffleModeEnabled);
  const auto firstPeriodAndPosition =
    timeline->getPeriodPosition(window, period, firstWindowIndex, Definitions::kTimeUnset);
  // Add ad metadata if any and propagate the window sequence number to new period id.
  MediaPeriodId firstPeriodId =
    queue->resolveMediaPeriodId(timeline, firstPeriodAndPosition.first, /* positionUs= */ 0);
  auto positionUs = firstPeriodAndPosition.second;
  return { firstPeriodId, positionUs };
}

void AvLivePlayer::reselectTracksInternal() {
  const auto playbackSpeed = mediaClock->getPlaybackParameters().speed;
  // Reselect tracks on each period in turn, until the selection changes.
  auto periodHolder = queue->getPlayingPeriod();
  const auto readingPeriodHolder = queue->getReadingPeriod();
  auto selectionsChangedForReadPeriod = true;
  std::shared_ptr<TrackSelectorResult> newTrackSelectorResult;
  while (true) {
    if (periodHolder == nullptr || !periodHolder->isPrepared()) {
      // The reselection did not change any prepared periods.
      return;
    }
    newTrackSelectorResult = periodHolder->selectTracks(playbackSpeed, playbackInfo.timeline, shouldPlayWhenReady());
    if (!newTrackSelectorResult->isEquivalent(periodHolder->getTrackSelectorResult())) {
      // Selected tracks have changed for this period.
      break;
    }
    if (periodHolder == readingPeriodHolder) {
      // The track reselection didn't affect any period that has been read.
      selectionsChangedForReadPeriod = false;
    }
    periodHolder = periodHolder->getNext();
  }

  if (selectionsChangedForReadPeriod) {
    // Update streams and rebuffer for the new selection, recreating all streams if reading ahead.
    const auto playingPeriodHolder = queue->getPlayingPeriod();
    const auto recreateStreams = queue->removeAfter(playingPeriodHolder);
    auto streamResetFlags = std::vector<bool>(renderers.size());
    const auto periodPositionUs =
      playingPeriodHolder->applyTrackSelection(newTrackSelectorResult,
        playbackInfo.positionUs,
        recreateStreams,
        streamResetFlags);
    playbackInfo = handlePositionDiscontinuity(playbackInfo.periodId,
      periodPositionUs, playbackInfo.requestedContentPositionUs);
    if (playbackInfo.playbackState != State::STATE_ENDED &&
        periodPositionUs != playbackInfo.positionUs) {
      //      playbackInfoUpdate.setPositionDiscontinuity(Player.DISCONTINUITY_REASON_INTERNAL);
      resetRendererPosition(periodPositionUs);
    }

    auto rendererWasEnabledFlags = std::vector<bool>(renderers.size());
    for (size_t i = 0; i < renderers.size(); ++i) {
      auto const& renderer = renderers[i];
      rendererWasEnabledFlags[i] = isRendererEnabled(renderer);
      auto const& sampleStream = playingPeriodHolder->getSampleStream(i);
      if (rendererWasEnabledFlags[i]) {
        if (sampleStream != renderer->getStream()) {
          // We need to disable the renderer.
          disableRenderer(renderer);
        } else if (streamResetFlags[i]) {
          // The renderer will continue to consume from its current stream, but needs to be reset.
          renderer->resetPosition(rendererPositionUs);
        }
      }
    }
    enableRenderers(rendererWasEnabledFlags);
  } else {
    // Release and re-prepare/buffer periods after the one whose selection changed.
    queue->removeAfter(periodHolder);
    if (periodHolder->isPrepared()) {
      const auto loadingPeriodPositionUs =
        std::max(periodHolder->info.startPositionUs, periodHolder->toPeriodTime(rendererPositionUs));
      periodHolder->applyTrackSelection(newTrackSelectorResult, loadingPeriodPositionUs, false);
    }
  }
  handleLoadingMediaPeriodChanged(/* loadingTrackSelectionChanged= */ true);
  if (playbackInfo.playbackState != State::STATE_ENDED) {
    maybeContinueLoading();
    updatePlaybackPositions();
    msgHandler->sendEmptyMessage(MSG_DO_SOME_WORK);
  }
  dispatchTracksChanged();
}

void AvLivePlayer::updateTrackSelectionPlaybackSpeed(const float playbackSpeed) const {
  auto periodHolder = queue->getPlayingPeriod();
  while (periodHolder != nullptr) {
    if (periodHolder->getTrackSelectorResult() == nullptr ||
        periodHolder->getTrackSelectorResult()->getSelections() == nullptr) {
      continue;
    }
    auto trackSelections = periodHolder->getTrackSelectorResult()->getSelections()->getAll();
    for (auto const& trackSelection: trackSelections) {
      if (trackSelection != nullptr) {
        trackSelection->onPlaybackSpeed(playbackSpeed);
      }
    }
    periodHolder = periodHolder->getNext();
  }
}

void AvLivePlayer::notifyTrackSelectionDiscontinuity() const {
  auto periodHolder = queue->getPlayingPeriod();
  while (periodHolder != nullptr) {
    if (periodHolder->getTrackSelectorResult() == nullptr ||
        periodHolder->getTrackSelectorResult()->getSelections() == nullptr) {
      continue;
    }
    auto trackSelections = periodHolder->getTrackSelectorResult()->getSelections()->getAll();
    for (auto const& trackSelection: trackSelections) {
      if (trackSelection != nullptr) {
        trackSelection->onDiscontinuity();
      }
    }
    periodHolder = periodHolder->getNext();
  }
}

void AvLivePlayer::resetInternal(const bool resetRenderers,
  const bool resetPosition,
  const bool releaseMediaSourceList,
  bool resetError) {
  msgHandler->removeMessages(MSG_DO_SOME_WORK);
  updateRebufferingState(/* isRebuffering= */ false, /* resetLastRebufferRealtimeMs= */ true);
  mediaClock->stop();
  rendererPositionUs = 0;
  for (auto& renderer: renderers) {
    disableRenderer(renderer);
  }
  if (resetRenderers) {
    for (const auto& renderer: renderers) {
      renderer->reset();
    }
  }
  enabledRendererCount = 0;

  auto mediaPeriodId = playbackInfo.periodId;
  auto startPositionUs = playbackInfo.positionUs;
  auto requestedContentPositionUs =
    shouldUseRequestedContentPosition(playbackInfo, period, window)
      ? playbackInfo.requestedContentPositionUs
      : playbackInfo.positionUs;
  auto resetTrack = false;
  if (resetPosition) {
    pendingInitialSeekPosition = std::nullopt;
    auto [periodUid, positionUs] = getPlaceholderFirstMediaPeriodPosition(playbackInfo.timeline);
    mediaPeriodId = periodUid;
    startPositionUs = positionUs;
    requestedContentPositionUs = Definitions::kTimeUnset;
    if (mediaPeriodId != playbackInfo.periodId) {
      resetTrack = true;
    }
  }

  queue->clear();
  shouldContinueLoading = false;

  playbackInfo = PlaybackInfo(playbackInfo.timeline,
    mediaPeriodId,
    requestedContentPositionUs,
    playbackInfo.playbackState,
    resetError ? std::string() : playbackInfo.playbackErrorMessage,
    false,
    resetTrack ? std::shared_ptr<TrackGroupArray>() : playbackInfo.trackGroups,
    resetTrack ? emptyTrackSelectorResult : playbackInfo.trackSelectorResult,
    mediaPeriodId,
    playbackInfo.playWhenReady,
    playbackInfo.parameters,
    startPositionUs,
    0,
    startPositionUs,
    offloadSchedulingEnabled);
  if (releaseMediaSourceList) {
    mediaSourceList->release();
  }
}

void AvLivePlayer::handleLoadingMediaPeriodChanged(const bool loadingTrackSelectionChanged) {
  const auto loadingMediaPeriodHolder = queue->getLoadingPeriod();
  if (!loadingMediaPeriodHolder) {
    return;
  }
  const auto loadingMediaPeriodId =
    loadingMediaPeriodHolder == nullptr
      ? playbackInfo.periodId
      : loadingMediaPeriodHolder->info.id;
  const auto loadingMediaPeriodChanged = playbackInfo.loadingMediaPeriodId != loadingMediaPeriodId;
  if (loadingMediaPeriodChanged) {
    playbackInfo.loadingMediaPeriodId = loadingMediaPeriodId;
  }
  playbackInfo.bufferedPositionUs =
    loadingMediaPeriodHolder == nullptr
      ? playbackInfo.positionUs
      : loadingMediaPeriodHolder->getBufferedPositionUs();
  playbackInfo.totalBufferedDurationUs = getTotalBufferedDurationUs();
  if ((loadingMediaPeriodChanged || loadingTrackSelectionChanged) &&
      loadingMediaPeriodHolder != nullptr &&
      loadingMediaPeriodHolder->isPrepared()) {
    updateLoadControlTrackSelection(loadingMediaPeriodHolder->getTrackGroups(),
      loadingMediaPeriodHolder->getTrackSelectorResult());
  }
}

int64_t AvLivePlayer::getTotalBufferedDurationUs(const int64_t bufferedPositionInLoadingPeriodUs) const {
  const auto loadingPeriodHolder = queue->getLoadingPeriod();
  if (!loadingPeriodHolder) {
    return 0;
  }
  const auto totalBufferedDurationUs =
    bufferedPositionInLoadingPeriodUs - loadingPeriodHolder->toPeriodTime(rendererPositionUs);
  return std::max(static_cast<int64_t>(0), totalBufferedDurationUs);
}

void AvLivePlayer::updateLoadControlTrackSelection(const std::shared_ptr<TrackGroupArray>& trackGroups,
  const std::shared_ptr<TrackSelectorResult>& trackSelectorResult) const {
  const auto loadingPeriodHolder = queue->getLoadingPeriod();
  if (!loadingPeriodHolder) {
    return;
  }
  const auto bufferedDurationUs =
    getTotalBufferedDurationUs(loadingPeriodHolder->getNextLoadPositionUs());
  const auto playbackPositionUs =
    loadingPeriodHolder == queue->getPlayingPeriod()
      ? loadingPeriodHolder->toPeriodTime(rendererPositionUs)
      : loadingPeriodHolder->toPeriodTime(rendererPositionUs)
        - loadingPeriodHolder->info.startPositionUs;

  // int64_t targetLiveOffsetUs =
  //       shouldUseLivePlaybackSpeedControl(playbackInfo.timeline, playingPeriodHolder->info.id)
  //           ? livePlaybackSpeedControl.getTargetLiveOffsetUs()
  //           : Definitions::kTimeUnset;
  const auto targetLiveOffsetUs = Definitions::kTimeUnset;
  loadControl->onTracksSelected(LoadControl::Parameters(playbackInfo.timeline,
      loadingPeriodHolder->info.id,
      playbackPositionUs,
      bufferedDurationUs,
      mediaClock->getPlaybackParameters().speed,
      playbackInfo.playWhenReady,
      isRebuffering,
      targetLiveOffsetUs,
      lastRebufferRealtimeMs),
    trackGroups,
    trackSelectorResult->getSelections());
}

void AvLivePlayer::sendParametersChangedInternal(const Parameters& parameters, const bool acknowledgeCommand) {
  playbackInfo.parameters = parameters;
  handlePlaybackParameters(parameters, acknowledgeCommand);
}

bool AvLivePlayer::isLoadingPossible(const std::shared_ptr<MediaPeriodHolder>& mediaPeriodHolder) {
  return mediaPeriodHolder != nullptr &&
         mediaPeriodHolder->getNextLoadPositionUs() != Definitions::kTimeEndOfSource;
}

void AvLivePlayer::ensureStopped(const std::shared_ptr<Renderer>& renderer) {
  if (renderer != nullptr && renderer->getStatus() == Renderer::Status::kStatusRendering) {
    renderer->stop();
  }
}

bool AvLivePlayer::isRendererEnabled(const std::shared_ptr<Renderer>& renderer) {
  return renderer != nullptr && renderer->getStatus() != Renderer::Status::kStatusDisabled;
}

std::vector<std::shared_ptr<MediaFormat>>
AvLivePlayer::getFormats(const std::shared_ptr<TrackSelection>& trackSelection) {
  const auto numSelection = trackSelection != nullptr ? trackSelection->length() : 0;
  std::vector<std::shared_ptr<MediaFormat>> formats;
  if (numSelection > 0) {
    formats.resize(numSelection);
    for (auto track = 0; track < numSelection; ++track) {
      formats[track] = trackSelection->getFormat(track);
    }
  }
  return formats;
}

AvLivePlayer::PlaylistChange
AvLivePlayer::resolvePositionForPlaylistChange(const std::shared_ptr<Timeline>& timeline,
  const PlaybackInfo& playbackInfo,
  const std::optional<SeekPosition>& pendingInitialSeekPosition,
  const std::shared_ptr<MediaPeriodQueue>& queue,
  const RepeatMode& repeatMode,
  bool shuffleModeEnabled,
  const std::shared_ptr<Timeline::Window>& window,
  const std::shared_ptr<Timeline::Period>& period) {
  if (timeline->isEmpty()) {
    return PlaylistChange(PlaybackInfo::getDummyPeriodForEmptyTimeline(),
      /* periodPositionUs= */ 0,
      /* requestedContentPositionUs= */ Definitions::kTimeUnset,
      /* forceBufferingState= */ false,
      /* endPlayback= */ true);
  }

  auto oldPeriodId = playbackInfo.periodId;
  auto newPeriodUid = oldPeriodId.periodUid();
  auto useRequestedContentPosition = shouldUseRequestedContentPosition(playbackInfo, period, window);
  auto oldContentPositionUs =
    useRequestedContentPosition
      ? playbackInfo.requestedContentPositionUs
      : playbackInfo.positionUs;
  auto newContentPositionUs = oldContentPositionUs;
  auto startAtDefaultPositionWindowIndex = Definitions::kIndexUnset;
  auto forceBufferingState = false;
  auto endPlayback = false;
  if (pendingInitialSeekPosition != std::nullopt) {
    // Resolve initial seek position.
    auto periodPosition =
      resolveSeekPositionUs(
        timeline,
        pendingInitialSeekPosition,
        /* trySubsequentPeriods= */ true,
        repeatMode,
        shuffleModeEnabled,
        window,
        period);
    if (periodPosition == std::pair<PeriodUid, int64_t>()) {
      // The initial seek in the empty old timeline is invalid in the new timeline.
      endPlayback = true;
      startAtDefaultPositionWindowIndex = timeline->getFirstWindowIndex(shuffleModeEnabled);
    } else {
      // The pending seek has been resolved successfully in the new timeline.
      if (pendingInitialSeekPosition->windowPositionUs == Definitions::kTimeUnset) {
        startAtDefaultPositionWindowIndex =
          timeline->getPeriodByUid(periodPosition.first, period)->windowIndex;
      } else {
        newPeriodUid = periodPosition.first;
        newContentPositionUs = periodPosition.second;
      }
      forceBufferingState = playbackInfo.playbackState == State::STATE_ENDED;
    }
  } else if (playbackInfo.timeline->isEmpty()) {
    // Resolve to default position if the old timeline is empty and no seek is requested above.
    startAtDefaultPositionWindowIndex = timeline->getFirstWindowIndex(shuffleModeEnabled);
  } else if (timeline->getIndexOfPeriod(newPeriodUid) == Definitions::kIndexUnset) {
    // The current period isn't in the new timeline. Attempt to resolve a subsequent period whose
    // window we can restart from.
    auto subsequentPeriodUid =
      resolveSubsequentPeriod(window,
        period,
        repeatMode,
        shuffleModeEnabled,
        newPeriodUid,
        playbackInfo.timeline,
        timeline);
    if (subsequentPeriodUid == std::nullopt) {
      // We failed to resolve a suitable restart position but the timeline is not empty.
      endPlayback = true;
      startAtDefaultPositionWindowIndex = timeline->getFirstWindowIndex(shuffleModeEnabled);
    } else {
      // We resolved a subsequent period. Start at the default position in the corresponding
      // window.
      startAtDefaultPositionWindowIndex =
        timeline->getPeriodByUid(subsequentPeriodUid, period)->windowIndex;
    }
  } else if (useRequestedContentPosition) {
    // We previously requested a content position, but haven't used it yet. Re-resolve the
    // requested window position to the period uid and position in case they changed.
    if (oldContentPositionUs == Definitions::kTimeUnset) {
      startAtDefaultPositionWindowIndex =
        timeline->getPeriodByUid(newPeriodUid, period)->windowIndex;
    } else {
      playbackInfo.timeline->getPeriodByUid(oldPeriodId.periodUid(), period);
      auto windowPositionUs = oldContentPositionUs + period->getPositionInWindowUs();
      auto windowIndex = timeline->getPeriodByUid(newPeriodUid, period)->windowIndex;
      const auto [periodUid, positionUs] =
        timeline->getPeriodPosition(window, period, windowIndex, windowPositionUs);
      newPeriodUid = periodUid;
      newContentPositionUs = positionUs;
    }
  }

  // Set period uid for default positions and resolve position for ad resolution.
  auto contentPositionForAdResolutionUs = newContentPositionUs;
  if (startAtDefaultPositionWindowIndex != Definitions::kIndexUnset) {
    const auto defaultPosition =
      timeline->getPeriodPosition(window, period,
        startAtDefaultPositionWindowIndex, Definitions::kTimeUnset);
    newPeriodUid = defaultPosition.first;
    contentPositionForAdResolutionUs = defaultPosition.second;
    newContentPositionUs = Definitions::kTimeUnset;
  }

  // Ensure ad insertion metadata is up to date.
  auto periodIdWithAds = queue->resolveMediaPeriodId(timeline, newPeriodUid, contentPositionForAdResolutionUs);
  auto earliestCuePointIsUnchangedOrLater = true;
  auto oldAndNewPeriodIdAreSame = oldPeriodId.periodUid() == newPeriodUid;
  auto newPeriodId = oldAndNewPeriodIdAreSame ? oldPeriodId : periodIdWithAds;

  auto periodPositionUs = contentPositionForAdResolutionUs;

  return PlaylistChange(newPeriodId, periodPositionUs, newContentPositionUs, forceBufferingState, endPlayback);
}

bool AvLivePlayer::shouldUseRequestedContentPosition(const PlaybackInfo& playbackInfo,
  const std::shared_ptr<Timeline::Period>& period,
  const std::shared_ptr<Timeline::Window>& window) {
  // Only use the actual position as content position if it's not an ad and we already have
  // prepared media information. Otherwise use the requested position.
  const auto periodId = playbackInfo.periodId;
  const auto timeline = playbackInfo.timeline;
  return timeline->isEmpty() ||
         timeline->getWindow(timeline->getPeriodByUid(periodId.periodUid(), period)->windowIndex, window)
                 ->isPlaceholder;
}

std::pair<PeriodUid, int64_t>
AvLivePlayer::resolveSeekPositionUs(const std::shared_ptr<Timeline>& timeline,
  const std::optional<SeekPosition>& seekPosition,
  const bool trySubsequentPeriods,
  const RepeatMode& repeatMode,
  const bool shuffleModeEnabled,
  const std::shared_ptr<Timeline::Window>& window,
  const std::shared_ptr<Timeline::Period>& period) {
  auto seekTimeline = seekPosition->timeline;
  if (timeline->isEmpty()) {
    // We don't have a valid timeline yet, so we can't resolve the position.
    return {};
  }
  if (seekTimeline->isEmpty()) {
    // The application performed a blind seek with an empty timeline (most likely based on
    // knowledge of what the future timeline will be). Use the internal timeline.
    seekTimeline = timeline;
  }
  // Map the SeekPosition to a position in the corresponding timeline.
  auto periodPosition =
    seekTimeline->getPeriodPosition(window, period, seekPosition->windowIndex, seekPosition->windowPositionUs);
  if (timeline == seekTimeline) {
    // Our internal timeline is the seek timeline, so the mapped position is correct.
    return periodPosition;
  }
  // Attempt to find the mapped period in the internal timeline.
  if (timeline->getIndexOfPeriod(periodPosition.first) != Definitions::kIndexUnset) {
    // We successfully located the period in the internal timeline.
    seekTimeline->getPeriodByUid(periodPosition.first, period);
    if (seekTimeline->getWindow(period->windowIndex, window)->isPlaceholder) {
      // The seek timeline was using a placeholder, so we need to re-resolve using the updated
      // timeline in case the resolved position changed.
      const auto newWindowIndex = timeline->getPeriodByUid(periodPosition.first, period)->windowIndex;
      periodPosition = timeline->getPeriodPosition(window, period, newWindowIndex, seekPosition->windowPositionUs);
    }
    return periodPosition;
  }
  if (trySubsequentPeriods) {
    // Try and find a subsequent period from the seek timeline in the internal timeline.
    const auto periodUid =
      resolveSubsequentPeriod(window,
        period,
        repeatMode,
        shuffleModeEnabled,
        periodPosition.first,
        seekTimeline,
        timeline);
    if (periodUid != std::nullopt) {
      // We found one. Use the default position of the corresponding window.
      return timeline->getPeriodPosition(window,
        period,
        timeline->getPeriodByUid(periodUid, period)->windowIndex,
        Definitions::kTimeUnset);
    }
  }
  // We didn't find one. Give up.
  return {};
}

std::optional<PeriodUid>
AvLivePlayer::resolveSubsequentPeriod(const std::shared_ptr<Timeline::Window>& window,
  const std::shared_ptr<Timeline::Period>& period,
  const RepeatMode& repeatMode,
  const bool shuffleModeEnabled,
  const PeriodUid& oldPeriodUid,
  const std::shared_ptr<Timeline>& oldTimeline,
  const std::shared_ptr<Timeline>& newTimeline) {
  auto oldPeriodIndex = oldTimeline->getIndexOfPeriod(oldPeriodUid);
  auto newPeriodIndex = Definitions::kIndexUnset;
  const auto maxIterations = oldTimeline->getPeriodCount();
  for (auto i = 0; i < maxIterations && newPeriodIndex == Definitions::kIndexUnset; ++i) {
    oldPeriodIndex = oldTimeline->getNextPeriodIndex(oldPeriodIndex, period, window, repeatMode, shuffleModeEnabled);
    if (oldPeriodIndex == Definitions::kIndexUnset) {
      // We've reached the end of the old timeline.
      break;
    }
    newPeriodIndex = newTimeline->getIndexOfPeriod(oldTimeline->getUidOfPeriod(oldPeriodIndex));
  }
  return newPeriodIndex == Definitions::kIndexUnset ? std::nullopt : newTimeline->getUidOfPeriod(newPeriodIndex);
}

//--------------------------------------------------------------------------------------------------

void AvLivePlayer::WakeupListener::onSleep(const int64_t wakeupDeadlineMs) {
  // Do not sleep if the expected sleep time is not long enough to save significant power.
  if (wakeupDeadlineMs >= MIN_RENDERER_SLEEP_DURATION_MS) {
    player.requestForRendererSleep = true;
  }
}

void AvLivePlayer::WakeupListener::onWakeup() {
  if (player.msgHandler && player.offloadSchedulingEnabled) {
    player.msgHandler->sendEmptyMessage(MSG_DO_SOME_WORK);
  }
}

} // namespace coral
