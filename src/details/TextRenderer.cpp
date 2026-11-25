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

#include "TextRenderer.h"

#include <extractor/text/DefaultSubtitleParserFactory.h>

#include <media/PacketBuffer.h>

#include <minilog/log.h>

#include <utility>

namespace coral {

TextRenderer::TextRenderer()
  : BaseRenderer(MediaType::kMediaTypeSubtitle),
    mFactory(createDefaultSubtitleParserFactory()),
    mTimeline(std::make_shared<AVCueTimeline>()) {
}

TextRenderer::~TextRenderer() = default;

void TextRenderer::setCueListener(CueListener listener) {
  mCueListener = std::move(listener);
}

void TextRenderer::setTextOutput(const std::shared_ptr<TextOutput>& output) {
  mTextOutput = output;
}

void TextRenderer::setPrefetchListener(PrefetchListener listener) {
  mPrefetchListener = std::move(listener);
}

SelectionCapabilities::Capabilities
TextRenderer::supportsFormat(const std::shared_ptr<MediaFormat>& format) {

  if (format == nullptr || !format->isSubtitle()) {
    return SelectionCapabilities::create(FORMAT_UNSUPPORTED_TYPE);
  }

  if (mFactory == nullptr || !mFactory->supportsFormat(*format)) {
    // A bitmap subtitle format, or a subtitle format the module does not parse yet: the track is
    // not advertised, so the selector does not pick it and no renderer is enabled for nothing.
    return SelectionCapabilities::create(FORMAT_UNSUPPORTED_SUBTYPE);
  }
  return SelectionCapabilities::create(FORMAT_HANDLED, ADAPTIVE_NOT_SUPPORTED,
    TUNNELING_NOT_SUPPORTED);
}

void TextRenderer::onStreamChanged(const std::vector<std::shared_ptr<MediaFormat>>& formats,
                                   const int64_t startPositionUs, const int64_t offsetUs) {

  static_cast<void>(startPositionUs);
  static_cast<void>(offsetUs);
  mInputEnded = false;
  mOutputEnded = false;
  mHasPending = false;
  mPendingResults.clear();
  mTimeline->clear();
  mHasLastGroup = false;
  mParser.reset();
  mFormat.reset();
  if (!formats.empty() && formats.front() != nullptr) {
    onInputFormatChanged(formats.front());
  }
}

void TextRenderer::onInputFormatChanged(const std::shared_ptr<MediaFormat>& format) {
  mFormat = format;
  mParser.reset();
  if (format == nullptr || mFactory == nullptr || !mFactory->supportsFormat(*format)) {
    return;
  }
  mParser = mFactory->create(*format);
}

void TextRenderer::onPositionReset(const int64_t positionUs, const bool joining) {
  static_cast<void>(joining);
  // The timeline may already hold the cues of the target position; rasterise the first group before the
  // screen is cleared, so the first frame after the seek finds it in the cache.
  if (mPrefetchListener != nullptr) {
    const auto group = mTimeline->cuesAt(positionUs);
    if (!group.isEmpty()) {
      mPrefetchListener(group);
    }
  }
  mInputEnded = false;
  mOutputEnded = false;
  if (mParser != nullptr) {
    mParser->reset();
  }
  mTimeline->clear();
  mHasPending = false;
  mPendingResults.clear();
  mHasLastGroup = false;
  // The cues of the previous position are cleared before the ones of the new position arrive.
  deliver(AVCueGroup(std::vector<AVCue>{}, positionUs));
}

void TextRenderer::onDisabled() {
  clearCues();
}

void TextRenderer::onReset() {
  clearCues();
  mParser.reset();
}

void TextRenderer::clearCues() {
  mTimeline->clear();
  mHasPending = false;
  mPendingResults.clear();
  mHasLastGroup = false;
  mInputEnded = true;
  mOutputEnded = true;
}

bool TextRenderer::isReady() {
  // A text renderer has nothing to prepare: whether a sample is available only decides how much the
  // playback can advance, which the player asks through isSourceReady.
  return true;
}

bool TextRenderer::isEnded() {
  return mOutputEnded;
}

void TextRenderer::handleMessage(int type, const Message* msg) {
  static_cast<void>(type);
  static_cast<void>(msg);
}

void TextRenderer::flushPending(const int64_t endUs) {
  if (!mHasPending) {
    return;
  }
  const auto behavior = mFormat != nullptr ? mFormat->getCueReplacementBehavior()
                                           : CueReplacementBehavior::kMerge;
  const int64_t offsetUs = mFormat != nullptr ? mFormat->getSubsampleOffsetUs() : 0;

  for (const auto& result : mPendingResults) {

    mTimeline->add(result, mPendingStartUs, endUs, behavior, offsetUs);
  }

  mHasPending = false;
  mPendingResults.clear();
}

bool TextRenderer::readSamples() {
  if (mInputEnded) {
    return false;
  }
  if (mFormat == nullptr) {
    auto& formatHolder = getFormatHolder();
    AVPacketBuffer flagsOnly(MediaType::kMediaTypeSubtitle);
    const auto result = readSource(formatHolder, &flagsOnly, true);
    if (result == ReadDataResult::kResultFormatRead) {
      onInputFormatChanged(formatHolder.format);
    } else if (result == ReadDataResult::kResultBufferRead) {
      mInputEnded = true;
      flushPending(Definitions::kTimeUnset);
      mOutputEnded = true;
      return false;
    } else {
      return true;
    }
  }
  if (mParser == nullptr) {
    // The format is not one this renderer parses. The track is not advertised, so this is only
    // reachable when a stream was replaced while it was playing.
    mInputEnded = true;
    mOutputEnded = true;
    return false;
  }

  constexpr int kMaxSamplesPerRender = 100;
  for (int i = 0; i < kMaxSamplesPerRender; ++i) {
    auto packet = std::make_shared<AVPacketBuffer>(MediaType::kMediaTypeSubtitle);
    auto& formatHolder = getFormatHolder();
    const auto result = readSource(formatHolder, packet.get(), false);
    if (result == ReadDataResult::kResultFormatRead) {
      onInputFormatChanged(formatHolder.format);
      packet->release();
      if (mParser == nullptr) {
        mInputEnded = true;
        mOutputEnded = true;
        return false;
      }
      continue;
    }
    if (result == ReadDataResult::kResultNothingRead) {
      packet->release();
      break;
    }
    if (packet->isEndOfStream()) {
      packet->release();
      mInputEnded = true;
      flushPending(Definitions::kTimeUnset);
      mOutputEnded = true;
      break;
    }

    const int64_t sampleStartUs = packet->timeUs();
    std::vector<AVCuesWithTiming> results;
    mParser->parse(packet->array(), packet->size(), AVSubtitleParser::OutputOptions::allCues(),
                   [&results](const AVCuesWithTiming& cues) { results.push_back(cues); });
    packet->release();

    flushPending(sampleStartUs);
    mPendingStartUs = sampleStartUs;
    mPendingResults = std::move(results);
    mHasPending = true;
  }
  if (!mInputEnded && hasReadStreamToEnd()) {
    // The stream ended: its last sample is complete, so the cues that wait for their end time are
    // flushed with the open end that the player replaces when it delivers the next group. The
    // renderer keeps delivering the cues at the positions that follow, until the player marks the
    // stream as final.
    flushPending(Definitions::kTimeUnset);
    mInputEnded = true;
    if (isCurrentStreamFinal()) {
      mOutputEnded = true;
    }
  }
  return !mInputEnded;
}

void TextRenderer::render(const int64_t positionUs, const int64_t elapsedRealtimeUs) {

  static_cast<void>(elapsedRealtimeUs);
  if (mOutputEnded) {
    return;
  }
  readSamples();
  emitCues(positionUs);
}

void TextRenderer::emitCues(const int64_t positionUs) {
  const auto group = mTimeline->cuesAt(positionUs);

  if (mHasLastGroup && group == mLastGroup) {
    return;
  }
  deliver(group);
}

void TextRenderer::deliver(const AVCueGroup& group) {
  mLastGroup = group;
  mHasLastGroup = true;
  if (mCueListener != nullptr) {
    mCueListener(group);
  }
  if (mTextOutput != nullptr) {
    mTextOutput->onCues(group);
  }
}

} // namespace coral
