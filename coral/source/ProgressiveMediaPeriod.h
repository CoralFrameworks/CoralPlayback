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

#ifndef CORAL_PLAYBACK_SOURCE_PROGRESSIVE_MEDIA_PERIOD_H
#define CORAL_PLAYBACK_SOURCE_PROGRESSIVE_MEDIA_PERIOD_H

#include <coral/selection/TrackGroup.h>
#include <coral/source/BaseMediaSource.h>
#include <coral/source/MediaLoadData.h>
#include <coral/source/MediaPeriod.h>
#include <coral/source/ProgressiveMediaExtractor.h>
#include <coral/source/SampleQueue.h>
#include <coral/source/SampleStream.h>
#include <coral/upstream/Allocator.h>
#include <coral/upstream/Loader.h>

#include <core/task/ConditionVariable.h>
#include <core/task/Mutex.h>

#include <extractor/DataBuffer.h>
#include <datasource/DataSource.h>
#include <extractor/DefaultExtractorInput.h>
#include <extractor/Extractor.h>

#include <coral/upstream/DefaultLoadErrorHandlingPolicy.h>
#include <coral/upstream/LoadErrorHandlingPolicy.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace coral {

/**
 * Loads the media of a progressive container and makes it available to the renderers.
 *
 * <p>This is the counterpart of ExoPlayer's {@code ProgressiveMediaPeriod}. It owns a
 * {@link ProgressiveMediaExtractor} that demuxes the container, one {@link SampleQueue} per track
 * that the extractor declares, and a {@link Loader} that runs the extraction on a background
 * thread.
 *
 * <h3>The loading loop</h3>
 *
 * The period does not pull samples: the loader pushes them into the sample queues.
 * {@link #prepare} opens the load condition and starts a load; the load reads the container until it
 * either reaches the end of the media or has read
 * {@link #DEFAULT_LOADING_CHECK_INTERVAL_BYTES} bytes. In the second case it closes the load
 * condition, which parks the loader thread, and asks its callback for permission to continue: the
 * playback thread answers by calling {@link #continueLoading}, which is where the {@code LoadControl}
 * of the player decides whether the buffered media is already enough. This is what keeps the amount
 * of buffered media bounded.
 *
 * <p>Preparation completes when the extractor has declared its seek map, all of its tracks are
 * known, and every sample queue has received the format of its track. That is the condition
 * ExoPlayer's {@code maybeFinishPrepare} uses, and it is why the extractor is asked for
 * {@link AVExtractorOutput::endTracks}: a container whose tracks are all unsupported declares none
 * of them, and only that notification tells the case apart from a container whose tracks were not
 * parsed yet.
 *
 * <h3>Seeking</h3>
 *
 * A seek first tries to move inside the samples that are already buffered. When that is not possible
 * for an audio or video track, the period discards the buffers, remembers the target and restarts
 * the load from the byte position that the seek map resolves for it. The samples between the seek
 * point and the target are decoded but not rendered, so that the sample at the target has its
 * reference frames while the output that was presented before the seek is not presented again. See
 * {@link SampleQueue::setPendingDiscontinuityPositionUs}.
 *
 * <p>All methods are called on the playback thread, except the callbacks of the loader and the
 * {@link Loadable}, which arrive on the callback looper of the loader, i.e. the thread that started
 * the load.
 */
class ProgressiveMediaPeriod final : public MediaPeriod,
                                     public AVExtractorOutput,
                                     public SampleQueue::UpstreamFormatChangedListener,
                                     public std::enable_shared_from_this<ProgressiveMediaPeriod> {
  /**
   * The load that extracts the container.
   *
   * <p>Declared before {@link LoaderCallbackImpl} because the loader is templated on it.
   */
  class ExtractingLoadable;

  /**
   * Bridges the loader's results to the period.
   *
   * <p>The loader only keeps a raw pointer to its callback, so the period owns this adapter (see
   * {@code loaderCallback}) for as long as the loader can call it. Every method is delivered on the
   * callback looper, which is the thread that owns the period, never the loader thread.
   */
  class LoaderCallbackImpl final : public LoaderCallback<ExtractingLoadable>,
                                   public ReleaseCallback {
  public:
    explicit LoaderCallbackImpl(ProgressiveMediaPeriod& period) noexcept
        : period(period) {
    }

    void onLoadCompleted(ExtractingLoadable* loadable,
        int64_t startTimeMs,
        int64_t loadDurationMs) override;

    void onLoadCanceled(ExtractingLoadable* loadable,
        int64_t startTimeMs,
        int64_t loadDurationMs,
        bool released) override;

    LoadErrorAction onLoadError(ExtractingLoadable* loadable,
        const std::string& errorMessage,
        int errorCount,
        int64_t startTimeMs,
        int64_t loadDurationMs) override;

    void onLoaderReleased() override;

  private:
    ProgressiveMediaPeriod& period;
  };

public:
  /**
   * Creates the data source that reads the media of a progressive source from a url.
   *
   * <p>This is the counterpart of ExoPlayer's {@code DataSource.Factory}. The period calls it on the
   * loader thread, once per period, and owns the data source it returns until it is released.
   */
  using DataSourceFactory = std::function<std::shared_ptr<AVDataSource>(const std::string& url)>;

  /**
   * A listener of the changes of the properties of the media that the period demuxes.
   */
  class Listener {
  public:
    virtual ~Listener() = default;

    /**
     * Called when the duration, the ability to seek within the period, or the categorization as
     * live stream changes.
     *
     * @param durationUs The duration of the period, or {@link Definitions::kTimeUnset}.
     * @param isSeekable Whether the period is seekable.
     * @param isLive Whether the period is live.
     */
    virtual void onSourceInfoRefreshed(int64_t durationUs, bool isSeekable, bool isLive) = 0;
  };

  /**
   * @param url The url of the media, used to report errors and to open the data source.
   * @param extractor The extractor that demuxes the container. Must not be null, and is owned by the
   *                  period: it is released by {@link #release}.
   * @param listener The listener of the source info, or null.
   * @param allocator The allocator the sample queues obtain their buffers from.
   * @param dataSourceFactory The factory of the data source, or null to read the media of the local
   *                          file system.
   * @param eventDispatcher Dispatches the load events of the period to the listeners of the source
   *                        that created it. The default dispatcher reports nothing.
   * @param loadErrorHandlingPolicy Decides how a failed load is retried, or null to use the default
   *                        policy.
   */
  ProgressiveMediaPeriod(std::string url,
      std::shared_ptr<ProgressiveMediaExtractor> extractor,
      Listener* listener,
      std::shared_ptr<Allocator> allocator,
      DataSourceFactory dataSourceFactory,
      BaseMediaSource::EventListenerDispatcher eventDispatcher = {},
      std::shared_ptr<LoadErrorHandlingPolicy> loadErrorHandlingPolicy = nullptr,
      int64_t imageDurationUs = Definitions::kTimeUnset) noexcept;

  ~ProgressiveMediaPeriod() override;

  /**
   * Returns the duration of the media in microseconds, or {@link Definitions::kTimeUnset}.
   */
  [[nodiscard]] int64_t getDurationUs() const;

  /**
   * Returns whether the media is seekable, i.e. whether its extractor declared a seekable seek map.
   */
  [[nodiscard]] bool isSeekable() const;

  /**
   * Releases the period. Called by the source that created it, from {@code releasePeriod}.
   */
  void release();

  // ================================================================================================
  // MediaPeriod
  // ================================================================================================

  void prepare(MediaPeriod::Callback* callback, int64_t positionUs) override;

  [[nodiscard]] const std::shared_ptr<TrackGroupArray>& getTrackGroups() const override;

  int64_t selectTracks(const std::vector<std::shared_ptr<TrackSelection>>& selections,
      const std::vector<bool>& mayRetainStreamFlags,
      std::vector<std::shared_ptr<SampleStream>>& streams,
      std::vector<bool>& streamResetFlags,
      int64_t positionUs) override;

  void discardBuffer(int64_t positionUs, bool toKeyframe) override;

  int64_t readDiscontinuity() override;

  int64_t seekToUs(int64_t positionUs) override;

  int64_t
  getAdjustedSeekPositionUs(int64_t positionUs, const SeekParameters& seekParameters) override;

  // ================================================================================================
  // SequenceableLoader
  // ================================================================================================

  int64_t getBufferedPositionUs() override;

  int64_t getNextLoadPositionUs() override;

  bool continueLoading(int64_t positionUs) override;

  bool isLoading() override;

  void reevaluateBuffer(int64_t positionUs) override;

  // ================================================================================================
  // AVExtractorOutput
  // ================================================================================================

  std::shared_ptr<AVTrackOutput>
  track(int id, MediaType type, AVTrackOutput::SampleConsumer consumer) override;

  std::shared_ptr<AVTrackOutput> track(int id, AVTrackOutput::SampleConsumer consumer) override;

  void seekMap(std::shared_ptr<AVSeekMap> seekMap) override;

  void endTracks() override;

  // ================================================================================================
  // SampleQueue::UpstreamFormatChangedListener
  // ================================================================================================

  void onUpstreamFormatChanged(const std::shared_ptr<MediaFormat>& format) override;

  /**
   * The duration that is added to the timestamp of the last sample when the container does not
   * declare a duration, mirroring ExoPlayer's {@code DEFAULT_LAST_SAMPLE_DURATION_US}.
   */
  static constexpr int64_t DEFAULT_LAST_SAMPLE_DURATION_US = 10000;

  /**
   * The number of bytes the load may read before it parks and asks whether it should continue,
   * mirroring ExoPlayer's {@code DEFAULT_LOADING_CHECK_INTERVAL_BYTES}.
   */
  static constexpr int64_t DEFAULT_LOADING_CHECK_INTERVAL_BYTES = 1024 * 1024;

private:
  // ================================================================================================
  // The loading loop
  // ================================================================================================

  /** Starts a load from {@link #pendingResetPositionUs} or from the start of the media. */
  void startLoading();

  void onLoadCompleted(ExtractingLoadable* loadable, int64_t startTimeMs, int64_t loadDurationMs);

  void onLoadCanceled(ExtractingLoadable* loadable,
      int64_t startTimeMs,
      int64_t loadDurationMs,
      bool released);

  LoadErrorAction onLoadError(ExtractingLoadable* loadable,
      const std::string& errorMessage,
      int errorCount,
      int64_t startTimeMs,
      int64_t loadDurationMs);

  void onLoaderReleased();

  /**
   * Reports that a load started to the listeners of the source.
   *
   * <p>Called by {@link ExtractingLoadable} from the loading thread; the dispatcher of the source
   * moves the event to the thread the application asked for.
   */
  void notifyLoadStarted();

  /**
   * Builds the data that describes what is being loaded.
   *
   * @param mediaStartTimeUs The position the media is loaded from, in microseconds.
   */
  [[nodiscard]] MediaLoadData createMediaLoadData(int64_t mediaStartTimeUs) const;

  /** The dispatcher of the load events of this period. */
  BaseMediaSource::EventListenerDispatcher eventDispatcher;

  /** Decides how a failed load is retried. Never null. */
  std::shared_ptr<LoadErrorHandlingPolicy> loadErrorHandlingPolicy;

  /** The identifier of the next load, see {@link LoadEventInfo#loadTaskId}. */
  int64_t nextLoadTaskId = 0;

  // ================================================================================================
  // Preparation
  // ================================================================================================

  /**
   * Creates and returns the output of a track, or null when the period is already prepared.
   */
  std::shared_ptr<AVTrackOutput> prepareTrackOutput(int trackId, MediaType mediaType);

  /**
   * Completes the preparation when the seek map, the tracks, and the format of every track are
   * known, which is exactly when ExoPlayer's {@code maybeFinishPrepare} does.
   */
  void maybeFinishPrepare();

  /**
   * Reports a fatal failure to the callback, which stops the playback of the period.
   */
  void reportFatalError(const std::string& message);

  /**
   * Throws the fatal error of the period, if it has one.
   *
   * <p>The player calls this while the period is being prepared, which is how it learns that the media
   * cannot be played, see {@link MediaPeriod::maybeThrowPrepareError}.
   *
   * @throws MediaPeriodException If the period cannot be prepared.
   */
  void maybeThrowPrepareError() override;

  /**
   * Returns the fatal error of the period, or an empty string while it has none.
   *
   * <p>See {@link MediaPeriod::getPrepareError}: the same failure as {@link #maybeThrowPrepareError}
   * reports, as a value.
   */
  [[nodiscard]] std::string getPrepareError() const override;

  // ================================================================================================
  // Seeking and reading
  // ================================================================================================

  bool seekInsideBufferUs(const std::vector<bool>& trackIsAudioVideoFlags, int64_t positionUs);

  void markPendingDiscontinuity(int64_t positionUs);

  int getExtractedSamplesCount();

  int64_t getLargestQueuedTimestampUs();

  [[nodiscard]] bool suppressRead() const;

  [[nodiscard]] bool isPendingReset() const;

  [[nodiscard]] bool isReady(int track) const;

  SampleStream::ReadDataResult
  readData(int track, FormatHolder& formatHolder, AVPacketBuffer* buffer, bool formatRequired);

  int skipData(int track, int64_t positionUs);

  void maybeNotifyDownstreamFormat(int track);

  /**
   * Reports media that was removed from the back of a sample queue.
   *
   * <p>Called by the {@link SampleQueueOutput} of the queue that discarded it, and forwarded to the
   * listeners of the source as {@code onUpstreamDiscarded}.
   */
  void notifyUpstreamDiscarded(const MediaLoadData& mediaLoadData);

  void maybeStartDeferredRetry(int track);

  void maybeCallbackContinueLoadingRequested();

  /**
   * The state of the tracks, built once the period is prepared.
   */
  struct TrackState {
    std::shared_ptr<TrackGroupArray> tracks;
    std::vector<bool> trackIsAudioVideoFlags;
    std::vector<bool> trackEnabledStates;
    std::vector<bool> trackNotifiedDownstreamFormats;
  };

  /**
   * The gate that lets the loader read.
   *
   * <p>Mirrors ExoPlayer's {@code LoadCondition}: the loader thread blocks in {@link #block} while
   * the condition is closed, and the playback thread opens it from {@link #continueLoading} when it
   * wants more media. {@link Loadable::cancelLoad} closes it to wake the loader thread up.
   */
  class LoadCondition {
  public:
    /** Opens the condition. Returns whether it was closed before. */
    bool open() {
      Lock lock(pending_lock_);
      if (opened) {
        return false;
      }
      opened = true;
      notify_cv_.notifyAll();
      return true;
    }

    /** Closes the condition. Returns whether it was open before. */
    bool close() {
      Lock lock(pending_lock_);
      const auto wasOpened = opened;
      opened = false;
      notify_cv_.notifyAll();
      return wasOpened;
    }

    /**
     * Blocks until the condition is open or the load is cancelled.
     *
     * @return true when the condition is open, false when the load was cancelled.
     */
    bool block(const std::atomic<bool>& cancelled) const {
      Lock lock(pending_lock_);
      notify_cv_.wait(pending_lock_, [this, &cancelled] {
        return opened || cancelled.load(std::memory_order_acquire);
      });
      return opened;
    }

    [[nodiscard]] bool isOpen() const {
      Lock lock(pending_lock_);
      return opened;
    }

  private:
    mutable Mutex pending_lock_;
    mutable ConditionVariable notify_cv_;
    bool opened = false;
  };

  /**
   * Adapts the samples of the extractor module to a {@link SampleQueue}.
   *
   * <p>The extractor module delivers an {@link AVSample}, whose payload is a reference counted
   * buffer that the extractor allocates, while the playback module copies the payload of every
   * sample into the rolling buffer of the queue, which the decoders read from. This adapter is where
   * the two representations meet.
   */
  class SampleQueueOutput final : public AVTrackOutput::Consumer,
                                  public SampleQueue::UpstreamDiscardedListener {
  public:
    explicit SampleQueueOutput(const std::shared_ptr<SampleQueue>& queue,
        ProgressiveMediaPeriod* period = nullptr) noexcept
        : queue(queue),
          period(period) {
    }

    void setFormat(const std::shared_ptr<MediaFormat>& format) override;

    void consume(const AVSample& sample) override;

    /**
     * Reports the media that was removed from the back of the queue as a media source event, which is
     * what ExoPlayer's {@code MediaSourceEventListener.onUpstreamDiscarded} carries.
     */
    void onUpstreamDiscarded(int64_t mediaStartTimeUs, int64_t mediaEndTimeUs) override;

    /** Translates the flags of the extractor module to the flags of a media buffer. */
    static AVBufferFlags toBufferFlags(uint32_t flags);

  private:
    std::shared_ptr<SampleQueue> queue;
    //!< The period the event is dispatched for, or null when the queue belongs to no period.
    ProgressiveMediaPeriod* period;
  };

  /**
   * The stream a renderer reads its samples from.
   */
  class ProgressiveSampleStream final : public SampleStream {
  public:
    explicit ProgressiveSampleStream(ProgressiveMediaPeriod* source,
        int track,
        MediaType mediaType) noexcept
        : source(source),
          track(track),
          mediaType(mediaType) {
    }

    [[nodiscard]] MediaType getMediaType() const override { return mediaType; }

    [[nodiscard]] int getTrack() const { return track; }

    [[nodiscard]] bool isReady() const override;

    ReadDataResult
    readData(FormatHolder& formatHolder, AVPacketBuffer* packetBuffer, bool formatRequired) override;

    int skipData(int64_t positionUs) override;

  private:
    ProgressiveMediaPeriod* source;
    int track;
    MediaType mediaType;
  };

  // ================================================================================================
  // Members
  // ================================================================================================

  Listener* listener;
  std::shared_ptr<Allocator> allocator;
  int64_t continueLoadingCheckIntervalBytes;
  std::unique_ptr<Loader> loader;

  /** The adapter the loader calls back into. The loader only holds a raw pointer to it. */
  LoaderCallbackImpl loaderCallback{ *this };

  /** The load in flight. Owned here because the loader only keeps a raw pointer to it. */
  std::shared_ptr<ExtractingLoadable> loadable;

  /**
   * Releases the sample queues, the extractor, the data source and the load in flight.
   *
   * <p>Called by {@link #release} once the loading thread has been stopped and joined, so that the
   * resources of the period never wait for a callback to be dispatched. Idempotent: the release
   * callback of the loader calls it too, and whichever runs first is the one that frees them.
   */
  void releaseResources();

  std::string inputUrl;
  DataSourceFactory dataSourceFactory;
  std::shared_ptr<ProgressiveMediaExtractor> extractor;

  /** The data source of the media, created by the first load and kept across the seeks. */
  std::shared_ptr<AVDataSource> dataSource;

  /** The input the extractor reads from, created with the data source. */
  std::shared_ptr<AVDefaultExtractorInput> input;

  LoadCondition loadCondition;

  MediaPeriod::Callback* mediaCallback;
  std::vector<std::shared_ptr<SampleQueue>> sampleQueues;

  /**
   * The track selection of every track, indexed like {@link #sampleQueues}, or null for a track that
   * is not selected.
   *
   * <p>The period owns them so that it can re-evaluate them and let them decide how much of the back
   * buffer to keep, which is what ExoPlayer's chunk source does.
   */
  std::vector<std::shared_ptr<TrackSelection>> trackSelections;
  std::vector<int> sampleQueueTrackIds;
  std::vector<std::shared_ptr<AVTrackOutput>> trackOutputs;

  /** Whether the extractor declared every track it will declare. */
  bool sampleQueuesBuilt;

  bool prepared;
  bool haveAudioVideoTracks;
  TrackState trackState;
  std::shared_ptr<AVSeekMap> mSeekMap;
  int64_t durationUs;
  int64_t imageDurationUs;
  bool isLive;

  bool seenFirstTrackSelection;
  bool notifyDiscontinuity;
  int enabledTrackCount;
  int64_t length;

  /** The position the media is played from, used as the origin of the buffered position. */
  int64_t startPositionUs;
  int64_t lastSeekPositionUs;
  int64_t pendingResetPositionUs;
  bool pendingDeferredRetry;

  int extractedSamplesCountAtStartOfLoad;
  bool loadingFinished;
  bool released;

  /**
   * Whether {@link #releaseResources()} has already freed the resources of the period.
   *
   * <p>It is what makes the release idempotent, so that {@link #release} and the release callback of
   * the loader can both run it.
   */
  bool resourcesReleased = false;

  /**
   * Whether the failure of the load in flight cannot be fixed by a retry, e.g. because no extractor
   * recognized the container. Set by {@link ExtractingLoadable}.
   */
  bool fatalLoadError = false;

  /**
   * The fatal error of the period, or an empty string while it has none.
   *
   * <p>It is reported by {@link #maybeThrowPrepareError}, and never cleared: a period that cannot be
   * prepared cannot become playable.
   */
  std::string fatalErrorMessage;

  /**
   * Whether the extractor was initialized with this period and recognized the container.
   *
   * The tracks of a container are declared when its extractor is initialized, and this period
   * refuses to create a track once it is prepared, so the extractor is initialized exactly once per
   * period. A load that follows a seek reuses it and only tells it where the input was repositioned.
   */
  bool extractorInitialized = false;
};

/**
 * The load of a {@link ProgressiveMediaPeriod}: it opens the data source, initializes the extractor,
 * and reads the container until it reaches the end of the media, until it has read enough to ask for
 * permission to continue, or until it is cancelled.
 */
class ProgressiveMediaPeriod::ExtractingLoadable final : public Loadable {
public:
  ExtractingLoadable(std::string url,
      const std::shared_ptr<ProgressiveMediaPeriod>& source,
      LoadCondition& loadCondition) noexcept;

  ~ExtractingLoadable() override;

  /**
   * Sets the position the load restarts from.
   *
   * @param timeUs The position in microseconds. The byte position is resolved from the seek map of
   *               the extractor once it is initialized, because the map is only known then.
   */
  void setLoadPosition(const int64_t timeUs) {
    seekTimeUs = timeUs;
    pendingExtractorSeek = true;
  }

  /** The identifier of this load, see {@link LoadEventInfo#loadTaskId}. */
  [[nodiscard]] int64_t getLoadTaskId() const { return loadTaskId; }

  /** When this load started, in milliseconds. */
  [[nodiscard]] int64_t getLoadStartTimeMs() const { return loadStartTimeMs; }

  /** How many bytes were read since this load started. */
  [[nodiscard]] int64_t getBytesLoaded() const;

  void cancelLoad() override;

  void load() override;

  [[nodiscard]] std::string getLoadErrorMessage() const override {
    return loadErrorMessage;
  }

private:
  /**
   * Opens the data source and creates the input, once per period.
   *
   * @return Whether the input is usable.
   */
  bool ensureInput();

  std::string mInputUrl;
  std::shared_ptr<ProgressiveMediaPeriod> source;
  LoadCondition& loadCondition;

  /**
   * The failure that stopped the last {@link #load()}, or an empty string.
   *
   * Failures are reported here rather than thrown: the Android build cannot unwind coral's frames
   * (see {@link Loadable::getLoadErrorMessage()}).
   */
  std::string loadErrorMessage;

  std::atomic_bool loadingCanceled{ false };

  /** The identifier of this load. */
  int64_t loadTaskId = 0;

  /** When this load started, in milliseconds, or 0 when it has not started yet. */
  int64_t loadStartTimeMs = 0;

  /** The position of the input this load started from, used to count the bytes it read. */
  long loadStartPosition = 0;

  /** The time the load starts from, in microseconds. */
  int64_t seekTimeUs = 0;

  /** Whether the extractor has to be told about {@link #seekTimeUs} before the first read. */
  bool pendingExtractorSeek = true;
};

} // namespace coral

#endif //CORAL_PLAYBACK_SOURCE_PROGRESSIVE_MEDIA_PERIOD_H
