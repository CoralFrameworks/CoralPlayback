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
 * The macOS playback demo: decode a video with CoralPlayback, run its frames through the effect graph
 * and present the composed result through an IOSurface.
 *
 * ## The pipeline
 *
 * ```
 * ProgressiveMediaSource -> DecodeVideoRenderer -> VideoFrameInputNode
 *     -> VideoLayoutPass -> SubtitleOverlayNode -> AppleIOSurfaceOutputNode -> IOSurface -> CALayer
 * ```
 *
 * Zero-copy is the default. `AppleHardwareVideoSurface` asks VideoToolbox for IOSurface-backed
 * frames; `AppleVideoFrameSink` asks the decoder for BGRA through the `MediaFormat` process format,
 * so the decoder's own IOSurface is published straight to `AppleIOSurfaceInputNode` and the pixels
 * never visit the CPU. A decoder that keeps NV12 falls back to a GPU NV12-to-BGRA conversion ring
 * inside the sink. `--cpu` selects the CPU sink instead: `DecodeVideoRenderer` hands the frame to
 * the player's CPU sink, which converts the planes and uploads them through `VideoFrameInputNode`.
 * In every case the graph composes the frame and `AppleVideoOutputNodeFactory` turns the
 * application's `AppleIOSurface` into an `AppleIOSurfaceOutputNode`, which copies the final image
 * into that surface on the GPU.
 *
 * The window is a layer-backed `NSView` whose `CALayer.contents` is the surface. Re-assigning the
 * surface on every new effect frame is what makes Core Animation re-composite it; the pixels never
 * leave the GPU. That is the difference from the previous version of this demo, which used a
 * `ReadbackOutputNode` and copied every frame back to the CPU to build an `NSImage`.
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
 * ./video_player_demo clip.mp4 --frames 120          # stop after 120 presented effect frames
 * ./video_player_demo clip.mp4 --cpu                 # CPU fallback instead of zero-copy
 * ./video_player_demo clip.mp4 --output frames --frames 30   # headless: write PNGs from the surface
 * ```
 *
 * `--output <dir>` reads the output IOSurface back (after waiting for the GPU) and writes
 * `frame_00001.png` per sampled presentation position, without opening a window. That is the check a
 * build machine can run: it proves the decoded frames reached the graph, the graph rendered into the
 * surface, and the surface contains pixels. The default media path is compiled in as
 * `CORAL_ASSETS_DIR`.
 *
 * Exit codes: 0 = the media played (and at least one frame was produced), 1 = the player or decoder
 * failed, 2 = bad arguments.
 */

#include <coral/playback/LivePlayer.h>
#include <coral/playback/Player.h>
#include <coral/playback/VideoOutput.h>
#include <coral/playback/apple/AppleIOSurface.h>
#include <coral/playback/apple/AppleVideoInput.h>
#include <coral/playback/apple/AppleVideoOutputNodeFactory.h>

#include <codec/MediaMetadataRetriever.h>

#include <effect/EffectEngine.h>

#include <graphics/Enums.h>
#include <graphics/ImageCodec.h>
#include <graphics/ImageInfo.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <IOSurface/IOSurface.h>

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
  bool zeroCopy = true;
};

void printUsage(const char* program) {
  std::printf(
      "usage: %s [media] [options]\n"
      "\n"
      "  media                 file to play; defaults to the asset in assets/video\n"
      "  --size WxH            size of the output IOSurface (default: the video size)\n"
      "  --frames N            stop after N presented frames (default: play to the end)\n"
      "  --output DIR          write frame_00001.png... into DIR, without a window\n"
      "  --audio               play the audio track (the default in a window)\n"
      "  --no-audio            do not build the audio renderer\n"
      "  --mute                set the audio volume to zero\n"
      "  --cpu                 decode through CPU planes and upload them (default: zero-copy)\n"
      "  --zero-copy           sample the decoder's IOSurface (the default)\n"
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
    if (argument == "--cpu") {
      options.zeroCopy = false;
      continue;
    }
    if (argument == "--zero-copy") {
      options.zeroCopy = true;
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
 * Reads the display size of the first video track, so the IOSurface and the window can be sized to
 * the picture before playback starts.
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
// Surface readback (headless only)
// ==================================================================================================

/** A frame copied out of the output IOSurface, in tightly packed RGBA8. */
struct CapturedFrame {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;
};

/**
 * Copies the IOSurface into tightly packed RGBA8.
 *
 * <p>The window never does this: the surface is handed to Core Animation as it is. Only `--output`
 * reads it back, and only after `EffectEngine::flushAndWait()` so the GPU copy into the surface is
 * complete.
 */
bool readSurface(IOSurfaceRef surface, CapturedFrame* frame) {
  if (surface == nullptr || frame == nullptr) {
    return false;
  }
  const uint32_t width = static_cast<uint32_t>(IOSurfaceGetWidth(surface));
  const uint32_t height = static_cast<uint32_t>(IOSurfaceGetHeight(surface));
  if (width == 0 || height == 0) {
    return false;
  }
  if (IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr) != KERN_SUCCESS) {
    return false;
  }
  const auto* base = static_cast<const uint8_t*>(IOSurfaceGetBaseAddress(surface));
  const size_t stride = IOSurfaceGetBytesPerRow(surface);
  const bool isBgra = coral::AppleIOSurface::formatOf(surface) == coral::EffectPixelFormat::BGRA8;

  CapturedFrame captured;
  captured.width = width;
  captured.height = height;
  captured.rgba.resize(static_cast<size_t>(width) * height * 4u);
  for (uint32_t row = 0; row < height; ++row) {
    const uint8_t* source = base + static_cast<size_t>(row) * stride;
    uint8_t* destination = captured.rgba.data() + static_cast<size_t>(row) * width * 4u;
    for (uint32_t column = 0; column < width; ++column) {
      const uint8_t* pixel = source + static_cast<size_t>(column) * 4u;
      if (isBgra) {
        destination[column * 4 + 0] = pixel[2];
        destination[column * 4 + 1] = pixel[1];
        destination[column * 4 + 2] = pixel[0];
      } else {
        destination[column * 4 + 0] = pixel[0];
        destination[column * 4 + 1] = pixel[1];
        destination[column * 4 + 2] = pixel[2];
      }
      destination[column * 4 + 3] = pixel[3];
    }
  }
  IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);
  *frame = std::move(captured);
  return true;
}

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
    uint32_t renderWidth = options.renderWidth;
    uint32_t renderHeight = options.renderHeight;
    if (renderWidth == 0 || renderHeight == 0) {
      if (!resolveVideoSize(options.mediaPath, &renderWidth, &renderHeight)) {
        renderWidth = 1280;
        renderHeight = 720;
      }
    }

    // The output surface is the target of the effect graph and the object Core Animation displays.
    // It exists before the player does, so `setOutputSurface` can attach it before `prepare()` and
    // the engine is sized to the picture from its first frame.
    auto surface = coral::AppleIOSurface::create(renderWidth, renderHeight,
                                                 coral::EffectPixelFormat::BGRA8);
    if (surface == nullptr) {
      std::fprintf(stderr, "error: could not create a %ux%u IOSurface\n", renderWidth, renderHeight);
      return nullptr;
    }

    auto player = coral::LivePlayer::create(
        options.mediaPath, coral::LivePlayer::Configuration{.audioEnabled = options.audio});
    if (player == nullptr) {
      std::fprintf(stderr, "error: could not create a player for '%s'\n", options.mediaPath.c_str());
      return nullptr;
    }

    auto session = std::unique_ptr<PlayerSession>(new PlayerSession());
    session->mSurface = surface;
    session->mListener = std::make_shared<SessionListener>();

    // The decoded frames enter through the CPU fallback node: the decoder hands the graph NV12/I420
    // planes and the node uploads them to the GPU. The output factory turns the IOSurface into the
    // graph's `AppleIOSurfaceOutputNode`.
    coral::VideoEffectInput effectInput;
    if (options.zeroCopy) {
      effectInput = coral::AppleVideoInput::createZeroCopyInput(renderWidth, renderHeight, 8);
      std::printf("video input: zero-copy AppleIOSurfaceInputNode + AppleVideoFrameSink\n");
    } else {
      effectInput.inputNode = coral::AppleVideoInput::createFrameInput(renderWidth, renderHeight, 8);
      std::printf("video input: CPU fallback VideoFrameInputNode\n");
    }
    if (effectInput.inputNode == nullptr ||
        (options.zeroCopy &&
         (effectInput.frameSink == nullptr || effectInput.decoderSurface == nullptr))) {
      std::fprintf(stderr, "error: could not create the effect input\n");
      return nullptr;
    }
    player->enableVideoEffectPipeline(
        std::make_shared<coral::AppleVideoOutputNodeFactory>(), effectInput);
    if (!player->isVideoEffectPipelineEnabled()) {
      std::fprintf(stderr, "error: the effect video pipeline could not be enabled\n");
      return nullptr;
    }
    if (options.audio && options.mute) {
      player->setVolume(coral::Volume{0.0f, 0.0f});
    }
    player->addListener(session->mListener);
    player->setOutputSurface(surface);

    session->mPlayer = std::move(player);
    session->mPlayer->prepare();

    // Wait for the source to be ready before starting: a player that is told to play while the first
    // period is still being prepared can otherwise race the flush of the initial load.
    const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < readyDeadline) {
      if (session->mPlayer->getPlaybackState() == coral::Player::State::STATE_READY ||
          session->mListener->failed()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    session->mPlayer->play();
    return session;
  }

  [[nodiscard]] coral::LivePlayer* player() const { return mPlayer.get(); }
  [[nodiscard]] const std::shared_ptr<coral::AppleIOSurface>& surface() const { return mSurface; }
  [[nodiscard]] const std::shared_ptr<SessionListener>& listener() const { return mListener; }

  [[nodiscard]] std::shared_ptr<coral::EffectEngine> engine() const {
    return mPlayer != nullptr ? mPlayer->getEffectEngine() : nullptr;
  }

private:
  PlayerSession() = default;

  std::unique_ptr<coral::LivePlayer> mPlayer;
  std::shared_ptr<coral::AppleIOSurface> mSurface;
  std::shared_ptr<SessionListener> mListener;
};

// ==================================================================================================
// PNG output (headless)
// ==================================================================================================

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
  const auto engine = session->engine();
  if (engine == nullptr) {
    std::fprintf(stderr, "error: the effect engine is not available\n");
    return 1;
  }

  const int wanted = options.frames > 0 ? options.frames : 30;
  // The engine renders continuously, so the same decoded frame can be committed several times. The
  // dump samples presentation positions instead: one PNG per ~33 ms of media, which is one per
  // decoded frame for the usual 24-30 fps content and never repeats the same instant.
  constexpr int64_t kCaptureIntervalUs = 33'000;
  int written = 0;
  // The first committed frame can still be the blank one the graph produces before the decoder's
  // first picture arrives; start sampling one frame interval in rather than at position zero.
  int64_t nextCaptureUs = kCaptureIntervalUs;
  auto lastNewFrame = std::chrono::steady_clock::now();

  while (written < wanted) {
    if (session->listener()->failed()) {
      break;
    }
    if (engine->stats().frameCount == 0) {
      if (std::chrono::steady_clock::now() - lastNewFrame >= std::chrono::seconds(10)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }

    const int64_t positionUs = session->player()->getCurrentPositionUs();
    if (positionUs >= nextCaptureUs) {
      // The surface is written during the frame's own GPU submission; wait for it before the CPU map.
      engine->flushAndWait();
      CapturedFrame frame;
      if (!readSurface(session->surface()->iosurface(), &frame)) {
        std::fprintf(stderr, "error: could not read the output IOSurface\n");
        return 1;
      }
      char name[32] = {};
      std::snprintf(name, sizeof(name), "frame_%05d.png", written + 1);
      const auto path = std::filesystem::path(options.outputDirectory) / name;
      if (!writePng(frame, path)) {
        std::fprintf(stderr, "error: cannot write '%s'\n", path.string().c_str());
        return 1;
      }
      std::printf("frame %d: %ux%u at %lldus\n", written + 1, frame.width, frame.height,
                  static_cast<long long>(positionUs));
      std::fflush(stdout);
      ++written;
      nextCaptureUs = positionUs + kCaptureIntervalUs;
      lastNewFrame = std::chrono::steady_clock::now();
      continue;
    }

    if (session->listener()->ended() && written > 0) {
      break;
    }
    if (std::chrono::steady_clock::now() - lastNewFrame >= std::chrono::seconds(10)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
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

} // namespace

// ==================================================================================================
// The view and application delegate
// ==================================================================================================

@interface CoralDemoVideoView : NSView
- (void)setSurface:(IOSurfaceRef)surface;
@end

@implementation CoralDemoVideoView

- (instancetype)initWithFrame:(NSRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self != nil) {
    // The frame is presented by the layer, not by `drawRect:`: the layer's contents is the IOSurface
    // the effect graph writes. `contentsGravity` letterboxes a picture whose aspect differs.
    self.wantsLayer = YES;
    self.layer.backgroundColor = NSColor.blackColor.CGColor;
    self.layer.contentsGravity = kCAGravityResizeAspect;
    self.layer.masksToBounds = YES;
  }
  return self;
}

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
    [NSApp terminate:nil];
  }
}

- (void)setSurface:(IOSurfaceRef)surface {
  if (surface == nullptr) {
    return;
  }
  // Re-assigning the contents is what tells Core Animation that the surface is a new image. The
  // transaction disables the implicit fade a contents change would otherwise animate.
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  self.layer.contents = (__bridge id)surface;
  [CATransaction commit];
}

@end

@interface CoralDemoAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) CoralDemoVideoView* view;
@property(nonatomic, strong) NSTimer* timer;
@property(nonatomic, assign) uint64_t lastFrameCount;
@property(nonatomic, assign) int displayedFrames;
@property(nonatomic, assign) int frameLimit;
@end

@implementation CoralDemoAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;

  double contentWidth = 960;
  double contentHeight = 540;
  if (gSession != nullptr) {
    const double videoWidth = static_cast<double>(gSession->surface()->width());
    const double videoHeight = static_cast<double>(gSession->surface()->height());
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
  self.lastFrameCount = 0;
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
  const auto engine = gSession->engine();
  if (engine == nullptr) {
    [NSApp terminate:nil];
    return;
  }

  // Every committed effect frame updates the surface. Re-assigning it is cheap: no pixels are copied,
  // the layer only re-reads the same GPU allocation.
  const uint64_t frameCount = static_cast<uint64_t>(engine->stats().frameCount);
  if (frameCount > self.lastFrameCount) {
    self.lastFrameCount = frameCount;
    [self.view setSurface:gSession->surface()->iosurface()];
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
  if (reachedEnd && self.displayedFrames > 0) {
    std::printf("displayed %d frame(s)\n", self.displayedFrames);
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
