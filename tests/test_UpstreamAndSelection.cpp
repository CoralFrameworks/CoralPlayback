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

#include <coral/selection/AdaptiveTrackSelection.h>
#include <coral/selection/DefaultTrackSelector.h>
#include <coral/selection/SelectionCapabilities.h>
#include <coral/selection/SelectionOverride.h>
#include <coral/selection/TrackGroup.h>
#include <coral/selection/TrackSelectorResult.h>
#include <coral/source/SampleQueue.h>
#include <coral/upstream/BandwidthMeter.h>
#include <coral/upstream/DefaultAllocator.h>
#include <coral/upstream/DefaultBandwidthMeter.h>
#include <coral/upstream/DefaultLoadErrorHandlingPolicy.h>
#include <coral/upstream/SlidingPercentile.h>
#include <core/time/SystemClock.h>
#include <coral/util/SystemLanguage.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace coral {
namespace {

constexpr int kAllocationSize = 64;

/**
 * A renderer that handles every audio or video format, so that the selector only decides on the
 * quality of the formats themselves.
 */
class FakeRendererCapabilities final : public SelectionCapabilities {
public:
  explicit FakeRendererCapabilities(const MediaType mediaType,
    const AdaptiveSupport adaptiveSupport =
      SelectionCapabilities::ADAPTIVE_SEAMLESS) noexcept
    : mediaType(mediaType),
      adaptiveSupport(adaptiveSupport) {
  }

  [[nodiscard]] char const* getName() const override { return "FakeRenderer"; }

  [[nodiscard]] MediaType getMediaType() const override { return mediaType; }

  Capabilities supportsFormat(const std::shared_ptr<MediaFormat>& format) override {
    static_cast<void>(format);
    return SelectionCapabilities::create(SelectionCapabilities::FORMAT_HANDLED);
  }

  AdaptiveSupport supportsMixedMimeTypeAdaptation() override {
    return adaptiveSupport;
  }

private:
  MediaType mediaType;
  AdaptiveSupport adaptiveSupport;
};

std::shared_ptr<MediaFormat> createVideoFormat(const int width,
  const int height,
  const int bitrate) {
  auto format = MediaFormat::CreateVideoFormat("video/avc", width, height);
  format->setAverageBitrate(bitrate);
  return format;
}

std::shared_ptr<MediaFormat> createAudioFormat(const int channelCount,
  const int sampleRate,
  const int bitrate) {
  auto format = MediaFormat::CreateAudioFormat("audio/mp4a-latm", sampleRate, channelCount);
  format->setAverageBitrate(bitrate);
  return format;
}

// ==================================================================================================
// upstream
// ==================================================================================================

TEST(DefaultAllocatorTest, ReusesEveryAllocationItReleasedAfterTheFreeListGrew) {
  // More allocations than the free list holds spare slots, so that the list has to grow. Writing the
  // released allocation past the end of the list used to be an out-of-bounds write.
  // More than the spare slots the allocator creates up front, so that its free list has to grow.
  constexpr int kAllocationCount = 200;
  auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, kAllocationSize);

  std::vector<std::shared_ptr<Allocation>> allocations;
  allocations.reserve(kAllocationCount);
  for (int i = 0; i < kAllocationCount; ++i) {
    auto allocation = allocator->allocate();
    ASSERT_NE(allocation, nullptr);
    ASSERT_NE(allocation->data(), nullptr);
    allocations.push_back(allocation);
  }
  EXPECT_EQ(allocator->getTotalBytesAllocated(), kAllocationCount * kAllocationSize);

  allocator->release(allocations);
  EXPECT_EQ(allocator->getTotalBytesAllocated(), 0);

  // Every released allocation has to come back out, and the buffers must not overlap.
  std::set<const uint8_t*> buffers;
  for (int i = 0; i < kAllocationCount; ++i) {
    auto allocation = allocator->allocate();
    ASSERT_NE(allocation, nullptr);
    EXPECT_TRUE(buffers.insert(allocation->data()).second)
        << "the allocator handed out the same buffer twice";
    allocations[static_cast<size_t>(i)] = nullptr;
  }
  allocator->release(allocations);
}

TEST(DefaultAllocatorTest, ReleasesAChainOfAllocations) {
  auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, kAllocationSize);
  auto first = allocator->allocate();
  auto second = allocator->allocate();

  // A two node chain, like the one a sample data queue builds.
  class Node final : public Allocator::Node {
  public:
    Node(std::shared_ptr<Allocation> allocation,
      std::shared_ptr<Allocator::Node> next) noexcept
      : allocation(std::move(allocation)),
        next(std::move(next)) {
    }

    [[nodiscard]] const std::shared_ptr<Allocation>& getAllocation() const override {
      return allocation;
    }

    std::shared_ptr<Allocator::Node> getNext() override { return next; }

  private:
    std::shared_ptr<Allocation> allocation;
    std::shared_ptr<Allocator::Node> next;
  };

  auto chain = std::make_shared<Node>(second, std::make_shared<Node>(first, nullptr));
  allocator->release(chain);
  EXPECT_EQ(allocator->getTotalBytesAllocated(), 0);
  EXPECT_NE(allocator->allocate(), nullptr);
  EXPECT_NE(allocator->allocate(), nullptr);
}

TEST(DefaultAllocatorTest, TellsWhetherAnAllocationOwnsItsBuffer) {
  auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, kAllocationSize);
  auto fresh = allocator->allocate();
  ASSERT_NE(fresh, nullptr);
  // The allocation owns the buffer it was handed, so destroying it frees the buffer.
  EXPECT_TRUE(fresh->ownsData());

  auto fromBlock = DefaultAllocator(/* trimOnReset= */ true, kAllocationSize, /* initialAllocationCount= */ 2)
    .allocate();
  ASSERT_NE(fromBlock, nullptr);
  // The allocations created up front borrow one block that their allocator owns.
  EXPECT_FALSE(fromBlock->ownsData());
}

TEST(SlidingPercentileTest, ReportsThePercentileOfTheSmallestValuesFirst) {
  SlidingPercentile percentile(/* maxWeight= */ 1000);
  percentile.addSample(1, 10.0F);
  percentile.addSample(1, 20.0F);
  percentile.addSample(1, 30.0F);
  percentile.addSample(1, 40.0F);

  // The samples are accumulated from the smallest value upwards, so the 25th percentile is a small
  // value and not a large one.
  EXPECT_FLOAT_EQ(percentile.getPercentile(0.25F), 10.0F);
  EXPECT_FLOAT_EQ(percentile.getPercentile(0.5F), 20.0F);
  EXPECT_FLOAT_EQ(percentile.getPercentile(1.0F), 40.0F);
}

TEST(SlidingPercentileTest, EvictsTheOldestSampleWhenTheWindowIsFull) {
  SlidingPercentile percentile(/* maxWeight= */ 20);
  // The first sample carries the whole window, so the second one evicts it.
  percentile.addSample(20, 1000.0F);
  percentile.addSample(10, 10.0F);

  EXPECT_FLOAT_EQ(percentile.getPercentile(0.5F), 10.0F);
  // The whole window is the largest value it holds: the newest observation is kept, the oldest one
  // gave up its weight.
  EXPECT_FLOAT_EQ(percentile.getPercentile(1.0F), 1000.0F);
}

TEST(DefaultBandwidthMeterTest, EstimatesTheBitrateFromTheTransfersItObserves) {
  auto meter = std::make_shared<DefaultBandwidthMeter>(/* initialBitrateEstimate= */ 100);
  EXPECT_EQ(meter->getBitrateEstimate(), 100);

  // A local transfer is not measured, like ExoPlayer: it says nothing about the network.
  meter->onTransferStart("file://local", /* isNetwork= */ false);
  meter->onBytesTransferred("file://local", /* isNetwork= */ false, 1 << 20);
  meter->onTransferEnd("file://local", /* isNetwork= */ false);
  EXPECT_EQ(meter->getBitrateEstimate(), 100);

  // A network transfer that moved bytes is measured. The estimate is a median of one sample, so it
  // is strictly larger than the initial guess for a fast transfer. The transfer has to take at least
  // a millisecond to be measurable, so the test waits for one.
  meter->onTransferStart("https://example.test/media", /* isNetwork= */ true);
  meter->onBytesTransferred("https://example.test/media", /* isNetwork= */ true, 1 << 20);
  std::this_thread::sleep_for(std::chrono::milliseconds(3));
  meter->onTransferEnd("https://example.test/media", /* isNetwork= */ true);
  EXPECT_GT(meter->getBitrateEstimate(), 100);
}

TEST(DefaultLoadErrorHandlingPolicyTest, GrowsTheRetryDelayAndCapsIt) {
  auto policy = std::make_shared<DefaultLoadErrorHandlingPolicy>();
  EXPECT_EQ(policy->getMinimumLoadableRetryCount(DataType::kMedia),
    DefaultLoadErrorHandlingPolicy::DEFAULT_MIN_LOADABLE_RETRY_COUNT);

  LoadErrorHandlingPolicy::LoadErrorInfo info;
  info.errorCount = 1;
  EXPECT_EQ(policy->getRetryDelayMsFor(info), 0);
  info.errorCount = 2;
  EXPECT_EQ(policy->getRetryDelayMsFor(info), 1000);
  info.errorCount = 3;
  EXPECT_EQ(policy->getRetryDelayMsFor(info), 2000);
  info.errorCount = 100;
  EXPECT_EQ(policy->getRetryDelayMsFor(info), DefaultLoadErrorHandlingPolicy::DEFAULT_MAX_RETRY_DELAY_MS);
}

// ==================================================================================================
// selection
// ==================================================================================================

/**
 * A meter that reports a fixed bitrate, so that the adaptive selection can be tested without a
 * network.
 */
class FixedBandwidthMeter final : public BandwidthMeter {
public:
  explicit FixedBandwidthMeter(const int64_t bitrate) noexcept : bitrate(bitrate) {
  }

  int64_t getBitrateEstimate() override { return bitrate; }

  /** Changes the estimate, as a real meter does when the network conditions change. */
  void setBitrateEstimate(const int64_t newBitrate) { bitrate = newBitrate; }

  void addEventListener(const std::shared_ptr<EventHandler>& eventHandler,
    const std::shared_ptr<EventListener>& listener) override {
    static_cast<void>(eventHandler);
    static_cast<void>(listener);
  }

  void removeEventListener(const std::shared_ptr<EventListener>& listener) override {
    static_cast<void>(listener);
  }

private:
  int64_t bitrate;
};

std::shared_ptr<TrackGroup> createVideoTrackGroup() {
  // The formats are in an order that is neither ascending nor descending by bitrate, so that the
  // selection has to establish the order itself.
  return std::make_shared<TrackGroup>(MediaType::kMediaTypeVideo,
    std::vector<std::shared_ptr<MediaFormat>>{
      createVideoFormat(1280, 720, 1500000),
      createVideoFormat(320, 180, 200000),
      createVideoFormat(1920, 1080, 4000000)
    });
}

TEST(AdaptiveTrackSelectionTest, OrdersTheTracksByIncreasingBitrate) {
  auto group = createVideoTrackGroup();
  // The group declares its tracks in the order 1.5 Mbit/s, 200 kbit/s, 4 Mbit/s. The selection orders
  // them itself, so the track it picks for a bandwidth that only fits the smallest one is the 200
  // kbit/s track, wherever it sits in the group.
  AdaptiveTrackSelection selection(group, std::vector<int>{ 0, 1, 2 },
    std::make_shared<FixedBandwidthMeter>(200000));

  ASSERT_NE(selection.getSelectedFormat(), nullptr);
  EXPECT_EQ(selection.getSelectedFormat()->getAverageBitrate(), 200000);
}

TEST(AdaptiveTrackSelectionTest, SelectsTheHighestTrackThatFitsTheBandwidth) {
  auto group = createVideoTrackGroup();

  // The default fraction of the estimate that a selection may use is 0.7, so 3 Mbit/s allocates
  // 2.1 Mbit/s: that fits the 200 kbit/s and the 1.5 Mbit/s tracks, but not the 4 Mbit/s one.
  AdaptiveTrackSelection medium(group, std::vector<int>{ 0, 1, 2 },
    std::make_shared<FixedBandwidthMeter>(3000000));
  EXPECT_EQ(medium.getSelectedFormat()->getAverageBitrate(), 1500000);

  // 2 Mbit/s allocates 1.4 Mbit/s, which only the smallest track fits.
  AdaptiveTrackSelection small(group, std::vector<int>{ 0, 1, 2 },
    std::make_shared<FixedBandwidthMeter>(2000000));
  EXPECT_EQ(small.getSelectedFormat()->getAverageBitrate(), 200000);

  // A bandwidth that fits everything selects the largest track.
  AdaptiveTrackSelection high(group, std::vector<int>{ 0, 1, 2 },
    std::make_shared<FixedBandwidthMeter>(10000000));
  EXPECT_EQ(high.getSelectedFormat()->getAverageBitrate(), 4000000);

  // A bandwidth that fits nothing selects the smallest track rather than nothing.
  AdaptiveTrackSelection tooSlow(group, std::vector<int>{ 0, 1, 2 },
    std::make_shared<FixedBandwidthMeter>(1000));
  EXPECT_EQ(tooSlow.getSelectedFormat()->getAverageBitrate(), 200000);
}

TEST(AdaptiveTrackSelectionTest, NeedsMoreBandwidthForTheSameTrackAtHigherPlaybackSpeed) {
  auto group = createVideoTrackGroup();

  // 2.5 Mbit/s allocates 1.75 Mbit/s, which fits the 1.5 Mbit/s track at the normal speed.
  AdaptiveTrackSelection normal(group, std::vector<int>{ 0, 1, 2 },
    std::make_shared<FixedBandwidthMeter>(2500000));
  EXPECT_EQ(normal.getSelectedFormat()->getAverageBitrate(), 1500000);

  // At twice the speed the same track needs twice the bandwidth, so it no longer fits.
  AdaptiveTrackSelection fast(group, std::vector<int>{ 0, 1, 2 },
    std::make_shared<FixedBandwidthMeter>(2500000));
  fast.onPlaybackSpeed(2.0f);
  fast.updateSelectedTrack(/* playbackPositionUs= */ 0, /* bufferedDurationUs= */ 60000000,
    /* availableDurationUs= */ Definitions::kTimeEndOfSource, /* queueSize= */ 0);
  EXPECT_EQ(fast.getSelectedFormat()->getAverageBitrate(), 200000);
}

TEST(AdaptiveTrackSelectionTest, OnlySwitchesUpWhenEnoughMediaIsBuffered) {
  auto group = createVideoTrackGroup();
  // The selection starts at the lowest track, because that is all the initial estimate allows.
  auto meter = std::make_shared<FixedBandwidthMeter>(1000);
  auto selection = AdaptiveTrackSelection(group, std::vector<int>{ 0, 1, 2 }, meter);
  selection.onPlayWhenReadyChanged(true);
  ASSERT_EQ(selection.getSelectedFormat()->getAverageBitrate(), 200000);

  // The bandwidth recovers, so every track fits and the ideal one becomes the largest.
  meter->setBitrateEstimate(10000000);

  // Switching up needs at least DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS buffered, so a short
  // buffer leaves the selection where it is.
  selection.updateSelectedTrack(/* playbackPositionUs= */ 0, /* bufferedDurationUs= */ 1000000,
    /* availableDurationUs= */ Definitions::kTimeEndOfSource, /* queueSize= */ 0);
  EXPECT_EQ(selection.getSelectedFormat()->getAverageBitrate(), 200000);

  // With enough media buffered the switch is applied.
  selection.updateSelectedTrack(0, /* bufferedDurationUs= */ 20000000,
    Definitions::kTimeEndOfSource, 0);
  EXPECT_EQ(selection.getSelectedFormat()->getAverageBitrate(), 4000000);
}

TEST(AdaptiveTrackSelectionTest, SwitchesDownAsSoonAsTheBandwidthDrops) {
  auto group = createVideoTrackGroup();
  auto meter = std::make_shared<FixedBandwidthMeter>(10000000);
  auto selection = AdaptiveTrackSelection(group, std::vector<int>{ 0, 1, 2 }, meter);
  selection.onPlayWhenReadyChanged(true);
  ASSERT_EQ(selection.getSelectedFormat()->getAverageBitrate(), 4000000);

  // A decrease in quality is applied without waiting for a longer buffer, because the alternative is
  // running out of media.
  meter->setBitrateEstimate(1000);
  selection.updateSelectedTrack(0, /* bufferedDurationUs= */ 1000000,
    Definitions::kTimeEndOfSource, 0);
  EXPECT_EQ(selection.getSelectedFormat()->getAverageBitrate(), 200000);
}

TEST(AdaptiveTrackSelectionTest, RejectsATrackThatDoesNotFitTheBandwidth) {
  auto format = createVideoFormat(1280, 720, 1500000);
  EXPECT_TRUE(AdaptiveTrackSelection::canSelectFormat(format, 1500000, 1500000));
  EXPECT_TRUE(AdaptiveTrackSelection::canSelectFormat(format, 1500000, 2000000));
  EXPECT_FALSE(AdaptiveTrackSelection::canSelectFormat(format, 1500000, 1000000));

  // A track whose bitrate is unknown cannot be ruled out.
  auto unknown = createVideoFormat(640, 360, Definitions::kUnspecified);
  unknown->setAverageBitrate(Definitions::kUnspecified);
  EXPECT_EQ(AdaptiveTrackSelection::getFormatBitrate(unknown), Definitions::kUnspecified);
  EXPECT_TRUE(AdaptiveTrackSelection::canSelectFormat(unknown, Definitions::kUnspecified, 1000));

  // The peak bitrate is the fallback when the average one is not declared.
  auto peakOnly = createVideoFormat(640, 360, Definitions::kUnspecified);
  peakOnly->setAverageBitrate(Definitions::kUnspecified);
  peakOnly->setPeakBitrate(800000);
  EXPECT_EQ(AdaptiveTrackSelection::getFormatBitrate(peakOnly), 800000);
}

TEST(DefaultTrackSelectorTest, HonoursAnOverrideOfTheSelectedTracks) {
  std::vector<std::shared_ptr<MediaFormat>> formats{
    createVideoFormat(320, 180, 200000),
    createVideoFormat(1920, 1080, 4000000),
    createVideoFormat(1280, 720, 1500000)
  };
  auto group = std::make_shared<TrackGroup>(MediaType::kMediaTypeVideo, formats);
  auto trackGroups = std::make_shared<TrackGroupArray>(
    std::vector<std::shared_ptr<TrackGroup>>{ group });

  // The selector would pick the 1080p rendition, but the application asks for the 320x180 one.
  auto parameters = ParametersBuilder()
                    .setSelectionOverride(0, SelectionOverride(0, 0))
                    .build();
  auto selector = std::make_shared<DefaultTrackSelector>(parameters, nullptr);
  EXPECT_TRUE(selector->hasSelectionOverride(0));
  ASSERT_TRUE(selector->getSelectionOverride(0).has_value());
  EXPECT_EQ(selector->getSelectionOverride(0)->tracks, (std::vector<int>{ 0 }));

  std::vector<std::shared_ptr<SelectionCapabilities>> capabilities{
    std::make_shared<FakeRendererCapabilities>(MediaType::kMediaTypeVideo)
  };
  auto result = selector->selectTracks(capabilities, trackGroups);
  ASSERT_NE(result, nullptr);
  const auto& selection = result->getSelection(0);
  ASSERT_NE(selection, nullptr);
  EXPECT_EQ(selection->getFormat(0)->getWidth(), 320);

  // An override that names no track deselects the renderer while leaving it enabled, and an override
  // that becomes impossible is ignored rather than applied to another group.
  selector->setSelectionOverride(0, SelectionOverride(0, {}));
  auto disabled = selector->selectTracks(capabilities, trackGroups);
  ASSERT_NE(disabled, nullptr);
  EXPECT_EQ(disabled->getSelection(0), nullptr);

  selector->setSelectionOverride(0, SelectionOverride(/* groupIndex= */ 7, 0));
  EXPECT_FALSE(selector->hasSelectionOverride(0) == false);
  auto ignored = selector->selectTracks(capabilities, trackGroups);
  ASSERT_NE(ignored, nullptr);
  // The override names a group the renderer does not have, so the selector decides again.
  ASSERT_NE(ignored->getSelection(0), nullptr);
  EXPECT_EQ(ignored->getSelection(0)->getFormat(0)->getWidth(), 1920);

  selector->setSelectionOverride(0, std::nullopt);
  EXPECT_FALSE(selector->hasSelectionOverride(0));
}

TEST(DefaultTrackSelectorTest, DoesNotSelectARendererWhoseMediaTypeIsDisabled) {
  std::vector<std::shared_ptr<MediaFormat>> formats{
    createVideoFormat(320, 180, 200000),
    createVideoFormat(1920, 1080, 4000000)
  };
  auto group = std::make_shared<TrackGroup>(MediaType::kMediaTypeVideo, formats);
  auto trackGroups = std::make_shared<TrackGroupArray>(
    std::vector<std::shared_ptr<TrackGroup>>{ group });

  auto parameters = ParametersBuilder()
                    .setTrackTypeDisabled(MediaType::kMediaTypeVideo, true)
                    .build();
  auto selector = std::make_shared<DefaultTrackSelector>(parameters, nullptr);
  std::vector<std::shared_ptr<SelectionCapabilities>> capabilities{
    std::make_shared<FakeRendererCapabilities>(MediaType::kMediaTypeVideo)
  };

  auto result = selector->selectTracks(capabilities, trackGroups);
  ASSERT_NE(result, nullptr);
  // Nothing is selected and the renderer is not enabled.
  EXPECT_EQ(result->getSelection(0), nullptr);
  EXPECT_FALSE(result->isEnabled(0));

  // Enabling the type again makes the selector decide as it did before.
  selector->setParameters(ParametersBuilder(parameters)
                          .setTrackTypeDisabled(MediaType::kMediaTypeVideo, false)
                          .build());
  auto enabled = selector->selectTracks(capabilities, trackGroups);
  ASSERT_NE(enabled, nullptr);
  ASSERT_NE(enabled->getSelection(0), nullptr);
  EXPECT_EQ(enabled->getSelection(0)->getFormat(0)->getWidth(), 1920);
}

TEST(DefaultTrackSelectorTest, DisablesARendererWithoutTouchingItsTracks) {
  auto group = createVideoTrackGroup();
  auto trackGroups = std::make_shared<TrackGroupArray>(
    std::vector<std::shared_ptr<TrackGroup>>{ group });

  auto selector = std::make_shared<DefaultTrackSelector>();
  std::vector<std::shared_ptr<SelectionCapabilities>> capabilities{
    std::make_shared<FakeRendererCapabilities>(MediaType::kMediaTypeVideo)
  };
  ASSERT_FALSE(selector->getParameters().getRendererDisabled(0));

  selector->setRendererDisabled(0, true);
  EXPECT_TRUE(selector->getParameters().getRendererDisabled(0));
  auto disabled = selector->selectTracks(capabilities, trackGroups);
  ASSERT_NE(disabled, nullptr);
  EXPECT_EQ(disabled->getSelection(0), nullptr);
  EXPECT_FALSE(disabled->isEnabled(0));

  selector->setRendererDisabled(0, false);
  EXPECT_FALSE(selector->getParameters().getRendererDisabled(0));
}

TEST(DefaultTrackSelectorTest, PrefersTheLanguageOfTheSystemWhenNothingIsPreferred) {
  std::vector<std::shared_ptr<MediaFormat>> formats{
    createAudioFormat(2, 48000, 256000),
    createAudioFormat(2, 48000, 128000)
  };
  formats[0]->setLanguage("fra");
  formats[1]->setLanguage("deu");
  auto group = std::make_shared<TrackGroup>(MediaType::kMediaTypeAudio, formats);
  auto trackGroups = std::make_shared<TrackGroupArray>(
    std::vector<std::shared_ptr<TrackGroup>>{ group });
  std::vector<std::shared_ptr<SelectionCapabilities>> capabilities{
    std::make_shared<FakeRendererCapabilities>(MediaType::kMediaTypeAudio)
  };

  const auto previousLanguages = SystemLanguage::getLanguageCodes();
  // The system speaks German, so the German track wins over the French one even though the French one
  // has the higher bitrate.
  SystemLanguage::setLanguageCodes({ "deu" });
  auto selector = std::make_shared<DefaultTrackSelector>();
  auto result = selector->selectTracks(capabilities, trackGroups);
  ASSERT_NE(result, nullptr);
  const auto& selection = result->getSelection(0);
  ASSERT_NE(selection, nullptr);
  ASSERT_GT(selection->length(), 0);
  EXPECT_EQ(selection->getFormat(0)->getLanguage(), "deu");

  // An explicit preference of the application beats the language of the system.
  auto preferred = std::make_shared<DefaultTrackSelector>(
    ParametersBuilder().setPreferredAudioLanguage("fra").build(), nullptr);
  auto preferredResult = preferred->selectTracks(capabilities, trackGroups);
  ASSERT_NE(preferredResult, nullptr);
  ASSERT_NE(preferredResult->getSelection(0), nullptr);
  EXPECT_EQ(preferredResult->getSelection(0)->getFormat(0)->getLanguage(), "fra");

  // A system without a known language leaves the choice to the other scores, and the higher bitrate
  // then wins.
  SystemLanguage::setLanguageCodes({});
  auto neutral = std::make_shared<DefaultTrackSelector>();
  auto neutralResult = neutral->selectTracks(capabilities, trackGroups);
  ASSERT_NE(neutralResult, nullptr);
  ASSERT_NE(neutralResult->getSelection(0), nullptr);
  EXPECT_EQ(neutralResult->getSelection(0)->getFormat(0)->getLanguage(), "fra");

  SystemLanguage::setLanguageCodes(previousLanguages);
}

TEST(AdaptiveTrackSelectionTest, ExcludesATrackThatWasReportedAsUnreadable) {
  auto group = createVideoTrackGroup();
  auto meter = std::make_shared<FixedBandwidthMeter>(10000000);
  auto selection = AdaptiveTrackSelection(group, std::vector<int>{ 0, 1, 2 }, meter);
  selection.onPlayWhenReadyChanged(true);
  ASSERT_EQ(selection.getSelectedFormat()->getAverageBitrate(), 4000000);

  // The largest track is reported as unreadable, so it is excluded for a while and the ideal selection
  // falls back to the next one that fits.
  const auto nowMs = SystemClock::CurrentTimeMillis();
  EXPECT_FALSE(selection.isTrackExcluded(2, nowMs));
  selection.maybeBlacklistTrack(/* index= */ 2, /* exclusionDurationMs= */ 60000, nowMs);
  EXPECT_TRUE(selection.isTrackExcluded(2, nowMs));
  EXPECT_FALSE(selection.isTrackExcluded(1, nowMs));
  EXPECT_EQ(selection.determineIdealSelectedIndex(nowMs, /* enableBackBuffer= */ false), 1);

  // The exclusion expires, so the track it names is considered again.
  EXPECT_FALSE(selection.isTrackExcluded(2, nowMs + 60001));
  EXPECT_EQ(selection.determineIdealSelectedIndex(nowMs + 60001, false), 2);

  // A report with no duration does not exclude anything, and it does not shorten an exclusion that is
  // already in force either.
  selection.maybeBlacklistTrack(/* index= */ 0, /* exclusionDurationMs= */ 0, nowMs);
  EXPECT_FALSE(selection.isTrackExcluded(0, nowMs));
  selection.maybeBlacklistTrack(/* index= */ 2, /* exclusionDurationMs= */ 0, nowMs);
  EXPECT_TRUE(selection.isTrackExcluded(2, nowMs));
}

TEST(TrackGroupTest, ReturnsOneIdentifierPerTrack) {
  std::vector<std::shared_ptr<MediaFormat>> formats{
    createVideoFormat(640, 360, 500000),
    createVideoFormat(1280, 720, 1500000)
  };
  formats[0]->setTrackId(7);
  formats[1]->setTrackId(9);
  TrackGroup group(MediaType::kMediaTypeVideo, formats);

  const auto trackIds = group.getTrackIds();
  ASSERT_EQ(trackIds.size(), 2u);
  EXPECT_EQ(trackIds[0], 7);
  EXPECT_EQ(trackIds[1], 9);
}

TEST(TrackGroupTest, ComparesGroupSizesAsWellAsTheirFormats) {
  TrackGroup small(MediaType::kMediaTypeVideo, std::vector<std::shared_ptr<MediaFormat>>{
    createVideoFormat(640, 360, 1)
  });
  TrackGroup larger(MediaType::kMediaTypeVideo, std::vector<std::shared_ptr<MediaFormat>>{
    createVideoFormat(640, 360, 1), createVideoFormat(1280, 720, 2)
  });

  // A group that is a prefix of another is not equal to it.
  EXPECT_FALSE(small == larger);
  EXPECT_TRUE(small != larger);
}

// ==================================================================================================
// The sample queue
// ==================================================================================================

/**
 * Records the media a sample queue gives up.
 */
class RecordingUpstreamDiscardedListener final : public SampleQueue::UpstreamDiscardedListener {
public:
  void onUpstreamDiscarded(const int64_t mediaStartTimeUs, const int64_t mediaEndTimeUs) override {
    ++count;
    startTimeUs = mediaStartTimeUs;
    endTimeUs = mediaEndTimeUs;
  }

  int count = 0;
  int64_t startTimeUs = Definitions::kTimeUnset;
  int64_t endTimeUs = Definitions::kTimeUnset;
};

TEST(SampleQueueTest, ReportsTheMediaItDiscardsFromTheBack) {
  auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, 1024);
  auto queue = std::make_shared<SampleQueue>(allocator, MediaType::kMediaTypeAudio);
  RecordingUpstreamDiscardedListener listener;
  queue->setUpstreamDiscardedListener(&listener);

  // Four samples of one byte each, at one second intervals.
  const uint8_t sample = 0;
  for (auto i = 0; i < 4; ++i) {
    queue->appendSampleData(&sample, 1, i * 1000000, AVBufferFlags::kKeyFrame);
  }
  ASSERT_EQ(queue->getWriteIndex(), 4);

  // Discarding from 2.5 s keeps the samples before that time, so it gives up the sample at 3 s.
  queue->discardUpstreamFrom(2500000);
  EXPECT_EQ(listener.count, 1);
  EXPECT_EQ(listener.startTimeUs, 3000000);
  EXPECT_EQ(listener.endTimeUs, 3000000);
  EXPECT_EQ(queue->getWriteIndex(), 3);

  // There is nothing after the sample that is retained, so a second discard reports nothing.
  queue->discardUpstreamFrom(5000000);
  EXPECT_EQ(listener.count, 1);
  EXPECT_EQ(queue->getWriteIndex(), 3);

  // An empty queue reports nothing either.
  queue->discardUpstreamSamples(queue->getReadIndex());
  ASSERT_EQ(queue->getWriteIndex(), 0);
  queue->discardUpstreamFrom(9000000);
  EXPECT_EQ(listener.count, 1);

  queue->setUpstreamDiscardedListener(nullptr);
}

/**
 * A format is owned by whatever is given it: a consumer that was handed a format keeps it even when the
 * producer that reported it lets go of its own reference.
 *
 * <p>The holder used to keep a {@code weak_ptr}, so a consumer could observe a null format for a format
 * that still existed. The audio renderer dereferenced exactly that null: the crash of
 * {@code DecodeAudioRenderer::onInputFormatChanged} read {@code MediaFormat::mEncoderDelay} through it.
 */
TEST(FormatHolderTest, OwnsTheFormatItIsGiven) {
  FormatHolder holder;
  {
    auto format = createAudioFormat(/* channelCount= */ 2, /* sampleRate= */ 48000, 128000);
    holder.format = format;
  }

  // The only reference left is the one of the holder.
  ASSERT_NE(holder.format, nullptr);
  EXPECT_EQ(holder.format->getSampleRate(), 48000);
  EXPECT_EQ(holder.format->getChannelCount(), 2);

  holder.clear();
  EXPECT_EQ(holder.format, nullptr);
}

TEST(SampleQueueTest, TheFormatGivenToAConsumerOutlivesTheQueue) {
  auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, 1024);
  auto queue = std::make_shared<SampleQueue>(allocator, MediaType::kMediaTypeAudio);
  queue->setFormat(createAudioFormat(2, 48000, 128000));

  const uint8_t sample = 0;
  queue->appendSampleData(&sample, 1, 0, AVBufferFlags::kKeyFrame);

  FormatHolder holder;
  auto buffer = std::make_shared<AVPacketBuffer>(MediaType::kMediaTypeAudio);
  ASSERT_EQ(queue->readSample(holder, buffer.get(), /* formatRequired= */ true, false),
    Definitions::kResultFormatRead);
  ASSERT_NE(holder.format, nullptr);

  // Releasing the queue drops the references the queue held to the format, which is what the player does
  // to the sample queues of a period when it releases it.
  queue->reset(/* resetUpstreamFormat= */ true);

  EXPECT_NE(holder.format, nullptr) << "the consumer lost a format that still exists";
  EXPECT_EQ(holder.format->getSampleRate(), 48000);
}

TEST(SampleQueueTest, NeverReportsAFormatReadWithoutAFormat) {
  auto allocator = std::make_shared<DefaultAllocator>(/* trimOnReset= */ true, 1024);
  auto queue = std::make_shared<SampleQueue>(allocator, MediaType::kMediaTypeAudio);

  // The sample is appended before the queue knows the format of its track, so the queue records no
  // format for it. Reporting that absence as a format read is what made the renderers dereference null.
  const uint8_t sample = 0;
  queue->appendSampleData(&sample, 1, 0, AVBufferFlags::kKeyFrame);
  queue->setFormat(createAudioFormat(2, 48000, 128000));

  FormatHolder holder;
  auto buffer = std::make_shared<AVPacketBuffer>(MediaType::kMediaTypeAudio);
  EXPECT_EQ(queue->readSample(holder, buffer.get(), /* formatRequired= */ true, false),
    Definitions::kResultFormatRead);
  EXPECT_NE(holder.format, nullptr) << "a format read must carry a format";
}

TEST(DefaultTrackSelectorTest, SelectsTheHighestBitrateVideoTrackWithinTheConstraints) {
  std::vector<std::shared_ptr<MediaFormat>> formats{
    createVideoFormat(320, 180, 200000),
    createVideoFormat(1920, 1080, 4000000),
    createVideoFormat(1280, 720, 1500000)
  };
  auto group = std::make_shared<TrackGroup>(MediaType::kMediaTypeVideo, formats);
  auto trackGroups = std::make_shared<TrackGroupArray>(std::vector<std::shared_ptr<TrackGroup>>{ group });

  auto selector = std::make_shared<DefaultTrackSelector>();
  std::vector<std::shared_ptr<SelectionCapabilities>> capabilities{
    std::make_shared<FakeRendererCapabilities>(MediaType::kMediaTypeVideo)
  };
  auto result = selector->selectTracks(capabilities, trackGroups);
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(result->length(), 1u);

  const auto& selection = result->getSelection(0);
  ASSERT_NE(selection, nullptr);
  ASSERT_GT(selection->length(), 0);
  // The chain compares capabilities, then the constraints, then the quality, and stops at the first
  // difference: the 1080p rendition is the one the max constraints of the default parameters allow.
  const auto& selected = selection->getFormat(0);
  ASSERT_NE(selected, nullptr);
  EXPECT_EQ(selected->getWidth(), 1920);
  EXPECT_EQ(selected->getHeight(), 1080);
}

TEST(DefaultTrackSelectorTest, HonoursTheMaximumVideoConstraints) {
  std::vector<std::shared_ptr<MediaFormat>> formats{
    createVideoFormat(320, 180, 200000),
    createVideoFormat(1920, 1080, 4000000),
    createVideoFormat(640, 360, 400000)
  };
  auto group = std::make_shared<TrackGroup>(MediaType::kMediaTypeVideo, formats);
  auto trackGroups = std::make_shared<TrackGroupArray>(std::vector<std::shared_ptr<TrackGroup>>{ group });

  auto parameters = ParametersBuilder().setMaxVideoSize(1280, 720).build();
  auto selector = std::make_shared<DefaultTrackSelector>(parameters, nullptr);
  std::vector<std::shared_ptr<SelectionCapabilities>> capabilities{
    std::make_shared<FakeRendererCapabilities>(MediaType::kMediaTypeVideo)
  };
  auto result = selector->selectTracks(capabilities, trackGroups);
  ASSERT_NE(result, nullptr);
  const auto& selection = result->getSelection(0);
  ASSERT_NE(selection, nullptr);
  ASSERT_GT(selection->length(), 0);
  const auto& selected = selection->getFormat(0);
  ASSERT_NE(selected, nullptr);
  // The 1080p rendition exceeds the maximum size, so the 720p one is the largest that fits.
  EXPECT_EQ(selected->getWidth(), 640);
  EXPECT_EQ(selected->getHeight(), 360);
}

TEST(DefaultTrackSelectorTest, SelectsTheAudioTrackThatScoresHighest) {
  std::vector<std::shared_ptr<MediaFormat>> formats{
    createAudioFormat(2, 44100, 96000),
    createAudioFormat(6, 48000, 256000)
  };
  formats[1]->setLanguage("fra");
  auto group = std::make_shared<TrackGroup>(MediaType::kMediaTypeAudio, formats);
  auto trackGroups = std::make_shared<TrackGroupArray>(std::vector<std::shared_ptr<TrackGroup>>{ group });

  // The preferred language decides before the channel count and the bitrate do.
  auto parameters = ParametersBuilder().setPreferredAudioLanguage("fra").build();
  auto selector = std::make_shared<DefaultTrackSelector>(parameters, nullptr);
  std::vector<std::shared_ptr<SelectionCapabilities>> capabilities{
    std::make_shared<FakeRendererCapabilities>(MediaType::kMediaTypeAudio)
  };
  auto result = selector->selectTracks(capabilities, trackGroups);
  ASSERT_NE(result, nullptr);
  const auto& selection = result->getSelection(0);
  ASSERT_NE(selection, nullptr);
  ASSERT_GT(selection->length(), 0);
  EXPECT_EQ(selection->getFormat(0)->getLanguage(), "fra");
}

} // namespace anonymous
} // namespace coral
