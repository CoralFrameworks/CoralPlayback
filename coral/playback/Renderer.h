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

#ifndef CORAL_PLAYBACK_RENDERER_H
#define CORAL_PLAYBACK_RENDERER_H

#include <coral/playback/MediaClock.h>
#include <coral/selection/Configuration.h>
#include <coral/selection/SelectionCapabilities.h>
#include <coral/selection/TrackSelection.h>
#include <coral/source/SampleStream.h>

#include <core/event/Message.h>
#include <media/Format.h>

#include <memory>
#include <vector>

namespace coral {

/**
 * The interface for rendering audio/video data
 */
class Renderer {
public:
  virtual ~Renderer() = default;

  /**
   * Some renderers can signal when {@link render(long, long)} should be called.
   *
   * That allows the player to sleep until the next wakeup, instead of calling {@link
   * #render(long, long)} in a tight loop. The aim of this interrupt based scheduling is to save
   * power.
   */
  class WakeupListener {
  public:
    virtual ~WakeupListener() = default;

    /**
     * The renderer no longer needs to render until the next wakeup.
     *
     * Must be called from the thread ExoPlayer invokes the renderer from.
     *
     * @param wakeupDeadlineMs Maximum time in milliseconds until {@link onWakeup()} will be called.
     */
    virtual void onSleep(int64_t wakeupDeadlineMs) = 0;

    /**
     * The renderer needs to render some frames. The client should call {@link render(long, long)}
     * at its earliest convenience.
     *
     * Can be called from any thread.
     */
    virtual void onWakeup() = 0;
  };

  /**
   * A listener of the size of the video a renderer decodes.
   *
   * <p>Mirrors ExoPlayer's {@code VideoSize}: the size is the one of the decoded frames, so it can
   * differ from the width and the height of the format. It is reported by the video renderers and
   * read by the player so that an application can lay the video out.
   */
  class VideoSizeListener {
  public:
    virtual ~VideoSizeListener() = default;

    /**
     * Called when the size of the decoded video changes.
     *
     * @param width The width of the video in pixels, or 0 when there is none.
     * @param height The height of the video in pixels, or 0 when there is none.
     */
    virtual void onVideoSizeChanged(int width, int height) = 0;
  };

  /**
   * The type of a message that can be passed to an audio renderer
   */
  static constexpr int MSG_SET_VOLUME = 0;

  /**
   * The type of a message that can be passed to a video renderer
   */
  static constexpr int MSG_SET_SURFACE = 1;

  /**
   * The type of a message that can be passed to a video renderer.
   */
  static constexpr int MSG_SET_SCALING_MODE = 3;

  enum class Status : uint8_t {
    //!< The Renderer is disabled
    kStatusDisabled = 0,
    //!< The Renderer is enabled but not has started
    kStatusEnabled = 1,
    //!< The Renderer is started and rendering
    kStatusRendering = 2,
  };

  /**
   * Sets the WakeupListener
   *
   * @param listener the specific WakeupListener
   */
  virtual void setWakeupListener(const std::shared_ptr<WakeupListener>& listener) {};

  /**
   * Sets the listener of the size of the video this renderer decodes.
   *
   * <p>Called on the player's thread. A renderer that does not decode video ignores it.
   *
   * @param listener The listener, or null to stop reporting the size.
   */
  virtual void setVideoSizeListener(const std::shared_ptr<VideoSizeListener>& listener) {};

  /**
   * Sets the index of this renderer within the player.
   *
   * @param i The renderer index.
   */
  virtual void setIndex(int i) = 0;

  /**
   * Enables the renderer to consume from the specified {@link SampleStream}.
   *
   * This method may be called when the renderer is in the following states: {@link kStatusDisabled}.
   *
   * @param configuration The renderer configuration.
   *
   * @param trackSelection The track selection.
   *
   * @param positionUs The player's current position.
   *
   * @param joining Whether this renderer is being enabled to join an ongoing playback.
   *
   * @param mayRenderStartOfStream Whether this renderer is allowed to render
   * the start of the stream even if the state is not {@link kStatusRendering} yet.
   *
   * @param startPositionUs The start position of the stream in renderer time (microseconds).
   *
   * @param offsetUs The offset to be added to timestamps of buffers before they are rendered.
   */
  virtual void enable(const std::shared_ptr<Configuration>& configuration,
      const std::shared_ptr<TrackSelection>& trackSelection,
      const std::shared_ptr<SampleStream>& stream,
      int64_t positionUs,
      bool joining,
      bool mayRenderStartOfStream,
      int64_t startPositionUs,
      int64_t offsetUs) = 0;

  /**
   * Replaces the {@link SampleStream} from which samples will be consumed.
   *
   * This method may be called when the renderer is in the following states:
   * {@link kStatusEnabled}, {@link kStatusRendering}.
   *
   * @param formats The enabled formats.
   *
   * @param newStream The {@link SampleStream} from which the renderer should consume.
   *
   * @param startPositionUs The start position of the new stream in renderer time (microseconds).
   *
   * @param offsetUs The offset to be added to timestamps of buffers read from
   * {@code newStream} before they are rendered.
   */
  virtual void replaceStream(const std::vector<std::shared_ptr<MediaFormat>>& formats,
      const std::shared_ptr<SampleStream>& newStream,
      int64_t startPositionUs, int64_t offsetUs) = 0;

  /**
   * Returns the packet stream of the specified playback track
   */
  [[nodiscard]] virtual const std::shared_ptr<SampleStream>& getStream() const = 0;

  /**
   * Starts the Renderer
   */
  virtual void start() = 0;

  /**
   * Stops the renderer
   */
  virtual void stop() = 0;

  /**
   * Disable the renderer
   */
  virtual void disable() = 0;

  /**
   * Forces the renderer to give up any resources (e.g. media decoders) that it may be holding. If
   * the renderer is not holding any resources, the call is a no-op.
   *
   * This method may be called when the renderer is in the following states: {@link kStatusDisabled}.
   */
  virtual void reset() = 0;

  /**
   * Returns whether the renderer has read to the end.
   * Maybe any samples is still remaining.
   */
  [[nodiscard]] virtual bool hasReadStreamToEnd() const = 0;

  /**
   * Returns the renderer time up to which the renderer has read samples from the current
   * {@link SampleStream}, in microseconds, or {@link Definitions::kTimeEndOfSource}
   * if the renderer has read the current {@link SampleStream} to the end.
   */
  virtual int64_t getReadingPositionUs() = 0;

  /**
   * Signals to the renderer will be the final one supplied before it is next
   * disabled or reset.
   * This method may be called when the renderer is in the following states:
   * {@link kStatusEnabled}, {@link kStatusRendering}.
   */
  virtual void setCurrentStreamFinal() = 0;

  /**
   * Returns whether the stream will be the final one supplied before
   * the stream is next disabled or reset.
   */
  virtual bool isCurrentStreamFinal() = 0;

  /**
   * Throws an error that's preventing the renderer from reading from its {@link
   * SampleStream}. Does nothing if no such error exists. <p> This method may be
   * called when the renderer is in the following states:
   * {@link kStatusEnabled}, {@link kStatusRendering}.
   *
   * @throws IOException An error that's preventing the renderer from making
   * progress or buffering more data.
   */
  virtual void maybeThrowStreamError() {}

  /**
   * Signals to the renderer that a position discontinuity has occurred.
   *
   * @param positionUs The new playback position
   */
  virtual void resetPosition(int64_t positionUs) = 0;

  /**
   * Enables the renderer to consume
   *
   * @param sampleProvider    The provider for the data to read
   * @param positionUs        The current media time
   * @param elapsedRealtimeUs System clock elapsedRealtime
   */
  virtual void render(int64_t positionUs, int64_t elapsedRealtimeUs) = 0;

  /**
   * Handles a message delivered to the target.
   *
   * @param type The message type.
   * @param msg The message payload.
   */
  virtual void handleMessage(int type, const Message* msg) = 0;

  /**
   * Sets the operating rate of this renderer, where 1 is the default rate, 2 is
   * twice the default rate, 0.5 is half the default rate and so on. The
   * operating rate is a hint to the renderer of the speed at which playback
   * will proceed, and may be used for resource planning. The default
   * implementation is a no-op.
   *
   * @param operatingRate The operating rate.
   */
  virtual void setOperatingRate(float operatingRate) {}

  /**
   * Whether the renderer is able to immediately process media from the current
   * position <p> If the renderer is in the {@link kStatusRendering} state then
   * returning true indicates that the renderer has everything that it needs to
   * continue playback. Returning false indicates that the player should pause
   * until the renderer is ready. <p> If the renderer is in the {@link
   * #kStatusDisabled} state then returning true indicates that the renderer is
   * ready for playback to be started. Returning false indicates that it is not.
   * <p>
   * This method may be called when the renderer is in the following states:
   * {@link kStatusDisabled}, {@link kStatusRendering}.
   *
   * @return Whether the renderer is ready to render media.
   */
  virtual bool isReady() = 0;

  /**
   * Returns whether the renderer is ended.
   */
  virtual bool isEnded() = 0;

  /**
   * Returns the media type of the playback
   */
  [[nodiscard]] virtual MediaType getMediaType() const = 0;

  /**
   * Returns the capabilities of the renderer.
   *
   * @return The capabilities of the renderer.
   */
  virtual std::shared_ptr<SelectionCapabilities> getCapabilities() = 0;

  /**
   * If the renderer advances its own playback position then this method returns
   * a corresponding {@link MediaClock}. If provided, the player will use the returned
   * {@link MediaClock} as its source of time during playback. A player may have at most
   * one renderer that returns a {@link MediaClock} from this method.
   *
   * @return  The {@link MediaClock} tracking the playback position of the renderer, or nullptr.
   */
  virtual MediaClock* getMediaClock() { return nullptr; }

  /**
   * Returns the current state of the renderer
   *
   * @return The current state. One of {@link kStatusDisabled}, {@link kStatusDisabled} or {@link kStatusRendering}.
   */
  [[nodiscard]] virtual Status getStatus() const = 0;

  /**
   * Returns the name of the renderer
   */
  [[nodiscard]] virtual const char* getName() const {
    return "Renderer";
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_RENDERER_H
