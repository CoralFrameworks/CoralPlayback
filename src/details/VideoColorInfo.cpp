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

#include <coral/playback/VideoColorInfo.h>

namespace coral {
namespace {

VideoColorPrimaries toPrimaries(const ColorInfo::SpaceType space) noexcept {
  switch (space) {
    case ColorInfo::SpaceType::kSpaceBT709:
      return VideoColorPrimaries::BT709;
    case ColorInfo::SpaceType::kSpaceBT601:
      return VideoColorPrimaries::BT601;
    case ColorInfo::SpaceType::kSpaceBT2020:
      return VideoColorPrimaries::BT2020;
    default:
      return VideoColorPrimaries::Unknown;
  }
}

VideoColorTransfer toTransfer(const ColorInfo::TransferType transfer) noexcept {
  switch (transfer) {
    case ColorInfo::TransferType::kTransferSDR:
      return VideoColorTransfer::SDR;
    case ColorInfo::TransferType::kTransferLinear:
      return VideoColorTransfer::Linear;
    case ColorInfo::TransferType::kTransferST2084:
      return VideoColorTransfer::PQ;
    case ColorInfo::TransferType::kTransferHLG:
      return VideoColorTransfer::HLG;
    case ColorInfo::TransferType::kTransferSRGB:
      return VideoColorTransfer::SRGB;
    case ColorInfo::TransferType::kTransferGamma22:
      return VideoColorTransfer::Gamma22;
    default:
      return VideoColorTransfer::Unknown;
  }
}

VideoColorMatrix toMatrix(const ColorInfo::SpaceType space) noexcept {
  switch (space) {
    case ColorInfo::SpaceType::kSpaceBT709:
      return VideoColorMatrix::BT709;
    case ColorInfo::SpaceType::kSpaceBT601:
      return VideoColorMatrix::BT601;
    case ColorInfo::SpaceType::kSpaceBT2020:
      return VideoColorMatrix::BT2020;
    default:
      return VideoColorMatrix::Unknown;
  }
}

VideoColorRange toRange(const ColorInfo::RangeType range) noexcept {
  switch (range) {
    case ColorInfo::RangeType::kRangeFull:
      return VideoColorRange::Full;
    case ColorInfo::RangeType::kRangeLimited:
      return VideoColorRange::Limited;
    default:
      return VideoColorRange::Unknown;
  }
}

uint16_t readU16(const std::vector<uint8_t>& bytes, const size_t offset) noexcept {
  if (bytes.size() < offset + 2) {
    return 0;
  }
  return static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

} // namespace

VideoColorInfo VideoColorInfo::fromMediaColorInfo(const ColorInfo& color) {
  VideoColorInfo info;
  info.primaries = toPrimaries(color.colorSpace);
  info.transfer = toTransfer(color.colorTransfer);
  info.matrix = toMatrix(color.colorSpace);
  info.range = toRange(color.colorRange);
  info.hdrStatic.raw = color.hdrStaticInfo;
  // The MP4 muxer stores maxCLL and maxFALL in the last four bytes of the CTA-861.3 payload.
  if (color.hdrStaticInfo.size() >= 25) {
    info.hdrStatic.maxContentLightLevel = readU16(color.hdrStaticInfo, 21);
    info.hdrStatic.maxFrameAverageLightLevel = readU16(color.hdrStaticInfo, 23);
  }
  return info;
}

} // namespace coral
