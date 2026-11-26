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

#ifndef CORAL_PLAYBACK_VIDEO_COLOR_INFO_H
#define CORAL_PLAYBACK_VIDEO_COLOR_INFO_H

#include <media/ColorInfo.h>

#include <cstdint>
#include <vector>

namespace coral {

/** The colour primaries of the video. */
enum class VideoColorPrimaries : uint8_t {
  Unknown = 0,
  BT709,
  BT601,
  BT2020,
  DisplayP3,
};

/** The transfer function (EOTF/OETF) of the video. */
enum class VideoColorTransfer : uint8_t {
  Unknown = 0,
  SDR,
  Linear,
  PQ,
  HLG,
  SRGB,
  Gamma22,
};

/** The YCbCr matrix of the video. */
enum class VideoColorMatrix : uint8_t {
  Unknown = 0,
  BT709,
  BT601,
  BT2020,
};

/** The sample range of the video. */
enum class VideoColorRange : uint8_t {
  Unknown = 0,
  Limited,
  Full,
};

/** The static HDR metadata of the video (CTA-861.3 / MP4 mdcv+clli). */
struct HdrStaticMetadata {
  /** Maximum content light level in nits, or 0 when unknown. */
  uint16_t maxContentLightLevel = 0;

  /** Maximum frame-average light level in nits, or 0 when unknown. */
  uint16_t maxFrameAverageLightLevel = 0;

  /** The raw CTA-861.3 payload; empty when the container carried none. */
  std::vector<uint8_t> raw;
};

/**
 * The colour description of the decoded video.
 *
 * <p>This is the player-level contract between the decoder, the effect graph and the UI. It does
 * not imply that the current pipeline outputs HDR: an SDR display path still tone-maps or lets the
 * platform convert. It gives the UI enough information to choose an HDR/EDR path or to label the
 * content, and it is carried on {@link VideoFrameOutput}.
 */
struct VideoColorInfo {
  VideoColorPrimaries primaries = VideoColorPrimaries::Unknown;
  VideoColorTransfer transfer = VideoColorTransfer::Unknown;
  VideoColorMatrix matrix = VideoColorMatrix::Unknown;
  VideoColorRange range = VideoColorRange::Unknown;
  HdrStaticMetadata hdrStatic;

  /** @return true for PQ or HLG content. */
  [[nodiscard]] bool isHdr() const noexcept {
    return transfer == VideoColorTransfer::PQ || transfer == VideoColorTransfer::HLG;
  }

  /** Converts the media module's colour description. */
  [[nodiscard]] static VideoColorInfo fromMediaColorInfo(const ColorInfo& color);
};

} // namespace coral

#endif // CORAL_PLAYBACK_VIDEO_COLOR_INFO_H
