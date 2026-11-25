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

#ifndef CORAL_PLAYBACK_SOURCE_DEFAULT_PROGRESSIVE_MEDIA_EXTRACTOR_H
#define CORAL_PLAYBACK_SOURCE_DEFAULT_PROGRESSIVE_MEDIA_EXTRACTOR_H

#include <coral/source/ProgressiveMediaExtractor.h>

#include <extractor/Extractor.h>

#include <memory>
#include <vector>

namespace coral {

/**
 * The {@link ProgressiveMediaExtractor} that demuxes a container with the extractors of the
 * extractor module.
 *
 * <p>This is the counterpart of ExoPlayer's {@code DefaultProgressiveMediaExtractor}, which used to
 * be named {@code BundledExtractorsAdapter}. It holds the list of the extractors of the factory,
 * asks them, in order, for the confidence with which they recognize the container, and keeps the one
 * that answered with the highest confidence.
 *
 * <p>Because the chosen extractor declares its tracks while it is initialized, this class is also
 * the {@link AVExtractorOutput} of that extractor: it forwards every call to the output of the
 * period, and records what the period and the interface need, i.e. the seek map and the identifiers
 * of the tracks.
 *
 * <p>A container that none of the extractors of the factory recognizes is handed to the fallback
 * extractor of the media, see {@link #setFallback}: that is what lets the container formats of the
 * module and the ones the platform reads be demuxed by the same period, and it is what makes a
 * container of the platform playable without a second media source.
 */
class DefaultProgressiveMediaExtractor final : public ProgressiveMediaExtractor,
                                               private AVExtractorOutput {
public:
  /**
   * @param extractorsFactory The factory of the extractors that are asked to recognize the
   *                          container. Must not be null.
   */
  explicit DefaultProgressiveMediaExtractor(
      std::unique_ptr<AVExtractorsFactory> extractorsFactory) noexcept;

  ~DefaultProgressiveMediaExtractor() override;

  /**
   * Sets the extractor that reads the containers that the extractors of the module do not recognize.
   *
   * <p>It is the platform extractor of the media: {@link #sniff} selects it when every extractor of the
   * factory answered with {@code AVExtractorsFactory::kSniffResultNone}, and every call is then
   * forwarded to it. The fallback is initialized with the output of the period, but it is only asked to
   * recognize the container after the extractors of the module rejected it, so a fallback that opens the
   * media itself does not touch it while a container of the module is being read.
   *
   * <p>Setting a fallback releases the one it replaces, which may be called at any time: a fallback
   * that is set after {@link #init} is bound to the output of the period immediately.
   *
   * @param fallback The fallback extractor, or null to report an unrecognized container instead.
   */
  void setFallback(std::shared_ptr<ProgressiveMediaExtractor> fallback) noexcept;

  void init(AVExtractorOutput& output) override;

  int sniff(AVExtractorInput& input) override;

  void release() override;

  [[nodiscard]] std::shared_ptr<AVSeekMap> getSeekMap() override;

  [[nodiscard]] AVExtractor* getChosenExtractor() const override;

  [[nodiscard]] bool isContainerRecognized() const override;

  [[nodiscard]] int getTrackCount() override;

  bool seek(long position, int64_t timeUs) override;

  AVExtractor::Result read(AVExtractorInput& input) override;

private:
  // ================================================================================================
  // AVExtractorOutput of the chosen extractor, forwarded to `output`.
  // ================================================================================================

  std::shared_ptr<AVTrackOutput>
  track(int id, MediaType type, AVTrackOutput::SampleConsumer consumer) override;

  std::shared_ptr<AVTrackOutput> track(int id, AVTrackOutput::SampleConsumer consumer) override;

  void seekMap(std::shared_ptr<AVSeekMap> seekMap) override;

  void endTracks() override;

  /**
   * Asks the fallback to recognize the container, and remembers whether it did.
   */
  int sniffFallback(AVExtractorInput& input);

  /**
   * Releases the fallback and forgets that it was selected. Does nothing when it was never
   * initialized.
   */
  void releaseFallback();

  /** The factory of the extractors, used to create them in {@link #init}. */
  std::unique_ptr<AVExtractorsFactory> extractorsFactory;

  /**
   * The extractor that reads the containers that the extractors of the module do not recognize, or
   * null. It is not owned exclusively: {@link #setFallback} hands it over, and the period holds it for
   * as long as this extractor refers to it.
   */
  std::shared_ptr<ProgressiveMediaExtractor> fallback;

  /** Whether the fallback was given the output, i.e. whether it has to be released. */
  bool fallbackInitialized = false;

  /** Whether the fallback is the extractor that {@link #sniff} selected. */
  bool fallbackSelected = false;

  /** The extractors that are asked to recognize the container, in the order of the factory. */
  std::vector<std::unique_ptr<AVExtractor>> extractors;

  /** The extractor that recognized the container, or null when none did. */
  std::unique_ptr<AVExtractor> chosenExtractor;

  /** The output of the period, or null when {@link #init} was not called yet. */
  AVExtractorOutput* output = nullptr;

  /** The seek map that the chosen extractor declared, or null. */
  std::shared_ptr<AVSeekMap> mSeekMap;

  /** The identifiers of the tracks that the chosen extractor declared. */
  std::vector<int> trackIds;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_DEFAULT_PROGRESSIVE_MEDIA_EXTRACTOR_H
