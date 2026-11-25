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

#include <coral/playback/ChapterThumbnailProvider.h>

#include <media/PixelBuffer.h>

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <vector>

namespace coral {
namespace {

std::shared_ptr<PixelBuffer> makeFrame() {
  return PixelBuffer::fromAVPixelBuffer(std::make_shared<I420PixelBuffer>(4, 4));
}

class FakeThumbnailProvider final : public ThumbnailProvider {
public:
  void requestThumbnail(const int64_t positionUs, Callback callback) override {
    std::vector<Callback> immediate;
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      mPositions.push_back(positionUs);
      if (mAsync) {
        mCallbacks.push_back(std::move(callback));
      } else {
        immediate.push_back(std::move(callback));
      }
    }
    for (auto& item : immediate) {
      Thumbnail thumbnail;
      thumbnail.positionUs = positionUs;
      thumbnail.frame = makeFrame();
      item(std::move(thumbnail));
    }
  }

  void completeAll() {
    std::vector<Callback> callbacks;
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      callbacks = std::move(mCallbacks);
      mCallbacks.clear();
    }
    for (auto& callback : callbacks) {
      Thumbnail thumbnail;
      thumbnail.frame = makeFrame();
      if (callback) {
        callback(std::move(thumbnail));
      }
    }
  }

  void cancel() override {
    const std::lock_guard<std::mutex> lock(mMutex);
    ++mCancels;
  }

  bool mAsync = false;

  int cancels() const {
    const std::lock_guard<std::mutex> lock(mMutex);
    return mCancels;
  }

private:
  mutable std::mutex mMutex;
  std::vector<int64_t> mPositions;
  std::vector<Callback> mCallbacks;
  int mCancels = 0;
};

TEST(ChapterThumbnailProviderTest, RequestsEveryChapterInOrder) {
  auto source = std::make_shared<FakeThumbnailProvider>();
  std::vector<Chapter> chapters;
  chapters.push_back(Chapter{0, 2'000'000, "Intro"});
  chapters.push_back(Chapter{2'000'000, 5'000'000, "Main"});
  auto provider = ChapterThumbnailProvider::create(source, chapters);
  ASSERT_NE(provider, nullptr);

  std::vector<ChapterThumbnail> result;
  provider->requestThumbnails([&](std::vector<ChapterThumbnail> thumbnails) {
    result = std::move(thumbnails);
  });
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].chapter.title, "Intro");
  EXPECT_EQ(result[1].chapter.title, "Main");
  ASSERT_NE(result[0].thumbnail.frame, nullptr);
  ASSERT_NE(result[1].thumbnail.frame, nullptr);
}

TEST(ChapterThumbnailProviderTest, CancelDropsAnAsyncBatch) {
  auto source = std::make_shared<FakeThumbnailProvider>();
  source->mAsync = true;
  auto provider = ChapterThumbnailProvider::create(
      source, std::vector<Chapter>{Chapter{0, 1'000'000, "A"}, Chapter{1'000'000, 2'000'000, "B"}});
  ASSERT_NE(provider, nullptr);
  int calls = 0;
  provider->requestThumbnails([&](std::vector<ChapterThumbnail>) { ++calls; });
  provider->cancel();
  EXPECT_GE(source->cancels(), 1);
  source->completeAll();
  EXPECT_EQ(calls, 0);
}

} // namespace
} // namespace coral
