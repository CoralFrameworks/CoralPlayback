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

#ifndef CORAL_PLAYBACK_WIN32_WINDOWS_THUMBNAIL_PROVIDER_H
#define CORAL_PLAYBACK_WIN32_WINDOWS_THUMBNAIL_PROVIDER_H

#if defined(_WIN32)

#include <coral/playback/Thumbnail.h>

#include <cstdint>
#include <memory>
#include <string>

namespace coral {

/**
 * A thumbnail provider backed by the Windows Media Foundation source reader.
 *
 * <p>The source reader decodes a video frame as RGB32 on a worker thread; the provider converts it
 * into a CPU BGRA PixelBuffer. Requests are coalesced and cancelled like the Apple provider's, and
 * the result can be installed directly or wrapped by {@code CachingThumbnailProvider}.
 */
class WindowsThumbnailProvider final : public ThumbnailProvider {
public:
  static std::shared_ptr<WindowsThumbnailProvider> create(
      std::string url, uint32_t maxWidth = 320, uint32_t maxHeight = 180);

  ~WindowsThumbnailProvider() override;

  WindowsThumbnailProvider(const WindowsThumbnailProvider&) = delete;
  WindowsThumbnailProvider& operator=(const WindowsThumbnailProvider&) = delete;

  void requestThumbnail(int64_t positionUs, Callback callback) override;

  void cancel() override;

private:
  WindowsThumbnailProvider(std::string url, uint32_t maxWidth, uint32_t maxHeight);

  struct State;
  std::shared_ptr<State> mState;
};

} // namespace coral

#endif // defined(_WIN32)

#endif // CORAL_PLAYBACK_WIN32_WINDOWS_THUMBNAIL_PROVIDER_H
