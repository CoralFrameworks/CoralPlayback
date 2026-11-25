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

#include <coral/source/DefaultProgressiveMediaExtractor.h>
#include <coral/source/MediaItem.h>
#include <coral/source/ProgressiveMediaPeriod.h>
#include <coral/source/ProgressiveMediaSource.h>
#include <coral/upstream/DefaultAllocator.h>

#include <datasource/DataSource.h>
#include <extractor/DefaultExtractorInput.h>

#include <media/Types.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace coral {
namespace {

using namespace std::chrono_literals;

/** The identifier and the duration that the fakes declare. */
constexpr int kTrackId = 7;
constexpr int64_t kDurationUs = 5000000;

constexpr auto kTimeout = 10000ms;

/**
 * Counts what the fakes were asked to do, so that a test can assert which of them was selected and
 * which of them was left alone.
 */
struct Counters {
  /** The number of times the extractor of the module was asked to recognize the container. */
  int builtinSniffCount = 0;
  /** The number of times the extractor of the module was initialized, i.e. it was selected. */
  int builtinInitCount = 0;
  /** The number of times the fallback was given the output of the period. */
  int fallbackInitCount = 0;
  /** The number of times the fallback was asked to recognize the container. */
  int fallbackSniffCount = 0;
  int fallbackReadCount = 0;
  int fallbackSeekCount = 0;
  int fallbackReleaseCount = 0;
};

/**
 * An extractor of the module: it reports the confidence it was built with, and it declares a track,
 * the end of the tracks and a seek map while it is initialized, the way the built-in extractors do.
 */
class FakeExtractor final : public AVExtractor {
public:
  FakeExtractor(Counters& counters, const int confidence) noexcept
      : counters_(counters),
        confidence_(confidence) {
  }

  int sniff(AVExtractorInput& input) override {
    static_cast<void>(input);
    ++counters_.builtinSniffCount;
    return confidence_;
  }

  void init(AVExtractorOutput& output) override {
    ++counters_.builtinInitCount;
    output.track(kTrackId, MediaType::kMediaTypeAudio, AVTrackOutput::emptyConsumer());
    output.endTracks();
    output.seekMap(std::make_shared<AVUnseekableSeekMap>(kDurationUs));
  }

  AVExtractor::Result read(AVExtractorInput& input) override {
    static_cast<void>(input);
    return AVExtractor::kResultEndOfInput;
  }

  [[nodiscard]] std::string getName() const override { return "FakeExtractor"; }

private:
  Counters& counters_;
  int confidence_;
};

/**
 * The factory of the extractors of the module, i.e. of the built-in ones: it creates a single
 * extractor, which recognizes the container with the confidence the factory was built with.
 */
class FakeExtractorsFactory final : public AVExtractorsFactory {
public:
  FakeExtractorsFactory(Counters& counters, const int confidence) noexcept
      : counters_(counters),
        confidence_(confidence) {
  }

  std::vector<std::unique_ptr<AVExtractor>> createExtractors() override {
    std::vector<std::unique_ptr<AVExtractor>> extractors;
    extractors.push_back(std::make_unique<FakeExtractor>(counters_, confidence_));
    return extractors;
  }

private:
  Counters& counters_;
  int confidence_;
};

/**
 * The fallback of the extractor, which behaves the way the extractor of the platform does: it opens
 * the media itself, so it declares its tracks while it is sniffed rather than while it is
 * initialized, and it has no extractor of the module to report as its chosen extractor.
 */
class FakeFallbackExtractor final : public ProgressiveMediaExtractor {
public:
  FakeFallbackExtractor(Counters& counters, const int confidence) noexcept
      : counters_(counters),
        confidence_(confidence) {
  }

  void init(AVExtractorOutput& output) override {
    ++counters_.fallbackInitCount;
    output_ = &output;
  }

  int sniff(AVExtractorInput& input) override {
    static_cast<void>(input);
    ++counters_.fallbackSniffCount;
    if (confidence_ <= AVExtractorsFactory::kSniffResultNone || output_ == nullptr) {
      return AVExtractorsFactory::kSniffResultNone;
    }
    recognized_ = true;
    // Opening the media is what tells the extractor of the platform which tracks it holds, so its
    // tracks are declared while it is sniffed.
    auto trackOutput =
        output_->track(kTrackId, MediaType::kMediaTypeAudio, AVTrackOutput::emptyConsumer());
    trackOutput->format(MediaFormat::CreateAudioFormat(MimeTypes::AUDIO_RAW, 8000, 1));
    seekMap_ = std::make_shared<AVUnseekableSeekMap>(kDurationUs);
    output_->endTracks();
    output_->seekMap(seekMap_);
    return confidence_;
  }

  void release() override {
    ++counters_.fallbackReleaseCount;
    recognized_ = false;
    seekMap_.reset();
    output_ = nullptr;
  }

  [[nodiscard]] std::shared_ptr<AVSeekMap> getSeekMap() override { return seekMap_; }

  [[nodiscard]] AVExtractor* getChosenExtractor() const override { return nullptr; }

  [[nodiscard]] bool isContainerRecognized() const override { return recognized_; }

  [[nodiscard]] int getTrackCount() override { return recognized_ ? 1 : 0; }

  bool seek(long position, int64_t timeUs) override {
    static_cast<void>(position);
    static_cast<void>(timeUs);
    ++counters_.fallbackSeekCount;
    return recognized_;
  }

  AVExtractor::Result read(AVExtractorInput& input) override {
    static_cast<void>(input);
    ++counters_.fallbackReadCount;
    return AVExtractor::kResultEndOfInput;
  }

private:
  Counters& counters_;
  int confidence_;
  AVExtractorOutput* output_ = nullptr;
  std::shared_ptr<AVSeekMap> seekMap_;
  bool recognized_ = false;
};

/** Records what an extractor declared to its output. */
class RecordingExtractorOutput final : public AVExtractorOutput {
public:
  std::shared_ptr<AVTrackOutput>
  track(const int id, const MediaType type, AVTrackOutput::SampleConsumer consumer) override {
    static_cast<void>(consumer);
    ++trackCount;
    lastTrackId = id;
    lastTrackType = type;
    auto trackOutput = std::make_shared<AVTrackOutput>(id, nullptr);
    trackOutputs.push_back(trackOutput);
    return trackOutput;
  }

  std::shared_ptr<AVTrackOutput> track(const int id, AVTrackOutput::SampleConsumer consumer) override {
    return track(id, MediaType::kMediaTypeDefault, std::move(consumer));
  }

  void seekMap(std::shared_ptr<AVSeekMap> seekMap) override {
    ++seekMapCount;
    declaredSeekMap = std::move(seekMap);
  }

  void endTracks() override { ++endTracksCount; }

  int trackCount = 0;
  int lastTrackId = Definitions::kIndexUnset;
  MediaType lastTrackType = MediaType::kMediaTypeInvalid;
  int seekMapCount = 0;
  int endTracksCount = 0;
  std::shared_ptr<AVSeekMap> declaredSeekMap;
  std::vector<std::shared_ptr<AVTrackOutput>> trackOutputs;
};

/** The media the tests read: a content that no extractor of the module recognizes. */
std::vector<uint8_t> unrecognizedMedia() { return std::vector<uint8_t>(1024, 0x5A); }


/**
 * Builds the input of the tests. The caller keeps the data source alive, because the input only
 * holds a reference to it.
 */
std::shared_ptr<AVByteArrayDataSource> openMedia() {
  auto dataSource = std::make_shared<AVByteArrayDataSource>(unrecognizedMedia());
  EXPECT_TRUE(dataSource->open("memory"));
  return dataSource;
}

// ==================================================================================================
// The selection of the extractor
// ==================================================================================================

TEST(DefaultProgressiveMediaExtractorTest, UsesTheFallbackWhenNoExtractorOfTheModuleRecognizesTheContainer) {
  Counters counters;
  RecordingExtractorOutput output;
  auto extractor = std::make_shared<DefaultProgressiveMediaExtractor>(
      std::make_unique<FakeExtractorsFactory>(counters, AVExtractorsFactory::kSniffResultNone));
  extractor->setFallback(
      std::make_shared<FakeFallbackExtractor>(counters, AVExtractorsFactory::kSniffResultCertain));

  extractor->init(output);
  // The fallback belongs to the period for its whole life, so it is initialized with the output even
  // before it is selected.
  EXPECT_EQ(counters.fallbackInitCount, 1);

  const auto dataSource = openMedia();
  AVDefaultExtractorInput input(dataSource, 0);

  EXPECT_EQ(extractor->sniff(input), AVExtractorsFactory::kSniffResultCertain);
  EXPECT_EQ(counters.builtinSniffCount, 1) << "the extractors of the module were not asked";
  EXPECT_EQ(counters.fallbackSniffCount, 1) << "the fallback was not asked";
  EXPECT_FALSE(counters.builtinInitCount) << "an extractor that recognized nothing was selected";

  // The fallback demuxes the media with its own infrastructure, so it has no extractor of the module
  // to report, but the container it read was recognized.
  EXPECT_EQ(extractor->getChosenExtractor(), nullptr);
  EXPECT_TRUE(extractor->isContainerRecognized());

  // What the fallback declared reaches the output of the period, through this extractor.
  EXPECT_EQ(output.trackCount, 1);
  EXPECT_EQ(output.lastTrackId, kTrackId);
  EXPECT_EQ(output.lastTrackType, MediaType::kMediaTypeAudio);
  EXPECT_EQ(output.endTracksCount, 1);
  EXPECT_EQ(output.seekMapCount, 1);
  EXPECT_EQ(extractor->getTrackCount(), 1);
  EXPECT_EQ(extractor->getSeekMap(), output.declaredSeekMap);

  // The reads and the seeks are forwarded to the fallback.
  EXPECT_EQ(extractor->read(input), AVExtractor::kResultEndOfInput);
  EXPECT_EQ(counters.fallbackReadCount, 1);
  EXPECT_TRUE(extractor->seek(0, 0));
  EXPECT_EQ(counters.fallbackSeekCount, 1);

  extractor->release();
  EXPECT_EQ(counters.fallbackReleaseCount, 1);
}

TEST(DefaultProgressiveMediaExtractorTest, PrefersTheExtractorOfTheModuleOverTheFallback) {
  Counters counters;
  RecordingExtractorOutput output;
  auto extractor = std::make_shared<DefaultProgressiveMediaExtractor>(
      std::make_unique<FakeExtractorsFactory>(counters, AVExtractorsFactory::kSniffResultPossible));
  extractor->setFallback(
      std::make_shared<FakeFallbackExtractor>(counters, AVExtractorsFactory::kSniffResultCertain));

  extractor->init(output);
  const auto dataSource = openMedia();
  AVDefaultExtractorInput input(dataSource, 0);

  EXPECT_EQ(extractor->sniff(input), AVExtractorsFactory::kSniffResultPossible);
  // The confidence of the fallback is higher, but it is only a fallback: a container of the module is
  // read by the extractor of the module, and the media of the platform is not touched.
  EXPECT_EQ(counters.fallbackSniffCount, 0) << "the fallback was asked to read a container of the module";
  EXPECT_EQ(counters.builtinInitCount, 1);
  EXPECT_NE(extractor->getChosenExtractor(), nullptr);
  EXPECT_TRUE(extractor->isContainerRecognized());
  EXPECT_EQ(extractor->getTrackCount(), 1);
  // The track that was declared is the one of the extractor of the module.
  EXPECT_EQ(output.trackCount, 1);
  EXPECT_EQ(output.lastTrackId, kTrackId);

  extractor->read(input);
  extractor->seek(0, 0);
  EXPECT_EQ(counters.fallbackReadCount, 0);
  EXPECT_EQ(counters.fallbackSeekCount, 0);

  extractor->release();
  // The fallback was initialized, so it is released even though it was never selected.
  EXPECT_EQ(counters.fallbackReleaseCount, 1);
}

TEST(DefaultProgressiveMediaExtractorTest, ReportsAnUnrecognizedContainerWhenTheFallbackRejectsIt) {
  Counters counters;
  RecordingExtractorOutput output;
  auto extractor = std::make_shared<DefaultProgressiveMediaExtractor>(
      std::make_unique<FakeExtractorsFactory>(counters, AVExtractorsFactory::kSniffResultNone));
  extractor->setFallback(
      std::make_shared<FakeFallbackExtractor>(counters, AVExtractorsFactory::kSniffResultNone));

  extractor->init(output);
  const auto dataSource = openMedia();
  AVDefaultExtractorInput input(dataSource, 0);

  EXPECT_EQ(extractor->sniff(input), AVExtractorsFactory::kSniffResultNone);
  EXPECT_EQ(counters.fallbackSniffCount, 1);
  EXPECT_FALSE(extractor->isContainerRecognized());
  EXPECT_EQ(extractor->getChosenExtractor(), nullptr);
  EXPECT_EQ(extractor->getTrackCount(), 0);
  EXPECT_EQ(extractor->getSeekMap(), nullptr);
  EXPECT_EQ(output.trackCount, 0);
  EXPECT_EQ(output.seekMapCount, 0);
}

TEST(DefaultProgressiveMediaExtractorTest, ReportsAnUnrecognizedContainerWithoutAFallback) {
  Counters counters;
  RecordingExtractorOutput output;
  auto extractor = std::make_shared<DefaultProgressiveMediaExtractor>(
      std::make_unique<FakeExtractorsFactory>(counters, AVExtractorsFactory::kSniffResultNone));

  extractor->init(output);
  const auto dataSource = openMedia();
  AVDefaultExtractorInput input(dataSource, 0);

  // Without a fallback, a container that no extractor of the module recognizes stays unrecognized,
  // which is what the caller reports to the application.
  EXPECT_EQ(extractor->sniff(input), AVExtractorsFactory::kSniffResultNone);
  EXPECT_FALSE(extractor->isContainerRecognized());
  EXPECT_EQ(extractor->getChosenExtractor(), nullptr);
  EXPECT_EQ(extractor->read(input), AVExtractor::kResultEndOfInput);
  EXPECT_FALSE(extractor->seek(0, 0));
  EXPECT_EQ(counters.fallbackSniffCount, 0);
  EXPECT_EQ(counters.fallbackReleaseCount, 0);
}

TEST(DefaultProgressiveMediaExtractorTest, SetsAndClearsTheFallbackOfAPeriod) {
  Counters counters;
  RecordingExtractorOutput output;
  auto fallback =
      std::make_shared<FakeFallbackExtractor>(counters, AVExtractorsFactory::kSniffResultCertain);
  auto extractor = std::make_shared<DefaultProgressiveMediaExtractor>(
      std::make_unique<FakeExtractorsFactory>(counters, AVExtractorsFactory::kSniffResultNone));

  extractor->setFallback(fallback);
  // Replacing a fallback that was initialized releases it, and the new one takes its place.
  extractor->init(output);
  EXPECT_EQ(counters.fallbackInitCount, 1);
  EXPECT_EQ(counters.fallbackReleaseCount, 0);

  Counters replacementCounters;
  auto replacement = std::make_shared<FakeFallbackExtractor>(
      replacementCounters, AVExtractorsFactory::kSniffResultCertain);
  extractor->setFallback(replacement);
  EXPECT_EQ(counters.fallbackReleaseCount, 1) << "the replaced fallback was not released";

  const auto dataSource = openMedia();
  AVDefaultExtractorInput input(dataSource, 0);
  EXPECT_EQ(extractor->sniff(input), AVExtractorsFactory::kSniffResultCertain);
  EXPECT_EQ(replacementCounters.fallbackSniffCount, 1);

  // Removing the fallback leaves the extractors of the module alone.
  extractor->setFallback(nullptr);
  EXPECT_EQ(replacementCounters.fallbackReleaseCount, 1);
  extractor->release();
}

// ==================================================================================================
// The period
// ==================================================================================================

/** Reads the media from memory, so that the tests need no file. */
class InMemoryDataSourceFactory {
public:
  explicit InMemoryDataSourceFactory(std::vector<uint8_t> media) noexcept
      : media_(std::move(media)) {
  }

  std::shared_ptr<AVDataSource> operator()(const std::string& url) const {
    static_cast<void>(url);
    return std::make_shared<AVByteArrayDataSource>(media_);
  }

private:
  std::vector<uint8_t> media_;
};

/** Records that the period was prepared. */
class RecordingPeriodCallback final : public MediaPeriod::Callback {
public:
  void onPrepared(const std::shared_ptr<MediaPeriod>& mediaPeriod) override {
    static_cast<void>(mediaPeriod);
    std::lock_guard<std::mutex> lock(mutex_);
    prepared_ = true;
    cv_.notify_all();
  }

  void onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& mediaPeriod) override {
    static_cast<void>(mediaPeriod);
  }

  bool waitForPrepared(const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return prepared_; });
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool prepared_ = false;
};

/** A caller of the source, which only has to accept the timeline it is given. */
class IgnoringSourceCaller final : public MediaSourceCaller {
public:
  void onSourceInfoRefreshed(const std::shared_ptr<MediaSource>& source,
      const std::shared_ptr<Timeline>& timeline) override {
    static_cast<void>(source);
    static_cast<void>(timeline);
  }
};

TEST(DefaultProgressiveMediaExtractorTest, ThePeriodIsPreparedFromTheFallbackOfAContainerOfThePlatform) {
  Counters counters;
  const auto item = MediaItem::fromUrl("memory://fallback.avi");
  // The source is built the way `ProgressiveMediaSource::createDefaultExtractorFactory` builds it,
  // with the extractors of the module replaced by ones that recognize nothing: the fallback is then
  // the only extractor that can read the media, and the period has to prepare from it.
  auto source = std::make_shared<ProgressiveMediaSource>(item,
      InMemoryDataSourceFactory(unrecognizedMedia()),
      [&counters]() -> std::shared_ptr<ProgressiveMediaExtractor> {
        auto extractor = std::make_shared<DefaultProgressiveMediaExtractor>(
            std::make_unique<FakeExtractorsFactory>(counters, AVExtractorsFactory::kSniffResultNone));
        extractor->setFallback(std::make_shared<FakeFallbackExtractor>(
            counters, AVExtractorsFactory::kSniffResultCertain));
        return extractor;
      });

  auto caller = std::make_shared<IgnoringSourceCaller>();
  source->prepareSource(caller);
  source->enable(caller);

  const auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, 256 * 1024);
  auto period = source->createPeriod(MediaPeriodId(PeriodUid("fallback-period")), allocator, 0);
  const auto progressivePeriod = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(period);
  ASSERT_NE(progressivePeriod, nullptr);
  auto callback = std::make_shared<RecordingPeriodCallback>();
  period->prepare(callback.get(), 0);
  ASSERT_TRUE(callback->waitForPrepared(kTimeout))
      << "the period was not prepared from the fallback of the media";

  EXPECT_EQ(counters.builtinSniffCount, 1);
  EXPECT_EQ(counters.fallbackSniffCount, 1);
  EXPECT_FALSE(counters.builtinInitCount);

  // The track and the duration the period reports are the ones the fallback declared: a container of
  // the platform is prepared by the same period as a container of the module, and only the extractor
  // that reads it differs.
  const auto& groups = period->getTrackGroups();
  ASSERT_EQ(groups->length(), 1);
  EXPECT_EQ(groups->get(0)->length(), 1);
  const auto& format = groups->get(0)->getFormat(0);
  ASSERT_NE(format, nullptr);
  EXPECT_TRUE(format->isAudio());
  EXPECT_EQ(format->getMimeType(), MimeTypes::AUDIO_RAW);
  EXPECT_EQ(progressivePeriod->getDurationUs(), kDurationUs);

  source->releasePeriod(period);
  source->releaseSource(caller);
}

} // namespace anonymous
} // namespace coral
