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

#ifndef CORAL_PLAYBACK_DETAILS_TEXT_RENDERER_H
#define CORAL_PLAYBACK_DETAILS_TEXT_RENDERER_H

#include "BaseRenderer.h"

#include <extractor/text/CueGroup.h>
#include <extractor/text/CueTimeline.h>
#include <extractor/text/CuesWithTiming.h>
#include <extractor/text/SubtitleParser.h>

#include <coral/playback/TextOutput.h>

#include <media/Definitions.h>

#include <functional>
#include <memory>
#include <vector>

namespace coral {

/**
 * The renderer of a subtitle track.
 *
 * <p>This is the counterpart of ExoPlayer's {@code TextRenderer}: it reads the encoded samples of
 * the selected subtitle track, parses them with the subtitle parser of the format as they are read,
 * places the cues on an {@link AVCueTimeline}, and hands the group that is shown at the playback
 * position to its listener and to the {@link TextOutput} of the player. A new group is delivered
 * only when the set of shown cues changes, and the empty group clears the screen on a seek or when
 * the track is disabled.
 */
class TextRenderer final : public BaseRenderer {
public:
  using CueListener = std::function<void(const AVCueGroup&)>;
  using PrefetchListener = std::function<void(const AVCueGroup&)>;

  TextRenderer();

  ~TextRenderer() override;

  /** Sets the receiver of the groups, which the player forwards to its own listeners. */
  void setCueListener(CueListener listener);

  /** Sets the receiver of the group that follows the delivered one, rendered ahead of its time. */
  void setPrefetchListener(PrefetchListener listener);

  /** Sets the application output, or clears it with null. */
  void setTextOutput(const std::shared_ptr<TextOutput>& output);

  void render(int64_t positionUs, int64_t elapsedRealtimeUs) override;

  bool isReady() override;

  bool isEnded() override;

  void handleMessage(int type, const Message* msg) override;

  [[nodiscard]] const char* getName() const override { return "TextRenderer"; }

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

  /** Reads and parses the samples the stream has ready; false when the input ended. */
  bool readSamples();

  /** Adds the cues of the sample that waits for its end time, and forgets it. */
  void flushPending(int64_t endUs);

  /** Delivers the group shown at @p positionUs when it differs from the last one. */
  void emitCues(int64_t positionUs);

  void deliver(const AVCueGroup& group);

  /** Clears the timeline and the pending sample, delivering the empty group. */
  void clearCues();

  std::unique_ptr<AVSubtitleParserFactory> mFactory;
  std::unique_ptr<AVSubtitleParser> mParser;
  std::shared_ptr<MediaFormat> mFormat;
  std::shared_ptr<AVCueTimeline> mTimeline;
  std::shared_ptr<TextOutput> mTextOutput;
  CueListener mCueListener;
  PrefetchListener mPrefetchListener;
  bool mInputEnded = false;
  bool mOutputEnded = false;
  bool mDisableOnUnsupportedFormat = false;
  bool mHasLastGroup = false;
  AVCueGroup mLastGroup;
  std::vector<AVCuesWithTiming> mPendingResults;
  int64_t mPendingStartUs = 0;
  /** The end time the parsed document states for its cues, or {@code kTimeUnset} when it has none. */
  int64_t mPendingEndUs = Definitions::kTimeUnset;
  bool mHasPending = false;
};

} // namespace coral

#endif //CORAL_PLAYBACK_DETAILS_TEXT_RENDERER_H
