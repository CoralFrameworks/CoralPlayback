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

#ifndef CORAL_PLAYBACK_DEFAULT_MEDIA_CLOCK_H
#define CORAL_PLAYBACK_DEFAULT_MEDIA_CLOCK_H

#include <coral/playback/MediaClock.h>
#include <coral/playback/Renderer.h>
#include <coral/playback/StandaloneClock.h>

#include <memory>

namespace coral {

/**
 * The default implementation of the media clock
 */
class DefaultMediaClock final : public MediaClock {
public:
  class PlaybackParametersListener {
  public:
    virtual ~PlaybackParametersListener() = default;
    /**
     * Called when the active playback parameters changed. Will not be called for {@link
     * #setPlaybackParameters(PlaybackParameters)}.
     *
     * @param newPlaybackParameters The newly active playback parameters.
     */
    virtual void onPlaybackParametersChanged(const Parameters& newPlaybackParameters) = 0;
  };

  DefaultMediaClock(const DefaultMediaClock&) = delete;
  DefaultMediaClock& operator=(const DefaultMediaClock&) = delete;

  explicit DefaultMediaClock(PlaybackParametersListener* listener) noexcept;

  ~DefaultMediaClock() override;

  void start();

  void stop();

  void resetPosition(int64_t positionUs);

  void onRendererEnabled(const std::shared_ptr<Renderer>& renderer);

  void onRendererDisabled(const std::shared_ptr<Renderer>& renderer);

  int64_t syncAndGetPositionUs(bool isReadingAhead);

  int64_t getPositionUs() override;

  void setPlaybackParameters(const Parameters& parameters) override;

  [[nodiscard]] const Parameters& getPlaybackParameters() const override;

private:
  void syncClocks(bool isReadingAhead);

  bool shouldUseStandaloneClock(bool isReadingAhead);

  StandaloneClock standaloneClock;
  PlaybackParametersListener* listener;
  std::weak_ptr<Renderer> rendererClockSource;
  MediaClock* rendererClock;
  bool isUsingStandaloneClock;
  bool standaloneClockStarted;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DEFAULT_MEDIA_CLOCK_H
