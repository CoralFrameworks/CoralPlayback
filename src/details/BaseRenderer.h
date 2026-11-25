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

#ifndef CORAL_PLAYBACK_DETAILS_BASE_RENDERER_H
#define CORAL_PLAYBACK_DETAILS_BASE_RENDERER_H

#include <coral/playback/Renderer.h>

#include <coral/selection/SelectionCapabilities.h>
#include <media/Format.h>

namespace coral {

/**
 * The base renderer for specified playback track
 */
class BaseRenderer : public Renderer,
                     public SelectionCapabilities,
                     public std::enable_shared_from_this<SelectionCapabilities> {
public:
  explicit BaseRenderer(MediaType trackType) noexcept;

  void enable(const std::shared_ptr<Configuration>& configuration,
    const std::shared_ptr<TrackSelection>& trackSelection,
    const std::shared_ptr<SampleStream>& stream,
    int64_t positionUs,
    bool joining,
    bool mayRenderStartOfStream,
    int64_t startPositionUs,
    int64_t offsetUs) override;

  void start() override;

  void replaceStream(const std::vector<std::shared_ptr<MediaFormat>>& formats,
    const std::shared_ptr<SampleStream>& newStream,
    int64_t startPositionUs,
    int64_t offsetUs) override;

  const std::shared_ptr<SampleStream>& getStream() const override {
    return stream;
  }

  bool hasReadStreamToEnd() const override {
    return readingPositionUs == Definitions::kTimeEndOfSource;
  }

  int64_t getReadingPositionUs() override { return readingPositionUs; }

  void setCurrentStreamFinal() override { streamIsFinal = true; }

  bool isCurrentStreamFinal() override { return streamIsFinal; }

  void maybeThrowStreamError() override;

  void resetPosition(int64_t positionUs) override;

  void stop() override;

  void disable() override;

  void reset() override;

  MediaType getMediaType() const override { return mediaType; }

  std::shared_ptr<SelectionCapabilities> getCapabilities() override;

  void setIndex(int i) override {
    this->index = i;
  }

  Status getStatus() const override {
    return status;
  }

protected:
  /**
   * Called when the renderer is enabled.
   *
   * The default implementation is a no-op.
   *
   * @param joining Whether this renderer is being enabled to join an ongoing playback.
   * @param mayRenderStartOfStream Whether this renderer is allowed to render the start of the
   *     stream even if the state is not {@link #kStatusRendering} yet.
   */
  virtual void onEnabled(bool joining, bool mayRenderStartOfStream) {
    // Do nothing.
  }

  /**
   * Called when the renderer's stream has changed. This occurs when the renderer is enabled after
   * {@link #onEnabled(boolean, boolean)} has been called, and also when the stream has been
   * replaced whilst the renderer is enabled or started.
   *
   * The default implementation is a no-op.
   *
   * @param formats The enabled formats.
   * @param startPositionUs The start position of the new stream in renderer time (microseconds).
   * @param offsetUs The offset that will be added to the timestamps of buffers read via {@link
   *     #readSource} so that decoder input buffers have monotonically increasing timestamps.
   */
  virtual void onStreamChanged(const std::vector<std::shared_ptr<MediaFormat>>& formats,
    int64_t startPositionUs,
    int64_t offsetUs) {
    // Do nothing.
  }

  /**
   * Called when the position is reset. This occurs when the renderer is enabled after {@link
   * #onStreamChanged(Format[], long, long)} has been called, and also when a position
   * discontinuity is encountered.
   *
   * After a position reset, the renderer's {@link SampleStream} is guaranteed to provide samples
   * starting from a key frame.
   *
   * The default implementation is a no-op.
   *
   * @param positionUs The new playback position in microseconds.
   * @param joining Whether this renderer is being enabled to join an ongoing playback.
   */
  virtual void onPositionReset(int64_t positionUs, bool joining) {
    // Do nothing.
  }

  /**
   * Called when the renderer is started.
   *
   * The default implementation is a no-op.
   */
  virtual void onStarted() {
    // Do nothing.
  }

  /**
   * Called when the renderer is stopped.
   *
   * The default implementation is a no-op.
   */
  virtual void onStopped() {
    // Do nothing.
  }

  /**
   * Called when the renderer is disabled.
   *
   * The default implementation is a no-op.
   */
  virtual void onDisabled() {
    // Do nothing.
  }

  /**
   * Called when the renderer is reset.
   *
   * The default implementation is a no-op.
   */
  virtual void onReset() {
    // Do nothing.
  }

  /**
   * Returns the position passed to the most recent call to {@link #enable} or {@link
   * #resetPosition}.
   */
  int64_t getLastResetPositionUs() const { return lastResetPositionUs; }

  FormatHolder& getFormatHolder();

  /**
   * Returns the formats of the currently enabled stream.
   *
   * This method may be called when the renderer is in the following states: {@link
   * #kStatusEnabled}, {@link #kStatusRendering}.
   */
  const std::vector<std::shared_ptr<MediaFormat>>& getStreamFormats() const {
    return streamFormats;
  }

  /**
   * Returns the configuration set when the renderer was most recently enabled.
   *
   * This method may be called when the renderer is in the following states: {@link
   * #kStatusEnabled}, {@link #kStatusRendering}.
   */
  const std::shared_ptr<Configuration>& getConfiguration() const {
    return configuration;
  }

  /**
   * Returns the index of the renderer within the player.
   */
  int getIndex() const {
    return index;
  }

  using ReadDataResult = SampleStream::ReadDataResult;

  /**
   * Reads from the enabled upstream source. If the upstream source has been
   * read to the end then
   * {@link ReadDataResult::kResultBufferRead} is only returned if {@link
   * #setCurrentStreamFinal()} has been called. {@link
   * ReadDataResult::kResultNothingRead} is returned otherwise.
   *
   * This method may be called when the renderer is in the following states:
   *  {@link State::kStateEnabled}, {@link State::kStateStarted}
   *
   * @param formatHolder A {@link FormatHolder} to populate in the case of
   * reading a format.
   * @param buffer  A {@link PacketBuffer} to populate in the case of reading a
   * sample or the end of the stream. If the end of the stream has been reached,
   * the {@link Flag::BUFFER_FLAG_END_OF_STREAM} flag will be set on the buffer.
   * @param formatRequired Whether the caller requires that the format of the
   * stream be read even if it's not changing. A sample will never be read if
   * set to true, however it is still possible for the end of stream or nothing
   * to be read.
   * @return The status of read, one of {@link SampleStream::ReadDataResult}.
   */
  ReadDataResult
  readSource(FormatHolder& formatHolder, AVPacketBuffer* buffer, bool formatRequired);

  /**
   * Attempts to skip to the keyframe before the specified position, or to the
   * end of the stream if {@code position} is beyond it.
   *
   * @param positionUs The position in microseconds.
   * @return The number of samples that were skipped.
   */
  int skipSource(int64_t positionUs) const;

  /**
   * Returns whether the packet stream is ready.
   *
   * This method may be called when the renderer is in the following states:
   * {@link Status::kStatusEnabled}, {@link Status::kStatusRendering}
   */
  bool isSourceReady() const;

private:
  static std::vector<std::shared_ptr<MediaFormat>>
  getFormats(const std::shared_ptr<TrackSelection>& trackSelection);

  MediaType mediaType;
  FormatHolder formatHolder;
  std::shared_ptr<Configuration> configuration;
  int index;
  Status status;
  std::shared_ptr<SampleStream> stream;
  std::vector<std::shared_ptr<MediaFormat>> streamFormats;
  int64_t streamOffsetUs;
  int64_t lastResetPositionUs;
  int64_t readingPositionUs;
  bool streamIsFinal;
};

} // namespace coral

#endif // CORAL_PLAYBACK_DETAILS_BASE_RENDERER_H
