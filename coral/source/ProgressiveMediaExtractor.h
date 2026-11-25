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

#ifndef CORAL_PLAYBACK_SOURCE_PROGRESSIVE_MEDIA_EXTRACTOR_H
#define CORAL_PLAYBACK_SOURCE_PROGRESSIVE_MEDIA_EXTRACTOR_H

#include <extractor/Extractor.h>
#include <extractor/ExtractorInput.h>

#include <memory>

namespace coral {

/**
 * Extracts the media of a progressive container.
 *
 * <p>This is the counterpart of ExoPlayer's {@code ProgressiveMediaExtractor}. A progressive
 * container is a single file that holds its samples in the order in which they are played, as
 * opposed to an adaptive stream that is described by a manifest and split into segments: MP4,
 * Matroska, MP3, ADTS, WAV and FLAC are progressive containers.
 *
 * <p>The interface hides two things from the {@link ProgressiveMediaPeriod} that drives it:
 *
 * <ul>
 *   <li>the selection of the {@link AVExtractor} that recognizes the container, which happens in
 *       {@link #sniff};
 *   <li>the fact that a container may be demuxed by an extractor of the module or, in a build that
 *       provides one, by a platform extractor. The period only sees the tracks, the seek map and the
 *       samples.
 * </ul>
 *
 * <p>An instance is created per period, initialized once with {@link #init} and then used from the
 * loading thread of that period. It is not thread safe.
 */
class ProgressiveMediaExtractor {
public:
  virtual ~ProgressiveMediaExtractor() = default;

  ProgressiveMediaExtractor(const ProgressiveMediaExtractor& other) = delete;

  ProgressiveMediaExtractor& operator=(const ProgressiveMediaExtractor& other) = delete;

  /**
   * Initializes the extractor with the output that receives the tracks and the seek map.
   *
   * <p>Called exactly once, before {@link #sniff}.
   *
   * @param output The output of the demuxed media.
   */
  virtual void init(AVExtractorOutput& output) = 0;

  /**
   * Probes the input and initializes the extractor of the container it holds.
   *
   * <p>The position of the input is left untouched, and the caller must rewind it before the first
   * {@link #read}. After a successful sniff, {@link #getChosenExtractor} returns the extractor that
   * was selected and the tracks of the container are declared to the output.
   *
   * @param input The input to probe and to read from afterwards.
   *
   * @return One of the {@code AVExtractorsFactory::kSniffResult*} constants, i.e.
   *         {@code kSniffResultNone} when no extractor recognized the container.
   */
  virtual int sniff(AVExtractorInput& input) = 0;

  /**
   * Releases the resources of the extractor. Called once, when the period is released.
   */
  virtual void release() = 0;

  /**
   * Returns the seek map of the chosen extractor, or null when it is not known yet.
   */
  [[nodiscard]] virtual std::shared_ptr<AVSeekMap> getSeekMap() = 0;

  /**
   * Returns the extractor that was selected by {@link #sniff}, or null when none was.
   */
  [[nodiscard]] virtual AVExtractor* getChosenExtractor() const = 0;

  /**
   * Returns whether {@link #sniff} recognized the container, i.e. whether the media can be read.
   *
   * <p>The default implementation answers from {@link #getChosenExtractor}, which is what an
   * extractor of the module reports. An extractor that demuxes the media without selecting one of
   * those, e.g. the one that drives the extractor of the platform, overrides it: it has no chosen
   * extractor of the module to report, but it did recognize the container, and the caller has to be
   * able to tell that apart from a container that was not recognized at all.
   */
  [[nodiscard]] virtual bool isContainerRecognized() const {
    return getChosenExtractor() != nullptr;
  }

  /**
   * Returns the number of tracks that were discovered so far.
   */
  [[nodiscard]] virtual int getTrackCount() = 0;

  /**
   * Notifies the extractor that the input was repositioned.
   *
   * @param position The new byte position of the input.
   * @param timeUs The time that the position corresponds to, in microseconds.
   *
   * @return Whether the seek could be performed.
   */
  virtual bool seek(long position, int64_t timeUs) = 0;

  /**
   * Extracts the media that starts at the current position of the input.
   *
   * @param input The input to read from. It is the same input that was passed to {@link #sniff}.
   *
   * @return One of the {@link AVExtractor::Result} constants.
   */
  virtual AVExtractor::Result read(AVExtractorInput& input) = 0;

protected:
  ProgressiveMediaExtractor() = default;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_PROGRESSIVE_MEDIA_EXTRACTOR_H
