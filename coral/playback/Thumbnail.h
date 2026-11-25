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

#ifndef CORAL_PLAYBACK_THUMBNAIL_H
#define CORAL_PLAYBACK_THUMBNAIL_H

#include <media/PixelBufferHandle.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace coral {

/** One decoded thumbnail frame at a media position. */
struct Thumbnail {
  /** The media position the thumbnail was decoded at, in microseconds. */
  int64_t positionUs = 0;

  /**
   * The decoded frame, or null when the provider could not produce one.
   *
   * <p>The handle is platform-neutral: a macOS provider exposes the IOSurface, a Windows provider a
   * D3D11 texture, and a CPU provider locked planes.
   */
  std::shared_ptr<PixelBuffer> frame;
};

/**
 * Produces thumbnail frames for a seek preview.
 *
 * <p>A player application can install a provider backed by its own decoder or platform thumbnail
 * service; the player only owns the lifetime and the cancellation of the requests. The provider's
 * callback may run on any thread chosen by the provider.
 */
class ThumbnailProvider {
public:
  using Callback = std::function<void(Thumbnail)>;

  virtual ~ThumbnailProvider() = default;

  /** Requests one thumbnail; a provider may coalesce or cancel earlier requests. */
  virtual void requestThumbnail(int64_t positionUs, Callback callback) = 0;

  /** Cancels outstanding requests; called when the player is released. */
  virtual void cancel() {}
};

} // namespace coral

#endif // CORAL_PLAYBACK_THUMBNAIL_H
