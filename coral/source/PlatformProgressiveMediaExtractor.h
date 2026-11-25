/*
 * Copyright (C) 2023 The Coral Project
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

#ifndef CORAL_PLAYBACK_SOURCE_PLATFORM_PROGRESSIVE_MEDIA_EXTRACTOR_H
#define CORAL_PLAYBACK_SOURCE_PLATFORM_PROGRESSIVE_MEDIA_EXTRACTOR_H

#include <coral/source/ProgressiveMediaExtractor.h>

#include <codec/MediaExtractor.h>

#include <memory>
#include <string>

namespace coral {

/**
 * A progressive media extractor that reads the media with the demuxer of the platform, e.g. the source
 * reader of the media foundation on Windows, the media parser or the NDK extractor of Android.
 *
 * <p>It is the counterpart of {@link DefaultProgressiveMediaExtractor}, which demuxes the media with the
 * built-in extractors of the extractor module: both are driven the same way by the media period, and the
 * platform one is what reads the containers that the built-in extractors did not recognize. The
 * extractor of the platform is an {@link AVExtractor}, so this class only owns it and forwards to it,
 * and the tracks and the samples it produces need no translation.
 *
 * <p>The extractor of the platform is created when the media is sniffed, and it is released by
 * {@link release} or by the destructor. It is created lazily rather than in {@link init}, because
 * {@link DefaultProgressiveMediaExtractor} initializes its fallback for every period: a container that
 * the built-in extractors read must not be opened a second time by the platform.
 *
 * <p>The extractor of the platform opens the media itself, so this one does not read the input of the
 * reader: {@link sniff} opens the media of the url rather than peeking at the input, and {@link read}
 * drives the reader of the platform without moving the input. A caller that measures the progress of an
 * extractor by the position of the input, as {@link ProgressiveMediaPeriod} does, therefore has to know
 * that the media of this extractor is not read through it; the extractor of the platform is meant to
 * read the input of the reader, i.e. through the adapter of the extractor module, before it can be
 * driven exactly like a built-in one.
 */
class PlatformProgressiveMediaExtractor final : public ProgressiveMediaExtractor,
                                                private AVExtractorOutput {
public:
  /**
   * @param url The uri of the media, which the extractor of the platform opens itself.
   */
  explicit PlatformProgressiveMediaExtractor(std::string url) noexcept;

  ~PlatformProgressiveMediaExtractor() override;

  PlatformProgressiveMediaExtractor(const PlatformProgressiveMediaExtractor& other) = delete;

  PlatformProgressiveMediaExtractor& operator=(const PlatformProgressiveMediaExtractor& other) = delete;

  void init(AVExtractorOutput& output) override;

  int sniff(AVExtractorInput& input) override;

  void release() override;

  [[nodiscard]] std::shared_ptr<AVSeekMap> getSeekMap() override;

  [[nodiscard]] AVExtractor* getChosenExtractor() const override;

  [[nodiscard]] int getTrackCount() override;

  bool seek(long position, int64_t timeUs) override;

  AVExtractor::Result read(AVExtractorInput& input) override;

  /**
   * Returns the uri of the media.
   */
  [[nodiscard]] const std::string& getUrl() const { return url_; }

private:
  // ================================================================================================
  // AVExtractorOutput of the extractor of the platform, forwarded to `output_`.
  // ================================================================================================

  std::shared_ptr<AVTrackOutput>
  track(int id, MediaType type, AVTrackOutput::SampleConsumer consumer) override;

  std::shared_ptr<AVTrackOutput> track(int id, AVTrackOutput::SampleConsumer consumer) override;

  void seekMap(std::shared_ptr<AVSeekMap> seekMap) override;

  void endTracks() override;

  std::string url_;

  /** The output of the period, which the tracks of the platform extractor are declared to. */
  AVExtractorOutput* output_ = nullptr;

  /** The extractor of the platform, created when the media is sniffed. */
  std::shared_ptr<AVMediaExtractor> extractor_;

  /** The number of tracks the extractor of the platform declared. */
  int trackCount_ = 0;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_PLATFORM_PROGRESSIVE_MEDIA_EXTRACTOR_H
