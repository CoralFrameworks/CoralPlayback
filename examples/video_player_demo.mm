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

/**
 * @file video_player_demo.mm
 *
 * The macOS playback demo: decode a video with CoralPlayback, run the frames through the effect graph
 * and display them in a Cocoa window.
 *
 * ## The pipeline
 *
 * ```
 * ProgressiveMediaSource -> DecodeVideoRenderer -> VideoFrameInputNode
 *     -> VideoLayoutPass -> SubtitleOverlayNode -> ReadbackOutputNode -> NSWindow
 * ```
 *
 * The player owns everything up to the output node. A `VideoOutputNodeFactory` is the public seam
 * between the player and a platform window: the player hands it a `VideoSurface` and the factory
 * returns the `EffectOutputNode` that receives the composed frames. This demo supplies a CPU-backed
 * factory that returns a `ReadbackOutputNode`, then copies each completed frame into an `NSImage`.
 * That keeps the demo to the public playback and effect APIs, with no private header and no GPU
 * context sharing; a production player would instead return a swap-chain output node and sample the
 * final texture directly.
 *
 * The windowed demo plays the sound track by default through CoralAudio; `--no-audio` runs on the
 * video and the standalone clock only, which is what a machine without an output device needs.
 * `--mute` keeps the device open at zero volume.
 *
 * The window closes at the end of the media, on Esc/Q, or when the window is closed.
 *
 * ## Usage
 *
 * ```sh
 * ./video_player_demo                                # assets/video/video720p_5s.mp4, windowed
 * ./video_player_demo clip.mp4 --size 1280x720
 * ./video_player_demo clip.mp4 --frames 120          # stop after 120 displayed frames
 * ./video_player_demo clip.mp4 --output frames --frames 30   # headless: write PNGs
 * ```
 *
 * `--output <dir>` writes every received frame as `frame_00001.png` and does not open a window, which
 * is the check a build machine can run. The default media path is compiled in as `CORAL_ASSETS_DIR`.
 *
 * Exit codes: 0 = the media played (and at least one frame was produced), 1 = the player or decoder
 * failed, 2 = bad arguments.
 */

#include <coral/playback/LivePlayer.h>
#include <coral/playback/Player.h>
#include <coral/playback/VideoOutput.h>

#include <codec/MediaCodec.h>
#include <codec/MediaMetadataRetriever.h>

#include <effect/EffectEngine.h>
#include <effect/EffectFrame.h>
#include <effect/ReadbackOutputNode.h>

#include <graphics/Enums.h>
#include <graphics/ImageCodec.h>
#include <graphics/ImageInfo.h>

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

// ==================================================================================================
// Options
// ==================================================================================================

struct Options {
  std::string mediaPath;
  std::string outputDirectory;
  uint32_t renderWidth = 0;  // 0 = use the video's own size
  uint32_t renderHeight = 0;
  int frames = 0; // 0 = until the media ends
  bool audio = true;
  bool audioSpecified = false;
  bool mute = false;
  bool showWindow = true;
};

void printUsage(const char* program) {
  std::printf(
      "usage: %s [media] [options]\n"
      "\n"
      "  media                 file to play; defaults to the asset in assets/video\n"
      "  --size WxH            render size of the effect graph (default: the video size)\n"
      "  --frames N            stop after N displayed frames (default: play to the end)\n"
      "  --output DIR          write frame_00001.png... into DIR, without a window\n"
      "  --audio               play the audio track (the default in a window)\n"
      "  --no-audio            do not build the audio renderer\n"
      "  --mute                set the audio volume to zero\n"
      "  --help                this text\n",
      program);
}

std::optional<Options> parseOptions(const int argc, char** argv) {
  Options options;
  options.mediaPath = std::string(CORAL_ASSETS_DIR) + "/video/video720p_5s.mp4";
  bool mediaGiven = false;

  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h") {
      printUsage(argv[0]);
      std::exit(0);
    }
    if (argument == "--audio") {
      options.audio = true;
      options.audioSpecified = true;
      continue;
    }
    if (argument == "--no-audio") {
      options.audio = false;
      options.audioSpecified = true;
      continue;
    }
    if (argument == "--mute") {
      options.mute = true;
      continue;
    }
    if (argument == "--size") {
      if (++index >= argc) {
        std::fprintf(stderr, "error: --size needs WxH\n");
        return std::nullopt;
      }
      unsigned width = 0;
      unsigned height = 0;
      if (std::sscanf(argv[index], "%ux%u", &width, &height) != 2 || width == 0 || height == 0) {
        std::fprintf(stderr, "error: invalid --size '%s'\n", argv[index]);
        return std::nullopt;
      }
      options.renderWidth = width;
      options.renderHeight = height;
      continue;
    }
    if (argument == "--frames") {
      if (++index >= argc) {
        std::fprintf(stderr, "error: --frames needs a count\n");
        return std::nullopt;
      }
      options.frames = std::atoi(argv[index]);
      if (options.frames <= 0) {
        std::fprintf(stderr, "error: --frames must be positive\n");
        return std::nullopt;
      }
      continue;
    }
    if (argument == "--output") {
      if (++index >= argc) {
        std::fprintf(stderr, "error: --output needs a directory\n");
        return std::nullopt;
      }
      options.outputDirectory = argv[index];
      options.showWindow = false;
      continue;
    }
    if (!argument.empty() && argument.front() == '-') {
      std::fprintf(stderr, "error: unknown option '%s'\n", argument.c_str());
      return std::nullopt;
    }
    if (mediaGiven) {
      std::fprintf(stderr, "error: more than one media file given\n");
      return std::nullopt;
    }
    options.mediaPath = argument;
    mediaGiven = true;
  }

  // A headless frame dump needs no sound and may run where no output device exists. It keeps the
  // audio renderer off unless the caller asked for it explicitly.
  if (!options.showWindow && !options.audioSpecified) {
    options.audio = false;
  }
  return options;
}

// ==================================================================================================
// Video size
// ==================================================================================================

/**
 * Reads the display size of the first video track, so the graph can be sized to the picture before
 * playback starts.
 *
 * The player can only report the size once the decoder has parsed the track, and replacing the output
 * surface while the engine is running is the late-attach path (it pauses and resumes the engine). The
 * demo knows the file up front, so asking the retriever keeps the surface attached before `prepare()`
 * and avoids that path entirely.
 */
bool resolveVideoSize(const std::string& path, uint32_t* width, uint32_t* height) {
  const auto retriever = coral::MediaMetadataRetriever::create(path);
  if (retriever == nullptr) {
    return false;
  }
  const int trackCount = retriever->getTrackCount();
  for (int index = 0; index < trackCount; ++index) {
    const auto format = retriever->getTrackFormat(index);
    if (format == nullptr || !format->isVideo()) {
      continue;
    }
    const int videoWidth = format->getWidth();
    const int videoHeight = format->getHeight();
    if (videoWidth > 0 && videoHeight > 0) {
      *width = static_cast<uint32_t>(videoWidth);
      *height = static_cast<uint32_t>(videoHeight);
      return true;
    }
  }
  return false;
}

// ==================================================================================================
// Frame capture
// ==================================================================================================

/** A frame copied out of the effect graph, in tightly packed RGBA8. */
struct CapturedFrame {
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t sequence = 0;
  std::vector<uint8_t> rgba;
};

/**
 * Receives the frames of the `ReadbackOutputNode` and keeps the newest one for the window.
 *
 * The callback runs on the effect engine's readback thread. It copies under the lock and returns, so
 * the render side never waits on the display; the window polls `takeAfter()` from its timer.
 */
class FrameSink {
public:
  /**
   * @param deliverCallback install the frame callback the window uses. A headless run consumes the
   *        node with `waitForFrame` instead: with a callback installed the readback node posts every
   *        frame to the engine's callback queue, and a headless run never drains it.
   */
  explicit FrameSink(const bool deliverCallback) : mNode(coral::ReadbackOutputNode::create()) {
    if (deliverCallback && mNode != nullptr) {
      mNode->setCallback([this](const coral::EffectFrame& frame, const uint8_t* pixels,
                                const size_t byteSize) { onFrame(frame, pixels, byteSize); });
    }
  }

  [[nodiscard]] std::shared_ptr<coral::ReadbackOutputNode> node() const { return mNode; }
  [[nodiscard]] uint32_t width() const { return mWidth.load(std::memory_order_acquire); }
  [[nodiscard]] uint32_t height() const { return mHeight.load(std::memory_order_acquire); }

  /** Resolves the size the graph renders at; called once the video size is known. */
  void setSize(const uint32_t width, const uint32_t height) {
    mWidth.store(width, std::memory_order_release);
    mHeight.store(height, std::memory_order_release);
  }
  [[nodiscard]] uint64_t frameCount() const { return mFrameCount.load(std::memory_order_acquire); }

  /** @return the newest frame newer than @p previousSequence, or nothing when none arrived. */
  [[nodiscard]] std::optional<CapturedFrame> takeAfter(const uint64_t previousSequence) const {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLatest.has_value() || mLatest->sequence <= previousSequence) {
      return std::nullopt;
    }
    return mLatest;
  }

private:
  void onFrame(const coral::EffectFrame& frame, const uint8_t* pixels, const size_t byteSize) {
    if (pixels == nullptr || frame.width() == 0 || frame.height() == 0) {
      return;
    }
    const size_t expected = static_cast<size_t>(frame.width()) * frame.height() * 4u;
    if (byteSize < expected) {
      return;
    }
    CapturedFrame captured;
    captured.width = frame.width();
    captured.height = frame.height();
    captured.sequence = ++mSequence;
    captured.rgba.assign(pixels, pixels + expected);
    {
      std::lock_guard<std::mutex> lock(mMutex);
      mLatest = std::move(captured);
    }
    mFrameCount.fetch_add(1, std::memory_order_acq_rel);
  }

  std::shared_ptr<coral::ReadbackOutputNode> mNode;
  std::atomic<uint32_t> mWidth{0};
  std::atomic<uint32_t> mHeight{0};
  mutable std::mutex mMutex;
  std::optional<CapturedFrame> mLatest;
  std::atomic<uint64_t> mSequence{0};
  std::atomic<uint64_t> mFrameCount{0};
};

/**
 * The factory the player asks for an output node.
 *
 * It ignores the platform surface: the demo's sink is a CPU readback, so the surface only carries
 * "the window is ready" and the size the graph should render at. A real window player returns a
 * `WindowsSwapChainOutputNode` or an Android surface node here instead.
 */
class ReadbackOutputFactory final : public coral::VideoOutputNodeFactory {
public:
  explicit ReadbackOutputFactory(std::shared_ptr<FrameSink> sink) : mSink(std::move(sink)) {}

  std::shared_ptr<coral::EffectOutputNode> create(
      const std::shared_ptr<coral::VideoSurface>& surface) override {
    if (surface == nullptr || !surface->isValid()) {
      return nullptr;
    }
    return mSink->node();
  }

  bool querySize(const std::shared_ptr<coral::VideoSurface>& surface, uint32_t* width,
                 uint32_t* height) override {
    if (surface == nullptr || !surface->isValid() || width == nullptr || height == nullptr) {
      return false;
    }
    if (mSink->width() == 0 || mSink->height() == 0) {
      return false;
    }
    *width = mSink->width();
    *height = mSink->height();
    return true;
  }

private:
  std::shared_ptr<FrameSink> mSink;
};

/** A surface that only says "the output is available"; the pixels never go to the platform decoder. */
class ReadbackSurface final : public coral::VideoSurface {
public:
  [[nodiscard]] void* getInstance() const override { return const_cast<ReadbackSurface*>(this); }
  [[nodiscard]] bool isValid() const override { return true; }
  void release() override {}
};

// ==================================================================================================
// Player session
// ==================================================================================================

class SessionListener final : public coral::Player::EventListener {
public:
  void onPlaybackStateChanged(const coral::Player::State& state) override {
    if (state == coral::Player::State::STATE_ENDED) {
      mEnded.store(true, std::memory_order_release);
    }
  }

  void onPlayerError(const std::string& errorMessage) override {
    std::lock_guard<std::mutex> lock(mMutex);
    mError = errorMessage;
    mFailed.store(true, std::memory_order_release);
  }

  [[nodiscard]] bool ended() const { return mEnded.load(std::memory_order_acquire); }
  [[nodiscard]] bool failed() const { return mFailed.load(std::memory_order_acquire); }

  [[nodiscard]] std::string error() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mError;
  }

private:
  std::atomic<bool> mEnded{false};
  std::atomic<bool> mFailed{false};
  mutable std::mutex mMutex;
  std::string mError;
};

class PlayerSession {
public:
  ~PlayerSession() {
    if (mPlayer != nullptr) {
      mPlayer->release();
    }
  }

  static std::unique_ptr<PlayerSession> create(const Options& options) {
    auto player = coral::LivePlayer::create(
        options.mediaPath, coral::LivePlayer::Configuration{.audioEnabled = options.audio});
    if (player == nullptr) {
      std::fprintf(stderr, "error: could not create a player for '%s'\n", options.mediaPath.c_str());
      return nullptr;
    }

    auto session = std::unique_ptr<PlayerSession>(new PlayerSession());
    session->mSink = std::make_shared<FrameSink>(options.showWindow);
    session->mListener = std::make_shared<SessionListener>();

    // The pipeline has to be enabled before prepare(), and the surface is what makes the player ask
    // the factory for an output node.
    player->enableVideoEffectPipeline(std::make_shared<ReadbackOutputFactory>(session->mSink));
    if (!player->isVideoEffectPipelineEnabled()) {
      std::fprintf(stderr, "error: the effect video pipeline could not be enabled\n");
      return nullptr;
    }
    if (options.audio && options.mute) {
      player->setVolume(coral::Volume{0.0f, 0.0f});
    }
    player->addListener(session->mListener);

    uint32_t renderWidth = options.renderWidth;
    uint32_t renderHeight = options.renderHeight;
    if (renderWidth == 0 || renderHeight == 0) {
      if (!resolveVideoSize(options.mediaPath, &renderWidth, &renderHeight)) {
        renderWidth = 1280;
        renderHeight = 720;
      }
    }

    session->mPlayer = std::move(player);
    session->mSink->setSize(renderWidth, renderHeight);
    session->mPlayer->setOutputSurface(std::make_shared<ReadbackSurface>());
    session->mPlayer->prepare();

    // Wait for the source to be ready before starting: a player that is told to play while the first
    // period is still being prepared can otherwise race the flush of the initial load.
    const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < readyDeadline) {
      if (session->mPlayer->getPlaybackState() == coral::Player::State::STATE_READY ||
          session->listener()->failed()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    session->mPlayer->play();
    return session;
  }

  [[nodiscard]] coral::LivePlayer* player() const { return mPlayer.get(); }
  [[nodiscard]] const std::shared_ptr<FrameSink>& sink() const { return mSink; }
  [[nodiscard]] const std::shared_ptr<SessionListener>& listener() const { return mListener; }

private:
  PlayerSession() = default;

  std::unique_ptr<coral::LivePlayer> mPlayer;
  std::shared_ptr<FrameSink> mSink;
  std::shared_ptr<SessionListener> mListener;
};

// ==================================================================================================
// PNG output (headless)
// ==================================================================================================

bool writePng(const CapturedFrame& frame, const std::filesystem::path& path) {
  graphics::DecodedImage image;
  image.info = graphics::ImageInfo(static_cast<int>(frame.width), static_cast<int>(frame.height),
                                   graphics::ColorType::kRGBA_8888,
                                   graphics::AlphaType::kPremultiplied);
  image.pixels = frame.rgba;
  const auto encoded = graphics::encodeImage(image, graphics::ImageFormat::PNG);
  if (!encoded.has_value() || encoded->empty()) {
    return false;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return false;
  }
  file.write(reinterpret_cast<const char*>(encoded->data()),
             static_cast<std::streamsize>(encoded->size()));
  return file.good();
}

int runHeadless(const Options& options) {
  std::error_code directoryError;
  std::filesystem::create_directories(options.outputDirectory, directoryError);
  if (directoryError) {
    std::fprintf(stderr, "error: cannot create '%s': %s\n", options.outputDirectory.c_str(),
                 directoryError.message().c_str());
    return 1;
  }

  auto session = PlayerSession::create(options);
  if (session == nullptr) {
    return 1;
  }

  const int wanted = options.frames > 0 ? options.frames : 30;
  int written = 0;
  int timeouts = 0;
  int64_t lastTimestamp = 0;
  uint32_t lastTimescale = 0;
  bool haveTimestamp = false;
  auto lastNewFrame = std::chrono::steady_clock::now();

  // The engine renders continuously, so several committed frames can carry the same decoder frame.
  // `waitForFrame` returns each commit; the dump keeps one PNG per new presentation time.
  while (written < wanted) {
    coral::EffectFrame frame;
    std::vector<uint8_t> pixels;
    if (!session->sink()->node()->waitForFrame(&frame, &pixels, 5000)) {
      if (++timeouts >= 2) {
        break;
      }
      continue;
    }
    timeouts = 0;

    const coral::RenderTime timestamp = frame.timestamp();
    if (haveTimestamp && timestamp.value == lastTimestamp &&
        timestamp.timescale == lastTimescale) {
      // The engine keeps rendering the last input frame after the media has ended. Stop once no new
      // presentation time has arrived for a while; the frame count is the requested one otherwise.
      if (std::chrono::steady_clock::now() - lastNewFrame >= std::chrono::seconds(3)) {
        break;
      }
      continue;
    }
    lastTimestamp = timestamp.value;
    lastTimescale = timestamp.timescale;
    haveTimestamp = true;
    lastNewFrame = std::chrono::steady_clock::now();

    CapturedFrame captured;
    captured.width = frame.width();
    captured.height = frame.height();
    captured.sequence = static_cast<uint64_t>(written + 1);
    captured.rgba = std::move(pixels);
    char name[32] = {};
    std::snprintf(name, sizeof(name), "frame_%05d.png", written + 1);
    const auto path = std::filesystem::path(options.outputDirectory) / name;
    if (!writePng(captured, path)) {
      std::fprintf(stderr, "error: cannot write '%s'\n", path.string().c_str());
      return 1;
    }
    std::printf("frame %d: %ux%u at %lld\n", written + 1, captured.width, captured.height,
                static_cast<long long>(timestamp.value));
    std::fflush(stdout);
    ++written;
    if (session->listener()->failed()) {
      break;
    }
  }

  if (session->listener()->failed()) {
    std::fprintf(stderr, "error: %s\n", session->listener()->error().c_str());
    return 1;
  }
  std::printf("wrote %d frame(s) to %s\n", written, options.outputDirectory.c_str());
  return written > 0 ? 0 : 1;
}

// ==================================================================================================
// Cocoa window
// ==================================================================================================

std::unique_ptr<PlayerSession> gSession;
std::atomic<int> gExitCode{0};

NSImage* imageFromFrame(const CapturedFrame& frame) {
  if (frame.width == 0 || frame.height == 0 || frame.rgba.size() < static_cast<size_t>(frame.width) * frame.height * 4u) {
    return nil;
  }
  NSBitmapImageRep* representation = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:nullptr
                    pixelsWide:static_cast<NSInteger>(frame.width)
                    pixelsHigh:static_cast<NSInteger>(frame.height)
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSCalibratedRGBColorSpace
                   bytesPerRow:static_cast<NSInteger>(frame.width) * 4
                  bitsPerPixel:32];
  if (representation == nil) {
    return nil;
  }
  std::memcpy(representation.bitmapData, frame.rgba.data(), frame.rgba.size());
  NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(frame.width, frame.height)];
  [image addRepresentation:representation];
  return image;
}

} // namespace

// ==================================================================================================
// The view and application delegate
// ==================================================================================================

@interface CoralDemoVideoView : NSView
@property(nonatomic, strong) NSImage* frameImage;
@property(nonatomic, assign) BOOL shouldStop;
@end

@implementation CoralDemoVideoView

- (BOOL)isOpaque {
  return YES;
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)keyDown:(NSEvent*)event {
  NSString* characters = [event charactersIgnoringModifiers];
  if (characters.length == 0) {
    return;
  }
  const unichar character = [characters characterAtIndex:0];
  if (character == 27 /* Esc */ || character == 'q' || character == 'Q') {
    self.shouldStop = YES;
    [NSApp terminate:nil];
  }
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  [[NSColor blackColor] setFill];
  NSRectFill(self.bounds);

  NSImage* image = self.frameImage;
  if (image == nil) {
    return;
  }
  const NSSize imageSize = image.size;
  if (imageSize.width <= 0 || imageSize.height <= 0) {
    return;
  }
  const NSSize bounds = self.bounds.size;
  const CGFloat scale = std::min(bounds.width / imageSize.width, bounds.height / imageSize.height);
  const NSSize targetSize = NSMakeSize(imageSize.width * scale, imageSize.height * scale);
  const NSRect target = NSMakeRect((bounds.width - targetSize.width) / 2.0,
                                   (bounds.height - targetSize.height) / 2.0,
                                   targetSize.width, targetSize.height);
  [image drawInRect:target
           fromRect:NSZeroRect
          operation:NSCompositingOperationCopy
           fraction:1.0
     respectFlipped:NO
              hints:nil];
}

@end

@interface CoralDemoAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) CoralDemoVideoView* view;
@property(nonatomic, strong) NSTimer* timer;
@property(nonatomic, assign) uint64_t lastSequence;
@property(nonatomic, assign) int displayedFrames;
@property(nonatomic, assign) int frameLimit;
@end

@implementation CoralDemoAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;

  double contentWidth = 960;
  double contentHeight = 540;
  if (gSession != nullptr) {
    const double videoWidth = static_cast<double>(gSession->sink()->width());
    const double videoHeight = static_cast<double>(gSession->sink()->height());
    if (videoWidth > 0 && videoHeight > 0) {
      const double scale = std::min(960.0 / videoWidth, 720.0 / videoHeight);
      contentWidth = videoWidth * std::min(scale, 1.0);
      contentHeight = videoHeight * std::min(scale, 1.0);
    }
  }
  const NSRect content = NSMakeRect(0, 0, contentWidth, contentHeight);
  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:content
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  window.title = @"CoralPlayback - video_player_demo";
  window.backgroundColor = [NSColor blackColor];
  window.delegate = self;
  window.minSize = NSMakeSize(320, 180);

  CoralDemoVideoView* view = [[CoralDemoVideoView alloc] initWithFrame:content];
  window.contentView = view;
  self.window = window;
  self.view = view;
  self.lastSequence = 0;
  self.displayedFrames = 0;

  [window center];
  [window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                                target:self
                                              selector:@selector(tick:)
                                              userInfo:nil
                                               repeats:YES];
  [[NSRunLoop mainRunLoop] addTimer:self.timer forMode:NSRunLoopCommonModes];
}

- (void)windowWillClose:(NSNotification*)notification {
  (void)notification;
  [NSApp terminate:nil];
}

- (void)applicationWillTerminate:(NSNotification*)notification {
  (void)notification;
  [self.timer invalidate];
  gSession.reset();
}

- (void)tick:(NSTimer*)timer {
  (void)timer;
  if (gSession == nullptr) {
    [NSApp terminate:nil];
    return;
  }

  // The readback node posts its callback through the engine's callback queue; a host that wants the
  // callback has to drain it. The demo does that on the main thread, right before it looks for a frame.
  if (const auto engine = gSession->player()->getEffectEngine(); engine != nullptr) {
    engine->pollCallbacks();
  }

  if (const auto frame = gSession->sink()->takeAfter(self.lastSequence); frame.has_value()) {
    self.lastSequence = frame->sequence;
    NSImage* image = imageFromFrame(*frame);
    if (image == nil) {
      std::fprintf(stderr, "error: could not create an NSImage for a %ux%u frame\n",
                   frame->width, frame->height);
    }
    self.view.frameImage = image;
    [self.view setNeedsDisplay:YES];
    ++self.displayedFrames;
  }

  if (gSession->listener()->failed()) {
    std::fprintf(stderr, "error: %s\n", gSession->listener()->error().c_str());
    gExitCode.store(1, std::memory_order_release);
    [NSApp terminate:nil];
    return;
  }

  const int64_t duration = gSession->player()->getDurationUs();
  const bool reachedEnd = gSession->listener()->ended() ||
                          (duration > 0 && gSession->player()->getCurrentPositionUs() >= duration);
  if (reachedEnd && gSession->sink()->frameCount() > 0) {
    std::printf("played %llu frame(s), displayed %d\n",
                static_cast<unsigned long long>(gSession->sink()->frameCount()),
                self.displayedFrames);
    [NSApp terminate:nil];
    return;
  }

  if (self.frameLimit > 0 && self.displayedFrames >= self.frameLimit) {
    std::printf("displayed %d frame(s)\n", self.displayedFrames);
    [NSApp terminate:nil];
  }
}

@end

// ==================================================================================================
// main
// ==================================================================================================

int main(int argc, char** argv) {
  const auto options = parseOptions(argc, argv);
  if (!options.has_value()) {
    return 2;
  }
  if (!std::filesystem::exists(options->mediaPath)) {
    std::fprintf(stderr, "error: media file not found: %s\n", options->mediaPath.c_str());
    return 2;
  }

  std::printf("media: %s\n", options->mediaPath.c_str());
  std::printf("audio: %s\n", options->audio ? "on" : "off");

  if (!options->showWindow) {
    return runHeadless(*options);
  }

  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    gSession = PlayerSession::create(*options);
    if (gSession == nullptr) {
      return 1;
    }

    CoralDemoAppDelegate* delegate = [[CoralDemoAppDelegate alloc] init];
    delegate.frameLimit = options->frames;
    [NSApp setDelegate:delegate];
    [NSApp run];
  }

  return gExitCode.load(std::memory_order_acquire);
}
