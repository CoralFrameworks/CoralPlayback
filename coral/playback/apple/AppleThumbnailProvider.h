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

#ifndef CORAL_PLAYBACK_APPLE_APPLE_THUMBNAIL_PROVIDER_H
#define CORAL_PLAYBACK_APPLE_APPLE_THUMBNAIL_PROVIDER_H

#if defined(__APPLE__)

#include <coral/playback/Thumbnail.h>

#include <cstdint>
#include <memory>
#include <string>

namespace coral {

/**
 * A thumbnail provider backed by AVFoundation's AVAssetImageGenerator.
 *
 * <p>Requests are serialized on one worker thread and coalesced: a new request replaces a request
 * that has not started yet, and an old callback is not invoked after the request was superseded or
 * cancelled. The returned frame is a BGRA IOSurface-backed PixelBuffer, ready for a UI device to
 * import.
 */
class AppleThumbnailProvider final : public ThumbnailProvider {
public:
  static std::shared_ptr<AppleThumbnailProvider> create(
      std::string url, uint32_t maxWidth = 320, uint32_t maxHeight = 180);

  ~AppleThumbnailProvider() override;

  AppleThumbnailProvider(const AppleThumbnailProvider&) = delete;
  AppleThumbnailProvider& operator=(const AppleThumbnailProvider&) = delete;

  void requestThumbnail(int64_t positionUs, Callback callback) override;

  void cancel() override;

private:
  AppleThumbnailProvider(std::string url, uint32_t maxWidth, uint32_t maxHeight);

  struct State;
  std::shared_ptr<State> mState;
};

} // namespace coral

#endif // defined(__APPLE__)

#endif // CORAL_PLAYBACK_APPLE_APPLE_THUMBNAIL_PROVIDER_H
