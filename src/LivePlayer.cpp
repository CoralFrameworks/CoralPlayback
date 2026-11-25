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

#include <coral/playback/LivePlayer.h>
#include <coral/source/MediaItem.h>
#include <coral/source/MergingMediaSourceFactory.h>

#include "details/AvLivePlayer.h"
#include "details/DecodeAudioRenderer.h"
#include "details/MetadataRenderer.h"
#include "details/TextRenderer.h"
#include "details/DecodeVideoRenderer.h"
#include "details/ImageRenderer.h"

using namespace coral;

namespace coral {

namespace {

std::vector<std::shared_ptr<Renderer>> createRenderers(
    const LivePlayer::Configuration& configuration) {
  auto renderers = std::vector<std::shared_ptr<Renderer>>();
  if (configuration.audioEnabled) {
    const int count = configuration.audioRendererCount > 0 ? configuration.audioRendererCount : 1;
    for (int index = 0; index < count; ++index) {
      renderers.push_back(std::make_shared<DecodeAudioRenderer>());
    }
  }
  if (configuration.videoEnabled) {
    renderers.push_back(std::make_shared<DecodeVideoRenderer>());
  }
  if (configuration.imageEnabled) {
    renderers.push_back(std::make_shared<ImageRenderer>());
  }
  if (configuration.textEnabled) {
    renderers.push_back(std::make_shared<TextRenderer>());
  }
  if (configuration.metadataEnabled) {
    renderers.push_back(std::make_shared<MetadataRenderer>());
  }
  return renderers;
}

} // namespace

std::unique_ptr<LivePlayer> LivePlayer::create(const std::shared_ptr<MediaItem>& mediaItem) {
  return create(mediaItem, Configuration{});
}

std::unique_ptr<LivePlayer> LivePlayer::create(const std::shared_ptr<MediaItem>& mediaItem,
                                               const Configuration& configuration) {
  if (mediaItem == nullptr) {
    return nullptr;
  }
  auto player = std::make_unique<LivePlayer>(
      std::make_unique<AvLivePlayer>(createRenderers(configuration)));
  player->mImpl->setMediaSourceFactory(std::make_shared<MergingMediaSourceFactory>());
  player->mImpl->setMediaItem(mediaItem);
  return player;
}

std::unique_ptr<LivePlayer> LivePlayer::create(const std::string& url) {
  return create(url, Configuration{});
}

std::unique_ptr<LivePlayer> LivePlayer::create(const std::string& url,
                                               const Configuration& configuration) {
  auto player = std::make_unique<LivePlayer>(
      std::make_unique<AvLivePlayer>(createRenderers(configuration)));
  player->setMediaItem(MediaItem::fromUrl(url));
  return player;
}

LivePlayer::LivePlayer(std::unique_ptr<Player> player) noexcept
    : mImpl(player.release()) {}

LivePlayer::~LivePlayer() {
  if (mImpl) {
    mImpl->release();
  }
  mImpl.reset();
}

void LivePlayer::enableVideoEffectPipeline(
    const std::shared_ptr<VideoOutputNodeFactory>& factory) {
  if (auto* player = dynamic_cast<AvLivePlayer*>(mImpl.get()); player != nullptr) {
    player->enableVideoEffectPipeline(factory);
  }
}

void LivePlayer::enableVideoEffectPipeline(
    const std::shared_ptr<VideoOutputNodeFactory>& factory,
    const VideoEffectInput& input) {
  if (auto* player = dynamic_cast<AvLivePlayer*>(mImpl.get()); player != nullptr) {
    player->enableVideoEffectPipeline(factory, input);
  }
}

bool LivePlayer::isVideoEffectPipelineEnabled() const {
  if (const auto* player = dynamic_cast<const AvLivePlayer*>(mImpl.get()); player != nullptr) {
    return player->isVideoEffectPipelineEnabled();
  }
  return false;
}

std::shared_ptr<EffectEngine> LivePlayer::getEffectEngine() const {
  if (const auto* player = dynamic_cast<const AvLivePlayer*>(mImpl.get()); player != nullptr) {
    return player->getEffectEngine();
  }
  return nullptr;
}

bool LivePlayer::isSubtitleOverlayActive() const {
  if (const auto* player = dynamic_cast<const AvLivePlayer*>(mImpl.get()); player != nullptr) {
    return player->isSubtitleOverlayActive();
  }
  return false;
}

bool LivePlayer::addLottieOverlay(const LottieOverlayConfig& config) {
  if (auto* player = dynamic_cast<AvLivePlayer*>(mImpl.get()); player != nullptr) {
    return player->addLottieOverlay(config);
  }
  return false;
}

void LivePlayer::clearLottieOverlays() {
  if (auto* player = dynamic_cast<AvLivePlayer*>(mImpl.get()); player != nullptr) {
    player->clearLottieOverlays();
  }
}

void LivePlayer::setOutputSurface(const std::shared_ptr<VideoSurface>& surface) {
  mImpl->setOutputSurface(surface);
}

void LivePlayer::setVideoScalingMode(int mode) {
  mImpl->setVideoScalingMode(mode);
}

void LivePlayer::setVideoScaleMode(const VideoScaleMode mode) {
  mImpl->setVideoScaleMode(mode);
}

LivePlayer::VideoScaleMode LivePlayer::getVideoScaleMode() const {
  return mImpl->getVideoScaleMode();
}

void LivePlayer::setVideoMirror(const bool horizontal, const bool vertical) {
  mImpl->setVideoMirror(horizontal, vertical);
}

void LivePlayer::setTextOutput(const std::shared_ptr<TextOutput>& output) {
  mImpl->setTextOutput(output);
}

Tracks LivePlayer::getCurrentTracks() const {
  return mImpl->getCurrentTracks();
}

VideoFrameOutput LivePlayer::getVideoFrameOutput() const {
  return mImpl->getVideoFrameOutput();
}

VideoColorInfo LivePlayer::getVideoColorInfo() const {
  return mImpl->getVideoColorInfo();
}

void LivePlayer::selectTrack(int rendererIndex, int groupIndex, int trackIndex) {
  mImpl->selectTrack(rendererIndex, groupIndex, trackIndex);
}

void LivePlayer::clearTrackSelection(int rendererIndex) {
  mImpl->clearTrackSelection(rendererIndex);
}

void LivePlayer::setPreferredTextLanguage(const std::string& language, const bool selectUndetermined) {
  mImpl->setPreferredTextLanguage(language, selectUndetermined);
}

void LivePlayer::setSubtitleFile(const std::string& path, const std::string& mimeType) {
  mImpl->setSubtitleFile(path, mimeType);
}

std::shared_ptr<TextOutput> LivePlayer::getTextOutput() const {
  return mImpl->getTextOutput();
}

void LivePlayer::setMetadataOutput(const std::shared_ptr<MetadataOutput>& output) {
  mImpl->setMetadataOutput(output);
}

std::shared_ptr<MetadataOutput> LivePlayer::getMetadataOutput() const {
  return mImpl->getMetadataOutput();
}

void LivePlayer::setSubtitleRenderMode(const SubtitleRenderMode mode) {
  mImpl->setSubtitleRenderMode(mode);
}

SubtitleRenderMode LivePlayer::getSubtitleRenderMode() const {
  return mImpl->getSubtitleRenderMode();
}
void LivePlayer::setSubtitleEnabled(const bool enabled) {
  mImpl->setSubtitleEnabled(enabled);
}

bool LivePlayer::isSubtitleEnabled() const {
  return mImpl->isSubtitleEnabled();
}

void LivePlayer::setMediaItem(const std::shared_ptr<MediaItem>& mediaItem) {
  mImpl->setMediaItem(mediaItem);
}

void LivePlayer::prepare() {
  mImpl->prepare();
}

const Player::State& LivePlayer::getPlaybackState() const {
  return mImpl->getPlaybackState();
}

const std::string& LivePlayer::getPlaybackErrorMessage() const {
  return mImpl->getPlaybackErrorMessage();
}

bool LivePlayer::isPlaying() const {
  return mImpl->isPlaying();
}

void LivePlayer::play() {
  mImpl->play();
}

void LivePlayer::pause() {
  mImpl->pause();
}

void LivePlayer::stop() {
  mImpl->stop();
}

void LivePlayer::release() {
  mImpl->release();
}

void LivePlayer::setPlayWhenReady(bool playWhenReady) {
  mImpl->setPlayWhenReady(playWhenReady);
}

bool LivePlayer::getPlayWhenReady() const {
  return mImpl->getPlayWhenReady();
}

void LivePlayer::setRepeatMode(RepeatMode mode) {
  mImpl->setRepeatMode(mode);
}

const RepeatMode& LivePlayer::getRepeatMode() const {
  return mImpl->getRepeatMode();
}

bool LivePlayer::isLoading() const {
  return mImpl->isLoading();
}

void LivePlayer::seekTo(int64_t timeUs) {
  mImpl->seekTo(timeUs);
}

void LivePlayer::seekToDefaultPosition() {
  mImpl->seekToDefaultPosition();
}

void LivePlayer::setPlaybackParameters(const Parameters& playbackParameters) {
  mImpl->setPlaybackParameters(playbackParameters);
}

const Parameters& LivePlayer::getPlaybackParameters() const {
  return mImpl->getPlaybackParameters();
}

int64_t LivePlayer::getCurrentPositionUs() const {
  return mImpl->getCurrentPositionUs();
}

int64_t LivePlayer::getBufferedPositionUs() const {
  return mImpl->getBufferedPositionUs();
}

std::vector<Player::BufferedRange> LivePlayer::getBufferedRanges() const {
  return mImpl->getBufferedRanges();
}

int64_t LivePlayer::getDurationUs() const {
  return mImpl->getDurationUs();
}

void LivePlayer::setLooping(bool looping) {
  if (looping) {
    setRepeatMode(RepeatMode::REPEAT_MODE_ALL);
  } else {
    setRepeatMode(RepeatMode::REPEAT_MODE_OFF);
  }
}

float LivePlayer::getVolume() const {
  return mImpl->getVolume();
}

void LivePlayer::setVolume(const Volume& volume) {
  mImpl->setVolume(volume);
}

int LivePlayer::getVideoHeight() const {
  return mImpl->getVideoHeight();
}

int LivePlayer::getVideoWidth() const {
  return mImpl->getVideoWidth();
}

void LivePlayer::addListener(const std::shared_ptr<EventListener>& listener) {
  mImpl->addListener(listener);
}

void LivePlayer::removeListener(const std::shared_ptr<EventListener>& listener) {
  mImpl->removeListener(listener);
}

void LivePlayer::addMediaSourceEventListener(
    const std::shared_ptr<MediaSourceEventListener>& listener) {
  mImpl->addMediaSourceEventListener(listener);
}

void LivePlayer::removeMediaSourceEventListener(
    const std::shared_ptr<MediaSourceEventListener>& listener) {
  mImpl->removeMediaSourceEventListener(listener);
}

void LivePlayer::addAnalyticsListener(const std::shared_ptr<AnalyticsListener>& listener) {
  mImpl->addAnalyticsListener(listener);
}

void LivePlayer::removeAnalyticsListener(const std::shared_ptr<AnalyticsListener>& listener) {
  mImpl->removeAnalyticsListener(listener);
}

int LivePlayer::getMediaItemCount() const {
  return mImpl->getMediaItemCount();
}

const std::shared_ptr<MediaItem>& LivePlayer::getMediaItemAt(int index) const {
  return mImpl->getMediaItemAt(index);
}

void LivePlayer::addMediaItems(int index,
    const std::vector<std::shared_ptr<MediaItem>>& mediaItems) {
  mImpl->addMediaItems(index, mediaItems);
}

void LivePlayer::removeMediaItems(int fromIndex, int toIndex) {
  mImpl->removeMediaItems(fromIndex, toIndex);
}

void LivePlayer::moveMediaItems(int fromIndex, int toIndex, int newIndex) {
  mImpl->moveMediaItems(fromIndex, toIndex, newIndex);
}

void LivePlayer::clearMediaItems() {
  mImpl->clearMediaItems();
}

} // namespace coral
