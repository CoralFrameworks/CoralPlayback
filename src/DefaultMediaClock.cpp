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

#include <coral/playback/DefaultMediaClock.h>

namespace coral {

DefaultMediaClock::DefaultMediaClock(PlaybackParametersListener* listener) noexcept
    : listener(listener),
      rendererClock(nullptr),
      isUsingStandaloneClock(true),
      standaloneClockStarted(false) {}

DefaultMediaClock::~DefaultMediaClock() {
  listener = nullptr;
  rendererClock = nullptr;
  rendererClockSource.reset();
}

void DefaultMediaClock::start() {
  standaloneClockStarted = true;
  standaloneClock.start();
}

void DefaultMediaClock::stop() {
  standaloneClockStarted = false;
  standaloneClock.stop();
}

void DefaultMediaClock::resetPosition(int64_t positionUs) {
  standaloneClock.resetPosition(positionUs);
}

void DefaultMediaClock::onRendererEnabled(const std::shared_ptr<Renderer>& renderer) {
  auto rendererSyncClock = renderer->getMediaClock();
  if (rendererSyncClock != nullptr && rendererSyncClock != rendererClock) {
    rendererClock = rendererSyncClock;
    rendererClockSource = renderer;
    rendererClock->setPlaybackParameters(standaloneClock.getPlaybackParameters());
  }
}

void DefaultMediaClock::onRendererDisabled(const std::shared_ptr<Renderer>& renderer) {
  if (rendererClock != nullptr && renderer == rendererClockSource.lock()) {
    rendererClock = nullptr;
    rendererClockSource.reset();
    isUsingStandaloneClock = true;
  }
}

int64_t DefaultMediaClock::syncAndGetPositionUs(bool isReadingAhead) {
  syncClocks(isReadingAhead);
  return getPositionUs();
}

int64_t DefaultMediaClock::getPositionUs() {
  if (isUsingStandaloneClock) {
    return standaloneClock.getPositionUs();
  }
  return rendererClock->getPositionUs();
}

void DefaultMediaClock::setPlaybackParameters(const Parameters& parameters) {
  if (rendererClock != nullptr) {
    rendererClock->setPlaybackParameters(parameters);
    standaloneClock.setPlaybackParameters(rendererClock->getPlaybackParameters());
  } else {
    standaloneClock.setPlaybackParameters(parameters);
  }
}

const Parameters& DefaultMediaClock::getPlaybackParameters() const {
  return rendererClock != nullptr ? rendererClock->getPlaybackParameters()
                                  : standaloneClock.getPlaybackParameters();
}

void DefaultMediaClock::syncClocks(bool isReadingAhead) {
  if (shouldUseStandaloneClock(isReadingAhead)) {
    if (!isUsingStandaloneClock) {
      isUsingStandaloneClock = true;
      if (standaloneClockStarted) {
        standaloneClock.start();
      }
    }
    return;
  }
  // We are either already using the renderer clock or switching from the
  // standalone to the renderer clock, so it must be non-null.
  auto rendererClockPositionUs = rendererClock->getPositionUs();
  if (isUsingStandaloneClock) {
    // Ensure enabling the renderer clock doesn't jump backwards in time.
    if (rendererClockPositionUs < standaloneClock.getPositionUs()) {
      return;
    }
    isUsingStandaloneClock = false;
    // The renderer clock takes over, so the stand-alone clock is no longer needed. It used to be
    // started here instead of stopped, which left it running while unused.
    standaloneClock.stop();
  }
  // Continuously sync stand-alone clock to renderer clock so that it can take
  // over if needed.
  standaloneClock.resetPosition(rendererClockPositionUs);
  auto playbackParameters = rendererClock->getPlaybackParameters();
  if (playbackParameters != standaloneClock.getPlaybackParameters()) {
    standaloneClock.setPlaybackParameters(playbackParameters);
    if (listener) {
      listener->onPlaybackParametersChanged(playbackParameters);
    }
  }
}

bool DefaultMediaClock::shouldUseStandaloneClock(bool isReadingAhead) {
  return rendererClockSource.lock() == nullptr ||
         rendererClockSource.lock()->isEnded() ||
         (
             !rendererClockSource.lock()->isReady() &&
             (isReadingAhead || rendererClockSource.lock()->hasReadStreamToEnd()));
}

} // namespace coral
