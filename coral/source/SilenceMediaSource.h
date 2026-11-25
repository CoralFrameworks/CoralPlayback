/*
 * Copyright (C) 2026 The Coral Project
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
#ifndef CORAL_PLAYBACK_SOURCE_SILENCE_MEDIA_SOURCE_H
#define CORAL_PLAYBACK_SOURCE_SILENCE_MEDIA_SOURCE_H

#include <coral/source/BaseMediaSource.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * A source of silence of a known duration.
 *
 * <p>This is the counterpart of ExoPlayer's {@code SilenceMediaSource}. Unlike every other source it
 * has nothing to load and nothing to prepare: its timeline is known from the moment it is created, so
 * {@link #getInitialTimeline()} returns it and a player can use the source to pad a playlist without
 * waiting for a media period. Its single period reads silence from {@link SilenceMediaPeriod}.
 */
class SilenceMediaSource final : public BaseMediaSource {
public:
  /**
   * @param durationUs The duration of the silence, in microseconds. Must not be negative.
   * @param mediaItem The media item the source reports, or null. ExoPlayer pads a playlist with a
   *                  source that has no item.
   */
  explicit SilenceMediaSource(int64_t durationUs,
      std::shared_ptr<MediaItem> mediaItem = nullptr) noexcept;

  ~SilenceMediaSource() override = default;

  SilenceMediaSource(const SilenceMediaSource& other) = delete;
  SilenceMediaSource& operator=(const SilenceMediaSource& other) = delete;

  [[nodiscard]] const std::shared_ptr<MediaItem>& getMediaItem() const override {
    return mediaItem;
  }

  /**
   * Returns the timeline, which is known up front: the source never has to be prepared to know how
   * long it is or that it can be seeked.
   */
  [[nodiscard]] const std::shared_ptr<Timeline>& getInitialTimeline() const override;

  /**
   * Returns false, like ExoPlayer: a silence source is never prepared lazily, so a playlist that
   * holds one keeps the real timeline of its windows instead of a masked placeholder.
   */
  [[nodiscard]] bool isSingleWindow() override { return false; }

  std::shared_ptr<MediaPeriod>
  createPeriod(MediaPeriodId id, const std::shared_ptr<Allocator>& allocator,
      int64_t startPositionUs) override;

  void releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) override;

protected:
  void prepareSourceInternal() override;

  void releaseSourceInternal() override;

private:
  int64_t durationUs;
  std::shared_ptr<MediaItem> mediaItem;
  std::shared_ptr<Timeline> timeline;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_SILENCE_MEDIA_SOURCE_H
