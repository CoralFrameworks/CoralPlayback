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

// The container fixtures are shared with the extractor module, see the CMakeLists of this target.
#include <TestMedia.h>

#include <coral/selection/AdaptiveTrackSelection.h>
#include <coral/selection/FixedTrackSelection.h>
#include <coral/source/MediaItem.h>
#include <coral/source/ProgressiveMediaPeriod.h>
#include <coral/source/ProgressiveMediaSource.h>
#include <coral/upstream/DefaultAllocator.h>
#include <coral/upstream/DefaultBandwidthMeter.h>

#include <datasource/DataSource.h>

#include <media/Types.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace coral {
namespace {

using namespace std::chrono_literals;

constexpr int kSampleRate = 8000;
constexpr int kChannelCount = 1;
/** Five seconds of 16 bit mono samples, i.e. several read chunks of the WAV extractor. */
constexpr int kSampleCount = 40000;
constexpr int64_t kDurationUs = static_cast<int64_t>(kSampleCount) * 1000000 / kSampleRate;
/** The number of bytes of the data chunk of the fixture. */
constexpr int kPayloadSize = kSampleCount * kChannelCount * 2;

constexpr auto kTimeout = 10000ms;

/**
 * Reads the media from memory, so that the tests need no file.
 */
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

/**
 * Records the timeline refreshes of a media source.
 */
class RecordingSourceCaller final : public MediaSourceCaller {
public:
  void onSourceInfoRefreshed(const std::shared_ptr<MediaSource>& source,
      const std::shared_ptr<Timeline>& timeline) override {
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(source);
    ++refreshCount_;
    durationUs_ = Definitions::kTimeUnset;
    isPlaceholder_ = true;
    windowCount_ = 0;
    periodCount_ = 0;
    if (timeline != nullptr) {
      windowCount_ = timeline->getWindowCount();
      periodCount_ = timeline->getPeriodCount();
      if (windowCount_ > 0) {
        const auto window = timeline->getWindow(0, std::make_shared<Timeline::Window>(), 0);
        if (window != nullptr) {
          durationUs_ = window->durationUs;
          isPlaceholder_ = window->isPlaceholder;
        }
      }
    }
    cv_.notify_all();
  }

  bool waitForRefreshCount(const int count, const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this, count] { return refreshCount_ >= count; });
  }

  /**
   * Waits until the timeline reports the given duration, i.e. until the source knows it.
   */
  bool waitForDuration(const int64_t durationUs, const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this, durationUs] {
      return !isPlaceholder_ && durationUs_ == durationUs;
    });
  }

  [[nodiscard]] bool isPlaceholder() {
    std::lock_guard<std::mutex> lock(mutex_);
    return isPlaceholder_;
  }

  [[nodiscard]] int windowCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return windowCount_;
  }

  [[nodiscard]] int periodCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return periodCount_;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int refreshCount_ = 0;
  int windowCount_ = 0;
  int periodCount_ = 0;
  int64_t durationUs_ = Definitions::kTimeUnset;
  bool isPlaceholder_ = true;
};

/**
 * Records the events of a media period.
 */
class RecordingPeriodCallback final : public MediaPeriod::Callback {
public:
  void onPrepared(const std::shared_ptr<MediaPeriod>& mediaPeriod) override {
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(mediaPeriod);
    ++preparedCount_;
    cv_.notify_all();
  }

  void onContinueLoadingRequested(const std::shared_ptr<MediaPeriod>& mediaPeriod) override {
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(mediaPeriod);
    ++continueLoadingRequestCount_;
    cv_.notify_all();
  }

  /**
   * Polls the period the way the player does on its playback thread, until it reports a fatal error.
   *
   * <p>This is what {@code AvLivePlayer::maybeThrowPeriodError} does on every iteration: a period that
   * cannot be prepared throws instead of calling back into the player, so the failure is only visible
   * once the load that discovers it has run.
   *
   * @return Whether the period reported a fatal error before the timeout.
   */
  bool waitForCannotBePrepared(MediaPeriod& mediaPeriod,
      const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      try {
        mediaPeriod.maybeThrowPrepareError();
      } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++errorCount_;
        errorMessage_ = e.what();
        cv_.notify_all();
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }
  bool waitForPrepared(const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return preparedCount_ > 0; });
  }

  bool waitForError(const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return errorCount_ > 0; });
  }

  bool waitForContinueLoadingRequest(const int count, const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(
        lock, timeout, [this, count] { return continueLoadingRequestCount_ >= count; });
  }

  [[nodiscard]] int continueLoadingRequestCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return continueLoadingRequestCount_;
  }

  [[nodiscard]] std::string errorMessage() {
    std::lock_guard<std::mutex> lock(mutex_);
    return errorMessage_;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int preparedCount_ = 0;
  int continueLoadingRequestCount_ = 0;
  int errorCount_ = 0;
  std::string errorMessage_;
};

/**
 * Records the events a media source reports.
 */
class RecordingMediaSourceEventListener final : public MediaSourceEventListener {
public:
  void onLoadStarted(const int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++loadStartedCount_;
    lastWindowIndex_ = windowIndex;
    lastMediaPeriodId_ = mediaPeriodId;
    lastUrl_ = loadEventInfo.url;
    lastMediaStartTimeMs_ = mediaLoadData.mediaStartTimeMs;
    cv_.notify_all();
  }

  void onLoadCompleted(const int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++loadCompletedCount_;
    lastWindowIndex_ = windowIndex;
    lastMediaPeriodId_ = mediaPeriodId;
    lastBytesLoaded_ = loadEventInfo.bytesLoaded;
    cv_.notify_all();
  }

  void onLoadCanceled(const int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++loadCanceledCount_;
    cv_.notify_all();
  }

  void onLoadError(const int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const LoadEventInfo& loadEventInfo,
      const MediaLoadData& mediaLoadData,
      const Error* error,
      const bool wasCanceled) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++loadErrorCount_;
    lastErrorMessage_ = error != nullptr ? error->failureReason : std::string();
    lastErrorWasCanceled_ = wasCanceled;
    cv_.notify_all();
  }

  void onDownstreamFormatChanged(const int windowIndex,
      const std::optional<MediaPeriodId>& mediaPeriodId,
      const MediaLoadData& mediaLoadData) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++downstreamFormatCount_;
    lastWindowIndex_ = windowIndex;
    lastTrackFormat_ = mediaLoadData.trackFormat;
    cv_.notify_all();
  }

  void onUpstreamDiscarded(const int windowIndex,
      const MediaPeriodId& mediaPeriodId,
      const MediaLoadData& mediaLoadData) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++upstreamDiscardedCount_;
    lastWindowIndex_ = windowIndex;
    lastDiscardedMediaPeriodId_ = mediaPeriodId;
    lastDiscardedMediaStartTimeMs_ = mediaLoadData.mediaStartTimeMs;
    lastDiscardedMediaEndTimeMs_ = mediaLoadData.mediaEndTimeMs;
    cv_.notify_all();
  }

  bool waitFor(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, predicate);
  }

  [[nodiscard]] int loadStartedCount() const { return loadStartedCount_.load(); }

  [[nodiscard]] int loadCompletedCount() const { return loadCompletedCount_.load(); }

  [[nodiscard]] int loadCanceledCount() const { return loadCanceledCount_.load(); }

  [[nodiscard]] int loadErrorCount() const { return loadErrorCount_.load(); }

  [[nodiscard]] int downstreamFormatCount() const { return downstreamFormatCount_.load(); }

  [[nodiscard]] int upstreamDiscardedCount() const { return upstreamDiscardedCount_.load(); }

  [[nodiscard]] std::optional<MediaPeriodId> lastDiscardedMediaPeriodId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastDiscardedMediaPeriodId_;
  }

  [[nodiscard]] int64_t lastDiscardedMediaStartTimeMs() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastDiscardedMediaStartTimeMs_;
  }

  [[nodiscard]] int64_t lastDiscardedMediaEndTimeMs() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastDiscardedMediaEndTimeMs_;
  }

  [[nodiscard]] int lastWindowIndex() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastWindowIndex_;
  }

  [[nodiscard]] std::optional<MediaPeriodId> lastMediaPeriodId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastMediaPeriodId_;
  }

  [[nodiscard]] std::string lastUrl() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastUrl_;
  }

  [[nodiscard]] int64_t lastBytesLoaded() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastBytesLoaded_;
  }

  [[nodiscard]] std::string lastErrorMessage() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastErrorMessage_;
  }

  [[nodiscard]] bool lastErrorWasCanceled() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastErrorWasCanceled_;
  }

  [[nodiscard]] std::shared_ptr<MediaFormat> lastTrackFormat() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastTrackFormat_;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<int> loadStartedCount_{ 0 };
  std::atomic<int> loadCompletedCount_{ 0 };
  std::atomic<int> loadCanceledCount_{ 0 };
  std::atomic<int> loadErrorCount_{ 0 };
  std::atomic<int> downstreamFormatCount_{ 0 };
  std::atomic<int> upstreamDiscardedCount_{ 0 };
  int lastWindowIndex_ = Definitions::kIndexUnset;
  std::optional<MediaPeriodId> lastMediaPeriodId_;
  std::string lastUrl_;
  int64_t lastMediaStartTimeMs_ = Definitions::kTimeUnset;
  int64_t lastBytesLoaded_ = 0;
  std::string lastErrorMessage_;
  bool lastErrorWasCanceled_ = false;
  std::shared_ptr<MediaFormat> lastTrackFormat_;
  std::optional<MediaPeriodId> lastDiscardedMediaPeriodId_;
  int64_t lastDiscardedMediaStartTimeMs_ = Definitions::kTimeUnset;
  int64_t lastDiscardedMediaEndTimeMs_ = Definitions::kTimeUnset;
};

/**
 * The summary of a sample of a stream, taken before the buffer is reused by the next read.
 */
struct SampleSummary {
  int64_t timeUs = 0;
  size_t size = 0;
  bool keyFrame = false;
  bool discard = false;
};

/**
 * A source whose single period was created, without starting the preparation of the period.
 */
struct Fixture {
  std::shared_ptr<ProgressiveMediaSource> source;
  std::shared_ptr<RecordingSourceCaller> caller;
  std::shared_ptr<RecordingPeriodCallback> callback;
  std::shared_ptr<MediaPeriod> period;

  ~Fixture() {
    if (source != nullptr && period != nullptr) {
      source->releasePeriod(period);
    }
    period.reset();
    callback.reset();
    if (source != nullptr && caller != nullptr) {
      source->releaseSource(caller);
    }
  }
};

std::unique_ptr<Fixture> createFixture(std::vector<uint8_t> media,
    const std::string& url = "memory://test.wav") {
  auto fixture = std::make_unique<Fixture>();
  const auto item = MediaItem::fromUrl(url);
  fixture->source = std::make_shared<ProgressiveMediaSource>(
      item, InMemoryDataSourceFactory(std::move(media)));
  fixture->caller = std::make_shared<RecordingSourceCaller>();
  fixture->source->prepareSource(fixture->caller);
  fixture->source->enable(fixture->caller);
  const auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, 256 * 1024);
  fixture->period =
      fixture->source->createPeriod(MediaPeriodId(PeriodUid("test-period")), allocator, 0);
  fixture->callback = std::make_shared<RecordingPeriodCallback>();
  return fixture;
}

/**
 * Drives the loading until the load completed, i.e. until the period reports that the whole media
 * is buffered.
 */
void loadToEnd(ProgressiveMediaPeriod& period, RecordingPeriodCallback& callback) {
  int requests = callback.continueLoadingRequestCount();
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (period.getBufferedPositionUs() == Definitions::kTimeEndOfSource) {
      return;
    }
    period.continueLoading(0);
    callback.waitForContinueLoadingRequest(++requests, kTimeout);
  }
}

/**
 * Reads the samples of a stream until the end of the stream was reported.
 */
std::vector<SampleSummary> readAllSamples(SampleStream& stream) {
  std::vector<SampleSummary> samples;
  const auto buffer = std::make_shared<AVPacketBuffer>(MediaType::kMediaTypeAudio);
  for (int guard = 0; guard < 100000; ++guard) {
    FormatHolder formatHolder;
    const auto result = stream.readData(formatHolder, buffer, false);
    if (result == SampleStream::ReadDataResult::kResultBufferRead) {
      if (buffer->isEndOfStream()) {
        break;
      }
      SampleSummary summary;
      summary.timeUs = buffer->timeUs();
      summary.size = buffer->size();
      summary.keyFrame = buffer->isKeyFrame();
      summary.discard = buffer->isDiscard();
      samples.push_back(summary);
      continue;
    }
    if (result == SampleStream::ReadDataResult::kResultNothingRead) {
      // The stream has nothing to deliver yet: the tests drive the load to its end before reading.
      break;
    }
  }
  return samples;
}

/**
 * Selects the only track of the period and returns its stream.
 */
std::shared_ptr<SampleStream> selectOnlyTrack(MediaPeriod& period) {
  const auto& groups = period.getTrackGroups();
  std::vector<std::shared_ptr<TrackSelection>> selections(1);
  if (groups->length() > 0) {
    selections[0] = std::make_shared<FixedTrackSelection>(groups->get(0), 0);
  }
  std::vector<bool> mayRetainStreamFlags(1, false);
  std::vector<std::shared_ptr<SampleStream>> streams(1);
  std::vector<bool> streamResetFlags(1);
  period.selectTracks(selections, mayRetainStreamFlags, streams, streamResetFlags, 0);
  return streams[0];
}

// ==================================================================================================

TEST(ProgressiveMediaSourceTest, ReportsAPlaceholderTimelineThenTheRealDuration) {
  auto fixture = createFixture(test::buildWav(kSampleRate, kChannelCount, kSampleCount));

  // The placeholder timeline is reported as soon as the source is prepared, because the duration of
  // the media is not known before its period parsed its header.
  ASSERT_TRUE(fixture->caller->waitForRefreshCount(1, kTimeout));
  EXPECT_TRUE(fixture->caller->isPlaceholder());
  EXPECT_EQ(fixture->caller->windowCount(), 1);
  EXPECT_EQ(fixture->caller->periodCount(), 1);

  const auto period = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(fixture->period);
  ASSERT_NE(period, nullptr);
  fixture->period->prepare(fixture->callback.get(), 0);
  ASSERT_TRUE(fixture->callback->waitForPrepared(kTimeout)) << "the period was not prepared";

  // The duration of a WAV file is known from its header, so it is reported as soon as the header
  // was parsed.
  EXPECT_EQ(period->getDurationUs(), kDurationUs);
  // The WAV extractor does not support seeking, so the period reports that it cannot be seeked.
  EXPECT_FALSE(period->isSeekable());

  ASSERT_TRUE(fixture->caller->waitForDuration(kDurationUs, kTimeout));
  EXPECT_FALSE(fixture->caller->isPlaceholder());
  EXPECT_EQ(fixture->caller->windowCount(), 1);
  EXPECT_EQ(fixture->caller->periodCount(), 1);
}

TEST(ProgressiveMediaSourceTest, PeriodExposesTheAudioTrack) {
  auto fixture = createFixture(test::buildWav(kSampleRate, kChannelCount, kSampleCount));
  fixture->period->prepare(fixture->callback.get(), 0);
  ASSERT_TRUE(fixture->callback->waitForPrepared(kTimeout));

  const auto& groups = fixture->period->getTrackGroups();
  ASSERT_EQ(groups->length(), 1);
  const auto& group = groups->get(0);
  ASSERT_EQ(group->length(), 1);
  const auto& format = group->getFormat(0);
  ASSERT_NE(format, nullptr);
  EXPECT_TRUE(format->isAudio());
  EXPECT_FALSE(format->isVideo());
  EXPECT_EQ(format->getMimeType(), MimeTypes::AUDIO_RAW);
  EXPECT_EQ(format->getSampleRate(), kSampleRate);
  EXPECT_EQ(format->getChannelCount(), kChannelCount);
}

TEST(ProgressiveMediaSourceTest, LoadsAndDeliversEveryByteOfTheMedia) {
  auto fixture = createFixture(test::buildWav(kSampleRate, kChannelCount, kSampleCount));
  fixture->period->prepare(fixture->callback.get(), 0);
  ASSERT_TRUE(fixture->callback->waitForPrepared(kTimeout));

  const auto period = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(fixture->period);
  ASSERT_NE(period, nullptr);
  const auto stream = selectOnlyTrack(*fixture->period);
  ASSERT_NE(stream, nullptr);

  // Preparation does not read the whole media: the load parks and asks whether it should continue.
  EXPECT_NE(period->getBufferedPositionUs(), Definitions::kTimeEndOfSource);

  loadToEnd(*period, *fixture->callback);
  ASSERT_EQ(period->getBufferedPositionUs(), Definitions::kTimeEndOfSource);

  const auto samples = readAllSamples(*stream);
  ASSERT_FALSE(samples.empty());

  // The period delivers the whole payload of the media, split in as many samples as the extractor
  // produced, in the order of their timestamps.
  size_t totalSize = 0;
  int64_t previousTimeUs = -1;
  for (const auto& sample : samples) {
    totalSize += sample.size;
    EXPECT_TRUE(sample.keyFrame);
    EXPECT_GE(sample.timeUs, previousTimeUs) << "the timestamps are not monotonic";
    previousTimeUs = sample.timeUs;
  }
  EXPECT_EQ(totalSize, static_cast<size_t>(kPayloadSize));
  EXPECT_EQ(samples.front().timeUs, 0);
  EXPECT_FALSE(samples.front().discard);
}

TEST(ProgressiveMediaSourceTest, SeekInsideTheBufferedMediaKeepsTheLoadFinished) {
  auto fixture = createFixture(test::buildWav(kSampleRate, kChannelCount, kSampleCount));
  fixture->period->prepare(fixture->callback.get(), 0);
  ASSERT_TRUE(fixture->callback->waitForPrepared(kTimeout));

  const auto period = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(fixture->period);
  ASSERT_NE(period, nullptr);
  const auto stream = selectOnlyTrack(*fixture->period);
  ASSERT_NE(stream, nullptr);
  loadToEnd(*period, *fixture->callback);
  ASSERT_EQ(period->getBufferedPositionUs(), Definitions::kTimeEndOfSource);

  const int64_t seekPositionUs = kDurationUs / 2;
  EXPECT_EQ(period->seekToUs(seekPositionUs), seekPositionUs);
  // The seek was served from the buffered samples, so the media was not read again and the load is
  // still finished.
  EXPECT_EQ(period->getBufferedPositionUs(), Definitions::kTimeEndOfSource);
  // A seek inside the buffer is not a discontinuity: the samples before the target are flagged as
  // to be discarded after they are decoded, see SampleQueue::readSampleMetadata.
  EXPECT_EQ(period->readDiscontinuity(), Definitions::kTimeUnset);

  const auto samples = readAllSamples(*stream);
  ASSERT_FALSE(samples.empty());
  // The stream restarts at the sample at or before the target, which is flagged for decoding only.
  EXPECT_LE(samples.front().timeUs, seekPositionUs);
  EXPECT_TRUE(samples.front().discard);
  size_t firstRendered = samples.size();
  for (size_t index = 0; index < samples.size(); ++index) {
    if (!samples[index].discard) {
      firstRendered = index;
      break;
    }
  }
  ASSERT_LT(firstRendered, samples.size()) << "no sample at or after the seek target";
  EXPECT_GE(samples[firstRendered].timeUs, seekPositionUs);
}

TEST(ProgressiveMediaSourceTest, SeekOutsideTheBufferedMediaRestartsTheLoad) {
  auto fixture = createFixture(test::buildWav(kSampleRate, kChannelCount, kSampleCount));
  fixture->period->prepare(fixture->callback.get(), 0);
  ASSERT_TRUE(fixture->callback->waitForPrepared(kTimeout));

  const auto period = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(fixture->period);
  ASSERT_NE(period, nullptr);
  const auto stream = selectOnlyTrack(*fixture->period);
  ASSERT_NE(stream, nullptr);

  // Nothing was buffered yet at the target, so the media has to be read again from there. The
  // period reports the pending reset until the load restarted.
  const int64_t seekPositionUs = kDurationUs / 2;
  EXPECT_EQ(period->seekToUs(seekPositionUs), seekPositionUs);
  EXPECT_EQ(period->getBufferedPositionUs(), seekPositionUs);

  loadToEnd(*period, *fixture->callback);
  ASSERT_EQ(period->getBufferedPositionUs(), Definitions::kTimeEndOfSource);

  const auto samples = readAllSamples(*stream);
  ASSERT_FALSE(samples.empty());
  size_t firstRendered = samples.size();
  for (size_t index = 0; index < samples.size(); ++index) {
    if (!samples[index].discard) {
      firstRendered = index;
      break;
    }
  }
  ASSERT_LT(firstRendered, samples.size()) << "no sample at or after the seek target";
  EXPECT_GE(samples[firstRendered].timeUs, seekPositionUs);
}

TEST(ProgressiveMediaSourceTest, UnrecognizedMediaReportsAPrepareError) {
  // A container that no extractor recognizes cannot be played, and no retry can fix it. The period
  // reports it by throwing from `maybeThrowPrepareError`, the way ExoPlayer's periods do, so the player
  // is the one that decides what the failure means.
  std::vector<uint8_t> garbage(4096, 0x5A);
  auto fixture = createFixture(std::move(garbage));

  EXPECT_NO_THROW(fixture->period->maybeThrowPrepareError());
  fixture->period->prepare(fixture->callback.get(), 0);
  // The failure is discovered by the load, which runs on the loader thread, so the period reports it
  // once that load has run.
  ASSERT_TRUE(fixture->callback->waitForCannotBePrepared(*fixture->period, kTimeout));
  EXPECT_THROW(fixture->period->maybeThrowPrepareError(), MediaPeriodException);
  EXPECT_FALSE(fixture->callback->errorMessage().empty());
}

TEST(ProgressiveMediaSourceTest, ReportsTheDurationAndTheSeekabilityOfASeekableContainer) {
  constexpr int kMp4SampleCount = 12;
  // Every sample of the fixture lasts one millisecond, see the builder.
  constexpr int64_t kMp4DurationUs = kMp4SampleCount * 1000;

  auto fixture = createFixture(test::buildMp4(kMp4SampleCount), "memory://test.mp4");
  const auto period = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(fixture->period);
  ASSERT_NE(period, nullptr);
  fixture->period->prepare(fixture->callback.get(), 0);
  ASSERT_TRUE(fixture->callback->waitForPrepared(kTimeout));

  // The movie box declares the duration before any sample is read, and the sample table makes the
  // container seekable.
  EXPECT_EQ(period->getDurationUs(), kMp4DurationUs);
  EXPECT_TRUE(period->isSeekable());
  ASSERT_TRUE(fixture->caller->waitForDuration(kMp4DurationUs, kTimeout));
  EXPECT_FALSE(fixture->caller->isPlaceholder());

  const auto& groups = fixture->period->getTrackGroups();
  ASSERT_EQ(groups->length(), 1);
  const auto& format = groups->get(0)->getFormat(0);
  ASSERT_NE(format, nullptr);
  EXPECT_TRUE(format->isAudio());
  EXPECT_EQ(format->getMimeType(), MimeTypes::AUDIO_AAC);
  EXPECT_EQ(format->getCodecs(), "mp4a.40.2");
  ASSERT_FALSE(format->csdList().empty());

  const auto stream = selectOnlyTrack(*fixture->period);
  ASSERT_NE(stream, nullptr);
  loadToEnd(*period, *fixture->callback);
  ASSERT_EQ(period->getBufferedPositionUs(), Definitions::kTimeEndOfSource);

  const auto samples = readAllSamples(*stream);
  ASSERT_EQ(samples.size(), static_cast<size_t>(kMp4SampleCount));
  for (size_t index = 0; index < samples.size(); ++index) {
    EXPECT_EQ(samples[index].timeUs, static_cast<int64_t>(index) * 1000);
    EXPECT_EQ(samples[index].size, 1024u);
    EXPECT_TRUE(samples[index].keyFrame);
  }
}

TEST(ProgressiveMediaSourceTest, SeekableMediaResolvesTheSeekPositionFromItsSeekMap) {
  constexpr int kMp4SampleCount = 12;
  constexpr int64_t kSeekPositionUs = 6000;
  constexpr int64_t kSeekSampleIndex = kSeekPositionUs / 1000;

  auto fixture = createFixture(test::buildMp4(kMp4SampleCount), "memory://test.mp4");
  const auto period = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(fixture->period);
  ASSERT_NE(period, nullptr);
  fixture->period->prepare(fixture->callback.get(), 0);
  ASSERT_TRUE(fixture->callback->waitForPrepared(kTimeout));

  // The seek parameters are exact, so the adjusted position is the requested one.
  EXPECT_EQ(period->getAdjustedSeekPositionUs(kSeekPositionUs, SeekParameters(0, 0)),
      kSeekPositionUs);

  const auto stream = selectOnlyTrack(*fixture->period);
  ASSERT_NE(stream, nullptr);

  // The target is not buffered yet, so the media is read again from the byte position that the seek
  // map resolves for it.
  EXPECT_EQ(period->seekToUs(kSeekPositionUs), kSeekPositionUs);
  loadToEnd(*period, *fixture->callback);
  ASSERT_EQ(period->getBufferedPositionUs(), Definitions::kTimeEndOfSource);

  const auto samples = readAllSamples(*stream);
  ASSERT_EQ(samples.size(), static_cast<size_t>(kMp4SampleCount - kSeekSampleIndex));
  EXPECT_EQ(samples.front().timeUs, kSeekPositionUs);
  for (size_t index = 0; index < samples.size(); ++index) {
    EXPECT_EQ(samples[index].timeUs, kSeekPositionUs + static_cast<int64_t>(index) * 1000);
  }
}

TEST(ProgressiveMediaSourceTest, ReportsTheLoadEventsToItsListeners) {
  auto fixture = createFixture(test::buildWav(kSampleRate, kChannelCount, kSampleCount));
  auto listener = std::make_shared<RecordingMediaSourceEventListener>();
  // The listener is registered before the period exists, like an application that registers on the
  // player before it prepares it.
  fixture->source->addEventListener(nullptr, listener);

  const auto period = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(fixture->period);
  ASSERT_NE(period, nullptr);
  fixture->period->prepare(fixture->callback.get(), 0);
  ASSERT_TRUE(fixture->callback->waitForPrepared(kTimeout));
  ASSERT_TRUE(listener->waitFor([&] { return listener->loadStartedCount() > 0; }, kTimeout))
      << "no load started event was reported";

  const auto stream = selectOnlyTrack(*fixture->period);
  ASSERT_NE(stream, nullptr);
  loadToEnd(*period, *fixture->callback);

  ASSERT_TRUE(listener->waitFor([&] { return listener->loadCompletedCount() > 0; }, kTimeout))
      << "no load completed event was reported";
  EXPECT_EQ(listener->loadStartedCount(), 1);
  EXPECT_EQ(listener->loadErrorCount(), 0);
  EXPECT_EQ(listener->lastUrl(), "memory://test.wav");
  EXPECT_GE(listener->lastBytesLoaded(), kPayloadSize);

  // Reading the samples hands the format of the track to a renderer for the first time, which is
  // reported as a downstream format change.
  readAllSamples(*stream);
  ASSERT_TRUE(listener->waitFor([&] { return listener->downstreamFormatCount() > 0; }, kTimeout))
      << "no downstream format change was reported";
  const auto format = listener->lastTrackFormat();
  ASSERT_NE(format, nullptr);
  EXPECT_TRUE(format->isAudio());
  EXPECT_EQ(format->getSampleRate(), kSampleRate);

  // The period identifiers the events carry are the ones the period was created with.
  ASSERT_TRUE(listener->lastMediaPeriodId().has_value());
  EXPECT_EQ(listener->lastMediaPeriodId()->periodUid().id(), "test-period");

  fixture->source->removeEventListener(listener);
  EXPECT_EQ(listener->loadStartedCount(), 1);
}

TEST(ProgressiveMediaSourceTest, ReportsTheLoadErrorOfAnUnrecognizedContainer) {
  std::vector<uint8_t> garbage(4096, 0x5A);
  auto fixture = createFixture(std::move(garbage));
  auto listener = std::make_shared<RecordingMediaSourceEventListener>();
  fixture->source->addEventListener(nullptr, listener);

  fixture->period->prepare(fixture->callback.get(), 0);
  // The load error is reported to the listeners of the source, and the period additionally stores the
  // failure, which is what the player turns into a playback error through `maybeThrowPrepareError`.
  ASSERT_TRUE(fixture->callback->waitForCannotBePrepared(*fixture->period, kTimeout));

  ASSERT_TRUE(listener->waitFor([&] { return listener->loadErrorCount() > 0; }, kTimeout))
      << "no load error event was reported";
  EXPECT_FALSE(listener->lastErrorMessage().empty());
  // The failure cannot be fixed by a retry, so the load is cancelled rather than retried.
  EXPECT_TRUE(listener->lastErrorWasCanceled());
}

TEST(ProgressiveMediaSourceTest, ReleasesThePeriodWithoutWaitingForACallback) {
  auto fixture = createFixture(test::buildWav(kSampleRate, kChannelCount, kSampleCount));
  fixture->period->prepare(fixture->callback.get(), 0);
  ASSERT_TRUE(fixture->callback->waitForPrepared(kTimeout));

  auto progressPeriod = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(fixture->period);
  ASSERT_NE(progressPeriod, nullptr);

  // The load is stopped and joined by `release`, and the sample queues, the extractor and the data
  // source are freed there. The period therefore does not have to keep itself alive until a release
  // callback is dispatched, which is what it used to do: a callback that was posted but never
  // delivered kept the media of the period allocated for good.
  std::weak_ptr<MediaPeriod> weakPeriod = fixture->period;
  progressPeriod->release();
  fixture->period.reset();
  progressPeriod.reset();
  EXPECT_TRUE(weakPeriod.expired())
      << "the released period is still reachable, so something it owns keeps it alive";

  fixture->source->releaseSource(fixture->caller);
  fixture->source.reset();
}

TEST(ProgressiveMediaSourceTest, ReleasesAStreamingPeriodWithoutWaitingForACallback) {
  // The same, with the load still in flight: `release` joins the loading thread, so the resources
  // cannot be freed while the loader is reading, and the period is gone once it returns.
  auto fixture = createFixture(test::buildWav(kSampleRate, kChannelCount, kSampleCount));
  fixture->period->prepare(fixture->callback.get(), 0);

  auto progressPeriod = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(fixture->period);
  ASSERT_NE(progressPeriod, nullptr);

  std::weak_ptr<MediaPeriod> weakPeriod = fixture->period;
  progressPeriod->release();
  fixture->period.reset();
  progressPeriod.reset();
  EXPECT_TRUE(weakPeriod.expired());

  fixture->source->releaseSource(fixture->caller);
  fixture->source.reset();
}

TEST(ProgressiveMediaSourceTest, DiscardsTheBackBufferTheSelectionDoesNotNeed) {
  auto fixture = createFixture(test::buildWav(kSampleRate, kChannelCount, kSampleCount));
  auto listener = std::make_shared<RecordingMediaSourceEventListener>();
  fixture->source->addEventListener(nullptr, listener);

  fixture->period->prepare(fixture->callback.get(), 0);
  ASSERT_TRUE(fixture->callback->waitForPrepared(kTimeout));
  const auto period = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(fixture->period);
  ASSERT_NE(period, nullptr);

  // An adaptive selection keeps `minDurationToRetainAfterDiscardMs` behind the playback position and
  // asks the source to give up the rest of what it read ahead. The default of twenty five seconds is
  // longer than the whole fixture, so a shorter one is used here.
  constexpr int64_t kMinDurationToRetainAfterDiscardMs = 1000;
  const auto meter = std::make_shared<DefaultBandwidthMeter>(/* initialBitrateEstimate= */ 10000000);
  ASSERT_GT(fixture->period->getTrackGroups()->length(), 0);
  auto selection = std::make_shared<AdaptiveTrackSelection>(
      fixture->period->getTrackGroups()->get(0),
      std::vector<int>{ 0 },
      meter,
      /* reservedBandwidth= */ 0,
      AdaptiveTrackSelection::DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS,
      AdaptiveTrackSelection::DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS,
      kMinDurationToRetainAfterDiscardMs,
      AdaptiveTrackSelection::DEFAULT_BANDWIDTH_FRACTION,
      AdaptiveTrackSelection::DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE);
  std::vector<std::shared_ptr<TrackSelection>> selections{ selection };
  std::vector<bool> mayRetainStreamFlags(1, false);
  std::vector<std::shared_ptr<SampleStream>> streams(1);
  std::vector<bool> streamResetFlags(1);
  fixture->period->selectTracks(selections, mayRetainStreamFlags, streams, streamResetFlags, 0);

  loadToEnd(*period, *fixture->callback);

  // The player drives this on every iteration of its playback loop.
  period->reevaluateBuffer(0);

  ASSERT_TRUE(listener->waitFor([&] { return listener->upstreamDiscardedCount() > 0; }, kTimeout))
      << "the media that the selection does not need was not discarded";
  EXPECT_GT(listener->lastDiscardedMediaStartTimeMs(), kMinDurationToRetainAfterDiscardMs);
  EXPECT_LT(listener->lastDiscardedMediaStartTimeMs(), kDurationUs / 1000);
  EXPECT_GE(listener->lastDiscardedMediaEndTimeMs(), listener->lastDiscardedMediaStartTimeMs());
  // The event belongs to the period the media is read for.
  ASSERT_TRUE(listener->lastDiscardedMediaPeriodId().has_value());
  EXPECT_EQ(listener->lastDiscardedMediaPeriodId()->periodUid().id(), "test-period");
}

} // namespace anonymous
} // namespace coral
