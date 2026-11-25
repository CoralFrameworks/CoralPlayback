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

#ifndef CORAL_PLAYBACK_DETAILS_METADATA_RENDERER_H
#define CORAL_PLAYBACK_DETAILS_METADATA_RENDERER_H

#include "BaseRenderer.h"

#include <coral/playback/MetadataOutput.h>

#include <extractor/metadata/MetadataDecoder.h>

#include <deque>
#include <functional>
#include <memory>

namespace coral {

/**
 * The renderer of a timed metadata track.
 *
 * <p>This is the counterpart of ExoPlayer's {@code MetadataRenderer}: it reads the samples of the
 * selected metadata track, decodes them with the decoder that their format selects (ID3, {@code emsg}
 * or SCTE-35), and hands each decoded {@link Metadata} to its listener and to the
 * {@link MetadataOutput} of the player when the playback position reaches the time of the sample.
 */
class MetadataRenderer final : public BaseRenderer {
public:
  using MetadataListener = std::function<void(const Metadata&)>;

  MetadataRenderer();

  ~MetadataRenderer() override;

  void setMetadataListener(MetadataListener listener);

  void setMetadataOutput(const std::shared_ptr<MetadataOutput>& output);

  void render(int64_t positionUs, int64_t elapsedRealtimeUs) override;

  bool isReady() override;

  bool isEnded() override;

  void handleMessage(int type, const Message* msg) override;

  [[nodiscard]] const char* getName() const override { return "MetadataRenderer"; }

  SelectionCapabilities::Capabilities
  supportsFormat(const std::shared_ptr<MediaFormat>& format) override;

  SelectionCapabilities::AdaptiveSupport supportsMixedMimeTypeAdaptation() override {
    return ADAPTIVE_NOT_SUPPORTED;
  }

protected:
  void onStreamChanged(const std::vector<std::shared_ptr<MediaFormat>>& formats,
    int64_t startPositionUs,
    int64_t offsetUs) override;

  void onPositionReset(int64_t positionUs, bool joining) override;

  void onDisabled() override;

  void onReset() override;

private:
  void onInputFormatChanged(const std::shared_ptr<MediaFormat>& format);

  bool readSamples();

  void deliverReadyMetadata(int64_t positionUs);

  void clearQueue();

  std::unique_ptr<AVMetadataDecoderFactory> mFactory;
  std::unique_ptr<AVMetadataDecoder> mDecoder;
  std::shared_ptr<MediaFormat> mFormat;
  std::shared_ptr<MetadataOutput> mMetadataOutput;
  MetadataListener mMetadataListener;
  std::deque<Metadata> mPending;
  bool mInputEnded = false;
};

} // namespace coral

#endif //CORAL_PLAYBACK_DETAILS_METADATA_RENDERER_H
