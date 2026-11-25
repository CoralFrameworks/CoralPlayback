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

#include "details/VideoFrameConverter.h"

#include <media/PixelBuffer.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

namespace coral {
namespace {

TEST(VideoFrameConverterTest, ConvertsI420WithoutACopy) {
  auto frame = std::make_shared<I420PixelBuffer>(4, 4, 4, 2, 2);
  std::memset(frame->mutableDataY(), 16, 16);
  std::memset(frame->mutableDataU(), 128, 4);
  std::memset(frame->mutableDataV(), 128, 4);
  frame->timeUs(1234567);

  VideoFrameConverter converter;
  VideoFrameDesc desc;
  ASSERT_TRUE(converter.convert(frame, &desc));
  EXPECT_EQ(desc.width, 4u);
  EXPECT_EQ(desc.height, 4u);
  EXPECT_EQ(desc.format, EffectPixelFormat::YUV420);
  EXPECT_EQ(desc.planeCount, 3u);
  EXPECT_EQ(desc.planes[0].data, frame->dataY());
  EXPECT_EQ(desc.planes[1].data, frame->dataU());
  EXPECT_EQ(desc.planes[2].data, frame->dataV());
  EXPECT_EQ(desc.timestamp.value, 1234567);
  EXPECT_EQ(desc.timestamp.timescale, 1'000'000u);
}

TEST(VideoFrameConverterTest, ConvertsNv12IntoI420Scratch) {
  auto frame = std::make_shared<NV12PixelBuffer>(4, 4, 4, 4);
  std::memset(frame->mutableDataY(), 16, 16);
  std::memset(frame->mutableDataUV(), 128, 8);

  VideoFrameConverter converter;
  VideoFrameDesc desc;
  ASSERT_TRUE(converter.convert(frame, &desc));
  EXPECT_EQ(desc.planeCount, 3u);
  ASSERT_NE(desc.planes[0].data, nullptr);
  ASSERT_NE(desc.planes[1].data, nullptr);
  ASSERT_NE(desc.planes[2].data, nullptr);
  EXPECT_EQ(desc.planes[0].rowBytes, 4u);
}

TEST(VideoFrameConverterTest, CropsNv12ToTheDisplaySize) {
  // A 1080-wide stream decodes into 1088 columns. The converter must report the container's 1080 as the
  // picture width while keeping the coded row pitch for addressing, or the padding becomes a green strip.
  auto frame = std::make_shared<NV12PixelBuffer>(16, 4, 16, 16);
  std::memset(frame->mutableDataY(), 16, 16 * 4);
  std::memset(frame->mutableDataUV(), 128, 16 * 2);

  VideoFrameConverter converter;
  VideoFrameDesc desc;
  ASSERT_TRUE(converter.convert(frame, &desc, 12, 4));
  EXPECT_EQ(desc.width, 12u) << "the visible width, not the coded 16";
  EXPECT_EQ(desc.height, 4u);
  EXPECT_EQ(desc.planes[0].rowBytes, 12u) << "the scratch I420 is allocated at the visible width";
  EXPECT_EQ(desc.planes[1].rowBytes, 6u);
}

TEST(VideoFrameConverterTest, CropsI420ToTheDisplaySizeWithoutCopying) {
  auto frame = std::make_shared<I420PixelBuffer>(16, 4, 16, 8, 8);
  std::memset(frame->mutableDataY(), 16, 16 * 4);
  std::memset(frame->mutableDataU(), 128, 8 * 2);
  std::memset(frame->mutableDataV(), 128, 8 * 2);

  VideoFrameConverter converter;
  VideoFrameDesc desc;
  ASSERT_TRUE(converter.convert(frame, &desc, 12, 4));
  EXPECT_EQ(desc.width, 12u);
  EXPECT_EQ(desc.height, 4u);
  EXPECT_EQ(desc.planes[0].data, frame->dataY()) << "I420 is passed through, not converted";
  EXPECT_EQ(desc.planes[0].rowBytes, 16u) << "the coded stride still addresses the rows";
}

TEST(VideoFrameConverterTest, RejectsNullAndEmptyBuffers) {
  VideoFrameConverter converter;
  VideoFrameDesc desc;
  EXPECT_FALSE(converter.convert(nullptr, &desc));
  EXPECT_FALSE(converter.convert(nullptr, nullptr));
}

} // namespace
} // namespace coral
