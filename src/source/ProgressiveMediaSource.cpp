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

#include <coral/source/ProgressiveMediaSource.h>

#include <coral/source/DefaultProgressiveMediaExtractor.h>
#include <coral/source/ForwardingTimeline.h>
#include <coral/source/PlatformProgressiveMediaExtractor.h>
#include <coral/source/SinglePeriodTimeline.h>

#include <extractor/DefaultExtractorsFactory.h>

#include <media/Definitions.h>
#include <media/Types.h>

#include <utility>

namespace coral {

namespace {

/**
 * The timeline of a media whose duration, seekability and liveness are not known yet.
 *
 * <p>The window is flagged as a placeholder, which tells the player that the position and the
 * duration it reports are not final.
 */
class ProgressiveTimeline final : public ForwardingTimeline {
public:
  explicit ProgressiveTimeline(const std::shared_ptr<Timeline>& timeline) noexcept
      : ForwardingTimeline(timeline) {
  }

  std::shared_ptr<Window> getWindow(int windowIndex,
      const std::shared_ptr<Window>& window,
      int64_t defaultPositionProjectionUs) override;
};

std::shared_ptr<Timeline::Window> ProgressiveTimeline::getWindow(int windowIndex,
    const std::shared_ptr<Window>& window,
    const int64_t defaultPositionProjectionUs) {
  ForwardingTimeline::getWindow(windowIndex, window, defaultPositionProjectionUs);
  window->isPlaceholder = true;
  return window;
}

} // namespace anonymous

//--------------------------------------------------------------------------------------------------

ProgressiveMediaSource::ProgressiveMediaSource(const std::shared_ptr<MediaItem>& mediaItem,
    ProgressiveMediaPeriod::DataSourceFactory dataSourceFactory,
    ExtractorFactory extractorFactory) noexcept
    : mediaItem(mediaItem),
      inputUrl(mediaItem != nullptr ? mediaItem->getUrl() : std::string()),
      dataSourceFactory(std::move(dataSourceFactory)),
      extractorFactory(std::move(extractorFactory)),
      timelineIsPlaceholder(true),
      timelineDurationUs(Definitions::kTimeUnset),
      timelineIsSeekable(false),
      timelineIsLive(false) {
  // The placeholder timeline is known from the start, so it is built here: `getInitialTimeline` can
  // be read before the source is prepared, by the masking source that wraps it.
  initialTimeline = createTimeline();
}

ProgressiveMediaSource::ExtractorFactory
ProgressiveMediaSource::createDefaultExtractorFactory(const std::string& url) {
  // The container type is inferred from the url so that the matching extractor is asked to sniff
  // the media first, which matters for the formats whose signature is ambiguous.
  const int fileType = FileTypes::inferFileTypeFromUrl(url);
  return [url, fileType]() -> std::shared_ptr<ProgressiveMediaExtractor> {
#if defined(WINDOWS) || defined(WIN32)
    // The Media Foundation source reader handles the codecs that an AVI file commonly carries,
    // including MPEG-4 video and MP3 audio, while the decoders of the module only expose the
    // containers they were implemented for. Reading AVI through the platform keeps the audio and
    // the video of the file on the path that the platform supports.
    if (fileType == FileTypes::AVI) {
      return std::make_shared<PlatformProgressiveMediaExtractor>(url);
    }
#endif
    auto extractor = std::make_shared<DefaultProgressiveMediaExtractor>(
        AVExtractorsFactory::createDefault(fileType));
    // A container that none of the extractors of the module recognizes is read by the extractor of
    // the platform, which opens the media itself. Both are driven the same way by the period, so the
    // container formats of the module and the ones of the platform share a single media source.
    extractor->setFallback(std::make_shared<PlatformProgressiveMediaExtractor>(url));
    return extractor;
  };
}

void ProgressiveMediaSource::prepareSourceInternal() {
  if (extractorFactory == nullptr) {
    extractorFactory = createDefaultExtractorFactory(inputUrl);
  }
  // The duration, the seekability and the liveness of the media are only known once its period
  // parsed its header, so the placeholder timeline is what is reported until then.
  notifySourceInfoRefreshed();
}

void ProgressiveMediaSource::releaseSourceInternal() {
  // A period the caller did not release keeps its loading task and loadable alive, and the loadable
  // reports back into this source. Releasing the remaining periods here means the queued callbacks
  // that run after this method see a released period instead of a destroyed listener.
  for (const auto& weakPeriod: createdPeriods) {
    if (const auto period = weakPeriod.lock(); period != nullptr) {
      if (auto progressive = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(period);
          progressive != nullptr) {
        progressive->release();
      }
    }
  }
  createdPeriods.clear();
}

std::shared_ptr<MediaPeriod> ProgressiveMediaSource::createPeriod(MediaPeriodId id,
    const std::shared_ptr<Allocator>& allocator,
    int64_t startPositionUs) {
  static_cast<void>(startPositionUs);
  // The extractor keeps the state of the demuxing, and it is initialized with the output of one
  // period, so every period gets its own.
  auto extractor = extractorFactory != nullptr ? extractorFactory() : nullptr;
  // The dispatcher is bound to the period, so that the events it reports carry the identifier of
  // the period they belong to.
  const int64_t imageDurationMs = mediaItem != nullptr ? mediaItem->getImageDurationMs()
                                                        : Definitions::kTimeUnset;
  const int64_t imageDurationUs =
      imageDurationMs > 0 ? imageDurationMs * 1000 : Definitions::kTimeUnset;
  auto period = std::make_shared<ProgressiveMediaPeriod>(inputUrl,
      std::move(extractor),
      this,
      allocator,
      dataSourceFactory,
      createEventDispatcher(id),
      loadErrorHandlingPolicy,
      imageDurationUs);
  createdPeriods.push_back(period);
  return period;
}

void ProgressiveMediaSource::releasePeriod(const std::shared_ptr<MediaPeriod>& mediaPeriod) {
  for (auto it = createdPeriods.begin(); it != createdPeriods.end();) {
    const auto period = it->lock();
    if (period == nullptr || period == mediaPeriod) {
      it = createdPeriods.erase(it);
    } else {
      ++it;
    }
  }
  auto progressivePeriod = std::dynamic_pointer_cast<ProgressiveMediaPeriod>(mediaPeriod);
  if (progressivePeriod != nullptr) {
    progressivePeriod->release();
  }
}

void ProgressiveMediaSource::onSourceInfoRefreshed(const int64_t durationUs,
    const bool isSeekable,
    const bool isLive) {
  // The period reports an unknown duration while it is being prepared and once it is prepared
  // without a declared duration: the duration that is already known is kept in that case.
  const auto resolvedDurationUs =
      durationUs == Definitions::kTimeUnset ? timelineDurationUs : durationUs;
  if (!timelineIsPlaceholder && timelineDurationUs == resolvedDurationUs &&
      timelineIsSeekable == isSeekable && timelineIsLive == isLive) {
    // Suppress no-op source info changes.
    return;
  }
  timelineDurationUs = resolvedDurationUs;
  timelineIsSeekable = isSeekable;
  timelineIsLive = isLive;
  timelineIsPlaceholder = false;
  notifySourceInfoRefreshed();
}

void ProgressiveMediaSource::notifySourceInfoRefreshed() {
  refreshSourceInfo(createTimeline());
}

std::shared_ptr<Timeline> ProgressiveMediaSource::createTimeline() const {
  std::shared_ptr<Timeline> timeline = std::make_shared<SinglePeriodTimeline>(
      timelineDurationUs,
      timelineIsSeekable,
      /* isDynamic= */ false,
      /* isLive= */ timelineIsLive,
      mediaItem);
  if (timelineIsPlaceholder) {
    timeline = std::make_shared<ProgressiveTimeline>(timeline);
  }
  return timeline;
}

} // namespace coral
