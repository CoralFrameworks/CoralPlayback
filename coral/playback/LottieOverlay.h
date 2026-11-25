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

#ifndef CORAL_PLAYBACK_LOTTIE_OVERLAY_H
#define CORAL_PLAYBACK_LOTTIE_OVERLAY_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace coral {

/** A reference point on both the output frame and the overlay for anchored placement. */
enum class LottieAnchor : uint8_t {
  TopLeft,
  TopCenter,
  TopRight,
  CenterLeft,
  Center,
  CenterRight,
  BottomLeft,
  BottomCenter,
  BottomRight,
};

/** How a Lottie overlay's position is interpreted. */
enum class LottiePositionMode : uint8_t {
  /** `centerX`/`centerY` are normalised to the output frame. */
  NormalizedCenter,
  /** `xPx`/`yPx` are the overlay's top-left in output pixels. */
  TopLeftPixels,
  /** `xPx`/`yPx` are the overlay's centre in output pixels. */
  CenterPixels,
  /** `anchor` plus inward `marginX`/`marginY`; keeps the overlay inside the frame. */
  Anchored,
};

/**
 * The layout and playback settings of one Lottie overlay.
 *
 * <p>The JSON is supplied by the application; the player does not depend on Android assets or a
 * platform file API. The animation is pre-rendered at its composition size and placed in the
 * output frame.
 *
 * <p>Size precedence:
 * <ol>
 *   <li>`widthPx` and `heightPx` both set: exact pixel size;</li>
 *   <li>one set: the other follows the composition aspect ratio;</li>
 *   <li>neither set: `scale` is the height as a fraction of the output height.</li>
 * </ol>
 */
struct LottieOverlayConfig {
  std::string name;
  /** The JSON document. */
  std::string json;
  /** Normalised centre in the output frame, used by `NormalizedCenter`. */
  float centerX = 0.5f;
  float centerY = 0.5f;
  /** Overlay height as a fraction of the output height when no pixel size is set. */
  float scale = 0.25f;
  /** Fixed overlay size in output pixels; 0 means derive from the other dimension or `scale`. */
  float widthPx = 0.0f;
  float heightPx = 0.0f;
  /** Position in output pixels for the pixel position modes. */
  float xPx = 0.0f;
  float yPx = 0.0f;
  LottiePositionMode positionMode = LottiePositionMode::NormalizedCenter;
  /** The edge/corner used by `Anchored`; both overlay and output use this point. */
  LottieAnchor anchor = LottieAnchor::TopLeft;
  /** Inward margin in output pixels for `Anchored`. */
  float marginX = 0.0f;
  float marginY = 0.0f;
  float alpha = 1.0f;
  bool loop = true;
  float speed = 1.0f;
  int64_t startTimeUs = 0;
  /** Raster size relative to the Lottie composition. */
  float renderScale = 1.0f;
  /** Higher values are composited later. */
  int zOrder = 0;
  /** Short-loop pre-render limits. */
  uint32_t maxPreRenderFrames = 120;
  size_t maxPreRenderBytes = 64u * 1024u * 1024u;
};

} // namespace coral

#endif // CORAL_PLAYBACK_LOTTIE_OVERLAY_H
