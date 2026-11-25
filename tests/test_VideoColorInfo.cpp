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

#include <gtest/gtest.h>

namespace coral {
namespace {

TEST(VideoColorInfoTest, MapsSdrBt709) {
  const ColorInfo media(ColorInfo::SpaceType::kSpaceBT709,
                        ColorInfo::RangeType::kRangeLimited,
                        ColorInfo::TransferType::kTransferSDR);
  const auto info = VideoColorInfo::fromMediaColorInfo(media);
  EXPECT_EQ(info.primaries, VideoColorPrimaries::BT709);
  EXPECT_EQ(info.matrix, VideoColorMatrix::BT709);
  EXPECT_EQ(info.transfer, VideoColorTransfer::SDR);
  EXPECT_EQ(info.range, VideoColorRange::Limited);
  EXPECT_FALSE(info.isHdr());
}

TEST(VideoColorInfoTest, MapsHdr10AndStaticMetadata) {
  ColorInfo media(ColorInfo::SpaceType::kSpaceBT2020,
                  ColorInfo::RangeType::kRangeLimited,
                  ColorInfo::TransferType::kTransferST2084);
  media.hdrStaticInfo.assign(25, 0);
  media.hdrStaticInfo[21] = 0x03;
  media.hdrStaticInfo[22] = 0xE8; // 1000 nits
  media.hdrStaticInfo[23] = 0x01;
  media.hdrStaticInfo[24] = 0xF4; // 500 nits
  const auto info = VideoColorInfo::fromMediaColorInfo(media);
  EXPECT_EQ(info.primaries, VideoColorPrimaries::BT2020);
  EXPECT_EQ(info.matrix, VideoColorMatrix::BT2020);
  EXPECT_EQ(info.transfer, VideoColorTransfer::PQ);
  EXPECT_TRUE(info.isHdr());
  EXPECT_EQ(info.hdrStatic.maxContentLightLevel, 1000);
  EXPECT_EQ(info.hdrStatic.maxFrameAverageLightLevel, 500);
  EXPECT_EQ(info.hdrStatic.raw.size(), 25u);
}

TEST(VideoColorInfoTest, MapsHlg) {
  ColorInfo media(ColorInfo::SpaceType::kSpaceBT2020,
                  ColorInfo::RangeType::kRangeFull,
                  ColorInfo::TransferType::kTransferHLG);
  const auto info = VideoColorInfo::fromMediaColorInfo(media);
  EXPECT_EQ(info.transfer, VideoColorTransfer::HLG);
  EXPECT_EQ(info.range, VideoColorRange::Full);
  EXPECT_TRUE(info.isHdr());
}

} // namespace
} // namespace coral
