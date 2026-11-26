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
 * @file video_player_demo_win32.cpp
 *
 * The Windows playback demo: decode a video with CoralPlayback, run its frames through the effect graph
 * and present the composed result in a Win32 window.
 *
 * ## The pipeline
 *
 * CPU upload (the default, and the macOS `--cpu` path):
 *
 * ```
 * ProgressiveMediaSource -> DecodeVideoRenderer -> CpuVideoFrameSink -> VideoFrameInputNode
 *     -> VideoLayoutPass -> SubtitleOverlayNode -> WindowsSwapChainOutputNode -> HWND
 * ```
 *
 * The WMF transform still decodes in hardware when the machine has a hardware MFT; without a
 * `HardwareVideoSurface` it copies the picture into system memory, and `CpuVideoFrameSink` converts
 * the planes for the effect input node. That copy is the price of a stable picture today.
 *
 * `--zero-copy` selects the shared-texture path instead:
 *
 * ```
 * ProgressiveMediaSource -> DecodeVideoRenderer -> WindowsVideoFrameSink (D3D11 video processor)
 *     -> WindowsD3D11InputNode -> VideoLayoutPass -> SubtitleOverlayNode
 *     -> WindowsSwapChainOutputNode -> HWND
 * ```
 *
 * The decoder then allocates its NV12 textures on a Direct3D 11 device the demo creates
 * (`WindowsVideoDevice`), `WindowsVideoFrameSink` converts them to BGRA on the GPU and
 * `WindowsD3D11InputNode` imports the shared texture into the effect engine's Vulkan device, so no
 * decoded pixel visits the CPU. It is *experimental*: the Media Foundation transform hands a frame
 * out before the driver's GPU write into its surface is necessarily complete, and it exposes no
 * fence for that write, so roughly one frame in ten arrives torn. The CPU path above is the default
 * until the decoder can carry a completion fence.
 *
 * This is the Windows counterpart of `video_player_demo.mm`. The player owns the media source, the
 * decoder and the effect graph; the demo supplies the public pieces a Windows host supplies:
 *
 * - a `VideoSurface` over an output `HWND` (`CreateWin32WindowSurface`), which tells the player an
 *   output target exists and how large it is;
 * - a `VideoOutputNodeFactory` (`WindowsVideoOutputNodeFactory`) that turns that surface into the
 *   graph's `WindowsSwapChainOutputNode`. The node presents the composed frame into the window through
 *   the RHI's swapchain - the OpenGL backend uses the window's WGL context, the Vulkan backend uses
 *   `VK_KHR_win32_surface`;
 * - the video input aggregate (`WindowsVideoInput`): the CPU node and `CpuVideoFrameSink` by default,
 *   or the D3D11 input node, the hardware decoder surface and the frame sink for `--zero-copy`.
 *
 * With `--zero-copy`, a machine whose decoder cannot produce textures (no hardware MFT, an old
 * driver, no video processor) never submits a frame; the demo then rebuilds the player on the CPU
 * input after a few seconds instead of presenting a black window. That fallback is also what makes
 * the demo usable on a machine without a hardware decoder at all.
 *
 * The window is an ordinary `WS_OVERLAPPEDWINDOW`: the engine renders at the client area and the
 * swapchain presents there, so the picture follows a resize without a readback. A resize is debounced
 * and then re-attached (`setOutputSurface`), which is the player's late-surface path: it resizes the
 * engine to the new client area and rebuilds the swapchain.
 *
 * The windowed demo plays the sound track by default through CoralAudio; `--no-audio` runs on the
 * video and the standalone clock only, which is what a machine without an output device needs.
 * `--mute` keeps the device open at zero volume. A machine that exposes no audio playback endpoint
 * gets a warning and the video only: the demo owns that fallback, while the player leaves audio
 * device policy to CoralAudio. Esc/Q or closing the window ends the windowed form.
 *
 * ## Usage
 *
 * ```sh
 * video_player_demo.exe                                # assets/video/video720p_5s.mp4, windowed
 * video_player_demo.exe clip.mp4 --size 1280x720
 * video_player_demo.exe clip.mp4 --frames 120          # stop after 120 presented effect frames
 * video_player_demo.exe clip.mp4 --zero-copy           # sample the decoder's GPU texture (experimental)
 * video_player_demo.exe clip.mp4 --seek 2.5          # start at 2.5 seconds
 * video_player_demo.exe clip.mp4 --loop              # repeat the media
 * video_player_demo.exe clip.mp4 --lottie gear.json # composite a Lottie animation
 * video_player_demo.exe clip.mp4 --subtitle sub.srt   # side-load subtitles and burn them in
 * video_player_demo.exe clip.mp4 --subtitle-language zho  # select an embedded text track
 * video_player_demo.exe clip.mp4 --output frames --frames 30   # headless: write PNGs
 * ```
 *
 * `--output <dir>` is the headless form: it does not open a window. The output node is a
 * `ReadbackOutputNode` instead of a swapchain, so a build machine can verify that decoding,
 * composition and the output produced pixels without a display. It writes `frame_00001.png` per
 * sampled presentation position and exits non-zero when it produced no frame.
 *
 * Exit codes: 0 = the media played (and at least one frame was produced), 1 = the player or decoder
 * failed, 2 = bad arguments.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// `sscanf` and `snprintf` are used as they are on the other platforms; the CRT's deprecation warning
// would only ask for the `_s` variants, which are not portable.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <mmdeviceapi.h>
#include <objbase.h>

#include <coral/playback/LivePlayer.h>
#include <coral/playback/LottieOverlay.h>
#include <coral/playback/Player.h>
#include <coral/playback/VideoOutput.h>
#include <coral/playback/win32/Win32WindowSurface.h>
#include <coral/playback/win32/WindowsVideoDevice.h>
#include <coral/playback/win32/WindowsVideoFrameSink.h>
#include <coral/playback/win32/WindowsVideoInput.h>
#include <coral/playback/win32/WindowsVideoOutputNodeFactory.h>

#include <codec/MediaMetadataRetriever.h>

#include <effect/EffectEngine.h>
#include <effect/EffectFrame.h>
#include <effect/ReadbackOutputNode.h>

#include <graphics/Enums.h>
#include <graphics/ImageCodec.h>
#include <graphics/ImageInfo.h>


#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

// ==================================================================================================
// Options
// ==================================================================================================

struct Options {
  std::string mediaPath;
  std::string outputDirectory;
  std::string subtitlePath;
  std::string subtitleLanguage;
  std::string lottiePath;
  int audioTrackIndex = -1;
  int64_t seekUs = -1; // -1 = play from the start
  bool loop = false;
  uint32_t renderWidth = 0;  // 0 = use the video's own size
  uint32_t renderHeight = 0;
  int frames = 0; // 0 = until the media ends
  bool audio = true;
  bool audioSpecified = false;
  bool mute = false;
  bool showWindow = true;
  // The CPU upload is the default: the WMF hardware transform can hand a frame out before its GPU
  // write has completed, which shows up as an occasional torn picture in the zero-copy path. The
  // transform still decodes in hardware when it can, it just copies the picture to system memory.
  bool zeroCopy = false; // --zero-copy samples the shared D3D11 texture instead
  float speed = 1.0f;
};

void printUsage(const char* program) {
  std::printf(
      "usage: %s [media] [options]\n"
      "\n"
      "  media                 file to play; defaults to the asset in assets/video\n"
      "  --size WxH            size of the player's client area (default: the video size)\n"
      "  --frames N            stop after N presented frames (default: play to the end)\n"
      "  --output DIR          write frame_00001.png... into DIR, without a window\n"
      "  --audio               play the audio track (the default in a window)\n"
      "  --no-audio            do not build the audio renderer\n"
      "  --mute                set the audio volume to zero\n"
      "  --subtitle FILE       side-load an SRT/ASS/WebVTT subtitle file and burn it in\n"
      "  --subtitle-language L prefer the embedded text track with language tag L\n"
      "  --lottie FILE         composite a Lottie animation (JSON) over the video\n"
      "  --audio-track N       play the N-th audio track of a multi-audio file\n"
      "  --cpu                 decode through CPU planes and upload them (the default)\n"
      "  --zero-copy           sample the decoder's shared D3D11 texture (experimental: the hardware\n"
      "                        transform can deliver a frame before its GPU write completes, so the\n"
      "                        picture occasionally tears; the CPU path avoids it)\n"
      "  --seek S              seek to S seconds before playback starts\n"
      "  --loop                repeat the media instead of stopping at the end\n"
      "  --speed N             playback speed, e.g. 2.0 (default: 1.0)\n"
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
    if (argument == "--seek") {
      if (++index >= argc) {
        std::fprintf(stderr, "error: --seek needs a position in seconds\n");
        return std::nullopt;
      }
      char* end = nullptr;
      const double seconds = std::strtod(argv[index], &end);
      if (end == argv[index] || *end != '\0' || seconds < 0.0) {
        std::fprintf(stderr, "error: invalid --seek '%s'\n", argv[index]);
        return std::nullopt;
      }
      options.seekUs = static_cast<int64_t>(seconds * 1'000'000.0);
      continue;
    }
    if (argument == "--loop") {
      options.loop = true;
      continue;
    }
    if (argument == "--speed") {
      if (++index >= argc) {
        std::fprintf(stderr, "error: --speed needs a factor\n");
        return std::nullopt;
      }
      char* end = nullptr;
      const float speed = std::strtof(argv[index], &end);
      if (end == argv[index] || *end != '\0' || speed <= 0.0f) {
        std::fprintf(stderr, "error: invalid --speed '%s'\n", argv[index]);
        return std::nullopt;
      }
      options.speed = speed;
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
    if (argument == "--subtitle") {
      if (++index >= argc) {
        std::fprintf(stderr, "error: --subtitle needs a file\n");
        return std::nullopt;
      }
      options.subtitlePath = argv[index];
      continue;
    }
    if (argument == "--subtitle-language") {
      if (++index >= argc) {
        std::fprintf(stderr, "error: --subtitle-language needs a language tag\n");
        return std::nullopt;
      }
      options.subtitleLanguage = argv[index];
      continue;
    }
    if (argument == "--audio-track") {
      if (++index >= argc) {
        std::fprintf(stderr, "error: --audio-track needs an index\n");
        return std::nullopt;
      }
      char* end = nullptr;
      const long value = std::strtol(argv[index], &end, 10);
      if (end == argv[index] || *end != '\0' || value < 0) {
        std::fprintf(stderr, "error: invalid --audio-track '%s'\n", argv[index]);
        return std::nullopt;
      }
      options.audioTrackIndex = static_cast<int>(value);
      continue;
    }
    if (argument == "--lottie") {
      if (++index >= argc) {
        std::fprintf(stderr, "error: --lottie needs a JSON file\n");
        return std::nullopt;
      }
      options.lottiePath = argv[index];
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
 * Reads the display size of the first video track, so the window and the initial client area are
 * sized to the picture before playback starts.
 *
 * The player can only report the size once the decoder has parsed the track, and replacing the output
 * surface after `prepare()` is the late-attach path (it pauses and resumes the engine). The demo knows
 * the file up front, so asking the retriever keeps the surface attached before `prepare()`. A
 * portrait recording carries its rotation in the container, not in the coded frame, so the returned
 * size is the display size: the graph rotates the picture inside it, and a window sized to the coded
 * frame would leave black bars on the sides.
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
    int videoWidth = format->getWidth();
    int videoHeight = format->getHeight();
    const int rotation = format->getRotation();
    if (rotation == 90 || rotation == 270) {
      std::swap(videoWidth, videoHeight);
    }
    if (videoWidth > 0 && videoHeight > 0) {
      *width = static_cast<uint32_t>(videoWidth);
      *height = static_cast<uint32_t>(videoHeight);
      return true;
    }
  }
  return false;
}

/**
 * Whether the first video track carries an HDR transfer function (PQ or HLG). The Windows hardware
 * decoder renegotiates its output to P010 for a 10-bit source, which the zero-copy input does not
 * handle yet, and the failed renegotiation leaves the transform blocked: such sources start on the
 * CPU input, whose NV12 path the platform transform already converts and which is known to work.
 */
bool isHdrMedia(const std::string& path) {
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
    const auto transfer = format->getColorInfo().colorTransfer;
    return transfer == coral::ColorInfo::TransferType::kTransferST2084 ||
           transfer == coral::ColorInfo::TransferType::kTransferHLG;
  }
  return false;
}
// ==================================================================================================
// Headless surface and output (no window)
// ==================================================================================================

/**
 * A `VideoSurface` with no platform object behind it.
 *
 * The headless output is a `ReadbackOutputNode`, which needs no `HWND`; the surface exists only so
 * that the player's `setOutputSurface` has a valid target to attach and a size to query.
 */
class HeadlessSurface final : public coral::VideoSurface {
public:
  HeadlessSurface(const uint32_t width, const uint32_t height) noexcept
      : mWidth(width), mHeight(height) {
  }

  [[nodiscard]] void* getInstance() const override {
    return const_cast<HeadlessSurface*>(this);
  }

  [[nodiscard]] bool isValid() const override {
    return mWidth > 0 && mHeight > 0;
  }

  void setDefaultBufferSize(const int width, const int height) override {
    if (width > 0 && height > 0) {
      mWidth = static_cast<uint32_t>(width);
      mHeight = static_cast<uint32_t>(height);
    }
  }

  void release() override {
  }

private:
  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
};

/**
 * Hands the player a `ReadbackOutputNode` for the headless surface.
 *
 * This is the "host without a window" case of `VideoOutputNodeFactory`: instead of a swapchain, the
 * graph's final frame is copied back to the CPU, where `--output` encodes it as a PNG.
 */
class ReadbackOutputFactory final : public coral::VideoOutputNodeFactory {
public:
  ReadbackOutputFactory(std::shared_ptr<coral::ReadbackOutputNode> node, const uint32_t width,
                        const uint32_t height)
      : mNode(std::move(node)), mWidth(width), mHeight(height) {
  }

  std::shared_ptr<coral::EffectOutputNode> create(
      const std::shared_ptr<coral::VideoSurface>& surface) override {
    if (surface == nullptr || !surface->isValid()) {
      return nullptr;
    }
    return mNode;
  }

  bool querySize(const std::shared_ptr<coral::VideoSurface>& surface, uint32_t* width,
                 uint32_t* height) override {
    if (surface == nullptr || !surface->isValid() || width == nullptr || height == nullptr) {
      return false;
    }
    *width = mWidth;
    *height = mHeight;
    return true;
  }

private:
  std::shared_ptr<coral::ReadbackOutputNode> mNode;
  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
};

// ==================================================================================================
// PNG output (headless)
// ==================================================================================================

bool writePng(const uint32_t width, const uint32_t height, const std::vector<uint8_t>& rgba,
              const std::filesystem::path& path) {
  if (width == 0 || height == 0 || rgba.empty()) {
    return false;
  }
  graphics::DecodedImage image;
  image.info = graphics::ImageInfo(static_cast<int>(width), static_cast<int>(height),
                                   graphics::ColorType::kRGBA_8888,
                                   graphics::AlphaType::kPremultiplied);
  image.pixels = rgba;
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

/** The name the track listing uses for a track kind. */
const char* trackTypeName(const coral::TrackType type) {
  switch (type) {
    case coral::TrackType::Video:
      return "video";
    case coral::TrackType::Audio:
      return "audio";
    case coral::TrackType::Text:
      return "text";
    case coral::TrackType::Metadata:
      return "metadata";
    case coral::TrackType::Unknown:
    default:
      return "unknown";
  }
}

/**
 * Whether Windows exposes a default audio playback endpoint.
 *
 * <p>The demo asks the player for audio before it knows whether the machine has an output device; a
 * headless CI machine or a session without a render endpoint then fails to create the WASAPI stream
 * and never presents a frame. Asking the endpoint up front lets the demo play the video instead. The
 * player itself does not do this: audio focus and device changes are CoralAudio's business.
 */
bool hasAudioPlaybackEndpoint() {
  const HRESULT initialized = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninitialize = SUCCEEDED(initialized);
  bool available = false;
  IMMDeviceEnumerator* enumerator = nullptr;
  if (SUCCEEDED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(&enumerator))) &&
      enumerator != nullptr) {
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
        device != nullptr) {
      available = true;
      device->Release();
    }
    enumerator->Release();
  }
  if (uninitialize) {
    ::CoUninitialize();
  }
  return available;
}

class SessionListener final : public coral::Player::EventListener {
public:
  void onPlaybackStateChanged(const coral::Player::State& state) override {
    if (state == coral::Player::State::STATE_ENDED) {
      mEnded.store(true, std::memory_order_release);
    }
  }

  void onVideoSizeChanged(const int width, const int height) override {
    std::fprintf(stderr, "video size: %dx%d\n", width, height);
  }

  void onTracksChanged(const coral::Tracks& tracks) override {
    // Every track is listed with its selection state: a multi-audio file is verified by seeing two
    // audio tracks and exactly one of them selected.
    for (const auto& group : tracks.groups) {
      for (const auto& track : group.tracks) {
        if (track.format == nullptr) {
          continue;
        }
        const auto flags = static_cast<uint32_t>(track.format->getSelectionFlags());
        const auto forced = static_cast<uint32_t>(coral::MediaSelectionFlags::kMediaSelectionForced);
        const auto isDefault =
            static_cast<uint32_t>(coral::MediaSelectionFlags::kMediaSelectionDefault);
        std::printf("track %s: language='%s' mime='%s' forced=%d default=%d selected=%d\n",
                    trackTypeName(track.type), track.format->getLanguage().c_str(),
                    track.format->getMimeType().c_str(), (flags & forced) != 0,
                    (flags & isDefault) != 0, track.selected ? 1 : 0);
        std::fflush(stdout);
      }
    }
    if (tracksCallback) {
      tracksCallback(tracks);
    }
  }

  std::function<void(const coral::Tracks&)> tracksCallback;

  void onCues(const coral::AVCueGroup& cueGroup) override {
    const auto& cues = cueGroup.getCues();
    const auto positionUs = cueGroup.getPresentationTimeUs();
    if (cues.empty()) {
      std::printf("cues: clear at %lldus\n", static_cast<long long>(positionUs));
      std::fflush(stdout);
      return;
    }
    for (const auto& cue : cues) {
      if (!cue.bitmap.isEmpty()) {
        std::printf("cues: bitmap %dx%d at %lldus\n", cue.bitmap.width, cue.bitmap.height,
                    static_cast<long long>(positionUs));
      } else {
        std::printf("cues: text '%s' at %lldus\n", cue.text.c_str(),
                    static_cast<long long>(positionUs));
      }
    }
    std::fflush(stdout);
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

/** The backend the RHI actually started, which may differ from the requested one after a fallback. */
const char* effectBackendName(const coral::EffectBackend backend) {
  switch (backend) {
    case coral::EffectBackend::OpenGL:
      return "OpenGL";
    case coral::EffectBackend::Vulkan:
      return "Vulkan";
    case coral::EffectBackend::Metal:
      return "Metal";
    case coral::EffectBackend::Default:
    default:
      return "default";
  }
}


/**
 * Tries the zero-copy input first and falls back to the CPU input.
 *
 * <p>The zero-copy path decodes into Direct3D 11 textures the effect graph samples through a shared
 * handle; the CPU path converts the decoder's planes and uploads them. A machine whose decoder cannot
 * produce textures (no hardware MFT, an old driver, no video processor) never submits a frame on the
 * zero-copy path, so the demo rebuilds the player on the CPU input instead of presenting a black window.
 */
class PlayerSession {
public:
  ~PlayerSession() {
    // The graph, its input node and the sink all reference the D3D11 device; release them first.
    if (mPlayer != nullptr) {
      mPlayer->release();
      mPlayer.reset();
    }
    mSink.reset();
    mDevice.reset();
  }

  /**
   * Builds the player and attaches its output before `prepare()`.
   *
   * @param window the player window, or null for the headless readback output.
   */
  static std::unique_ptr<PlayerSession> create(const Options& options, HWND window) {
    auto session = createWithVideoFallback(options, window);
    if (session == nullptr || !options.audio) {
      return session;
    }
    // A device that disappears between the endpoint probe and the stream open still makes the player
    // fail: retry video-only instead of waiting forever for a frame.
    if (session->waitForFirstFrame(3s)) {
      return session;
    }
    std::fprintf(stderr, "warning: no frame was presented with audio enabled; retrying video-only\n");
    session.reset();
    Options videoOnly = options;
    videoOnly.audio = false;
    videoOnly.mute = false;
    return createWithVideoFallback(videoOnly, window);
  }

private:
  /** The input selection and its fallbacks; audio is checked by `create` above. */
  static std::unique_ptr<PlayerSession> createWithVideoFallback(const Options& options, HWND window) {
    if (options.zeroCopy && isHdrMedia(options.mediaPath)) {
      std::fprintf(stderr,
                   "warning: the zero-copy input does not support HDR (10-bit) sources yet; using the "
                   "CPU input\n");
    } else if (options.zeroCopy) {
      const auto device = coral::WindowsVideoDevice::create();
      if (device == nullptr) {
        std::fprintf(stderr,
                     "warning: no Direct3D 11 device with video support on this host; using the CPU "
                     "input\n");
      } else {
        auto session = build(options, window, device);
        if (session != nullptr && session->waitForHardwareFrame(4s)) {
          return session;
        }
        std::fprintf(stderr,
                     "warning: the zero-copy input produced no decoded frame; falling back to the CPU "
                     "input\n");
        session.reset();
      }
    }
    return build(options, window, nullptr);
  }

public:

  [[nodiscard]] coral::LivePlayer* player() const { return mPlayer.get(); }
  [[nodiscard]] const std::shared_ptr<coral::VideoSurface>& surface() const { return mSurface; }
  [[nodiscard]] const std::shared_ptr<SessionListener>& listener() const { return mListener; }
  [[nodiscard]] const std::shared_ptr<coral::ReadbackOutputNode>& readback() const {
    return mReadback;
  }
  [[nodiscard]] const std::shared_ptr<coral::WindowsVideoFrameSink>& windowsSink() const {
    return mSink;
  }

  [[nodiscard]] std::shared_ptr<coral::EffectEngine> engine() const {
    return mPlayer != nullptr ? mPlayer->getEffectEngine() : nullptr;
  }

private:
  PlayerSession() = default;

  static std::unique_ptr<PlayerSession> build(const Options& options, HWND window,
                                              const std::shared_ptr<coral::WindowsVideoDevice>& device) {
    uint32_t renderWidth = options.renderWidth;
    uint32_t renderHeight = options.renderHeight;
    if (renderWidth == 0 || renderHeight == 0) {
      if (!resolveVideoSize(options.mediaPath, &renderWidth, &renderHeight)) {
        renderWidth = 1280;
        renderHeight = 720;
      }
    }

    // The output target exists before the player does, so `setOutputSurface` can attach it before
    // `prepare()` and the engine is sized to the picture from its first frame.
    std::shared_ptr<coral::VideoSurface> surface;
    std::shared_ptr<coral::VideoOutputNodeFactory> factory;
    std::shared_ptr<coral::ReadbackOutputNode> readback;
    if (window != nullptr) {
      surface = coral::CreateWin32WindowSurface(window);
      if (surface == nullptr) {
        std::fprintf(stderr, "error: the window is not a valid player surface\n");
        return nullptr;
      }
      factory = std::make_shared<coral::WindowsVideoOutputNodeFactory>();
    } else {
      readback = coral::ReadbackOutputNode::create();
      if (readback == nullptr) {
        std::fprintf(stderr, "error: could not create the readback output\n");
        return nullptr;
      }
      surface = std::make_shared<HeadlessSurface>(renderWidth, renderHeight);
      factory = std::make_shared<ReadbackOutputFactory>(readback, renderWidth, renderHeight);
    }

    // The zero-copy input samples the decoder's D3D11 textures; the CPU input converts the decoder's
    // planes and uploads them through `VideoFrameInputNode` on the effect render thread.
    const coral::VideoEffectInput input =
        device != nullptr
            ? coral::WindowsVideoInput::createZeroCopyInput(device->device(), renderWidth, renderHeight)
            : coral::WindowsVideoInput::createCpuInput(renderWidth, renderHeight);
    if (input.inputNode == nullptr || input.frameSink == nullptr) {
      std::fprintf(stderr, "error: the %s video input could not be created\n",
                   device != nullptr ? "zero-copy" : "CPU");
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
    session->mReadback = readback;
    session->mListener = std::make_shared<SessionListener>();
    session->mSink = std::dynamic_pointer_cast<coral::WindowsVideoFrameSink>(input.frameSink);
    session->mDevice = device;

    player->enableVideoEffectPipeline(factory, input);
    if (!player->isVideoEffectPipelineEnabled()) {
      std::fprintf(stderr, "error: the effect video pipeline could not be enabled\n");
      return nullptr;
    }
    std::printf("video input: %s\n",
                device != nullptr
                    ? "zero-copy WindowsD3D11InputNode + WindowsVideoFrameSink (D3D11 decode)"
                    : "CPU fallback VideoFrameInputNode + CpuVideoFrameSink");
    std::fflush(stdout);
    if (!options.lottiePath.empty()) {
      std::ifstream file(options.lottiePath, std::ios::binary);
      const std::string document((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
      if (document.empty()) {
        std::fprintf(stderr, "error: the Lottie file is empty or unreadable: %s\n",
                     options.lottiePath.c_str());
        return nullptr;
      }
      coral::LottieOverlayConfig overlay;
      overlay.name = "demo";
      overlay.json = document;
      overlay.scale = 0.35f;
      // The demo's Lottie clips are a few seconds long at 60 fps, which is more than the default
      // short-loop pre-render limit of 120 frames.
      overlay.maxPreRenderFrames = 600;
      if (!player->addLottieOverlay(overlay)) {
        std::fprintf(stderr, "error: the Lottie overlay could not be added\n");
        return nullptr;
      }
      std::printf("lottie: %s\n", options.lottiePath.c_str());
      std::fflush(stdout);
    }
    if (options.audio && options.mute) {
      player->setVolume(coral::Volume{0.0f, 0.0f});
    }
    if (!options.subtitleLanguage.empty()) {
      // The selector scores a text track against the preferred language; an embedded forced track whose
      // language differs from the audio is otherwise left unselected, which is ExoPlayer's behaviour.
      player->setPreferredTextLanguage(options.subtitleLanguage);
    }
    if (!options.subtitlePath.empty()) {
      player->setSubtitleFile(options.subtitlePath);
    }
    player->addListener(session->mListener);
    player->setOutputSurface(surface);

    session->mPlayer = std::move(player);
    if (options.speed != 1.0f) {
      session->mPlayer->setPlaybackParameters(coral::Parameters(options.speed));
    }
    if (options.audioTrackIndex >= 0) {
      // `selectTrack` needs the track list, which the player dispatches once the first period is
      // prepared. Applying the choice from that callback is where the list is guaranteed to exist.
      const auto applied = std::make_shared<std::atomic<bool>>(false);
      session->mListener->tracksCallback =
          [player = session->mPlayer.get(), index = options.audioTrackIndex, applied](
              const coral::Tracks& tracks) {
            if (applied->exchange(true, std::memory_order_acq_rel)) {
              return;
            }
            int seen = 0;
            for (const auto& group : tracks.groups) {
              for (const auto& track : group.tracks) {
                if (track.type != coral::TrackType::Audio) {
                  continue;
                }
                if (seen++ == index) {
                  player->selectTrack(track.rendererIndex, track.groupIndex, track.trackIndex);
                  std::printf("audio track: %d selected\n", index);
                  std::fflush(stdout);
                  return;
                }
              }
            }
          };
    }
    session->mPlayer->prepare();

    // Wait for the source to be ready before starting: a player that is told to play while the first
    // period is still being prepared can otherwise race the flush of the initial load.
    const auto readyDeadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < readyDeadline) {
      if (session->mPlayer->getPlaybackState() == coral::Player::State::STATE_READY ||
          session->mListener->failed()) {
        break;
      }
      std::this_thread::sleep_for(10ms);
    }
    if (options.loop) {
      session->mPlayer->setRepeatMode(coral::RepeatMode::REPEAT_MODE_ONE);
    }
    if (options.seekUs > 0) {
      session->mPlayer->seekTo(options.seekUs);
    }
    session->mPlayer->play();
    if (options.seekUs > 0) {
      // The seek is applied asynchronously; let it land before frames start being captured, so a
      // headless run does not write pictures from before the requested position.
      const auto seekDeadline = std::chrono::steady_clock::now() + 5s;
      while (std::chrono::steady_clock::now() < seekDeadline &&
             session->mPlayer->getCurrentPositionUs() + 100'000 < options.seekUs) {
        std::this_thread::sleep_for(5ms);
      }
    }

    // A player can reach READY even when the effect engine failed to start (no GPU device, a driver
    // that refuses the swapchain). That failure is reported through the engine's diagnostics rather
    // than through the playback state, so it is checked here instead of surfacing as "no frame".
    const auto engine = session->engine();
    if (engine == nullptr) {
      std::fprintf(stderr, "error: the effect engine is not available\n");
      return nullptr;
    }
    const auto startDeadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < startDeadline) {
      if (engine->isRunning() || session->mListener->failed()) {
        break;
      }
      std::this_thread::sleep_for(10ms);
    }
    if (!engine->isRunning() && !session->mListener->failed()) {
      std::fprintf(stderr, "error: the effect engine did not start: %s\n",
                   engine->diagnostics().c_str());
      return nullptr;
    }
    if (session->mListener->failed()) {
      std::fprintf(stderr, "error: %s\n", session->mListener->error().c_str());
      return nullptr;
    }
    std::printf("effect backend: %s\n", effectBackendName(engine->backend()));
    std::fflush(stdout);
    return session;
  }

  /** Waits for the first frame the effect graph presented. */
  [[nodiscard]] bool waitForFirstFrame(const std::chrono::milliseconds timeout) const {
    const auto engine = mPlayer != nullptr ? mPlayer->getEffectEngine() : nullptr;
    if (engine == nullptr) {
      return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (engine->stats().frameCount > 0) {
        return true;
      }
      if (mListener != nullptr && mListener->failed()) {
        return false;
      }
      std::this_thread::sleep_for(5ms);
    }
    return engine->stats().frameCount > 0;
  }

  /** Waits for the first frame the hardware sink handed to the graph. */
  [[nodiscard]] bool waitForHardwareFrame(const std::chrono::milliseconds timeout) const {
    if (mSink == nullptr) {
      return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (mSink->submittedFrameCount() > 0) {
        return true;
      }
      if (mListener != nullptr && mListener->failed()) {
        return false;
      }
      std::this_thread::sleep_for(5ms);
    }
    return mSink->submittedFrameCount() > 0;
  }

  std::unique_ptr<coral::LivePlayer> mPlayer;
  std::shared_ptr<coral::VideoSurface> mSurface;
  std::shared_ptr<coral::ReadbackOutputNode> mReadback;
  std::shared_ptr<SessionListener> mListener;
  std::shared_ptr<coral::WindowsVideoFrameSink> mSink;
  std::shared_ptr<coral::WindowsVideoDevice> mDevice;
};

/** Prints what the hardware sink did, so a run can be told apart from the CPU fallback. */
void printVideoStatistics(const std::shared_ptr<coral::WindowsVideoFrameSink>& sink) {
  if (sink == nullptr) {
    return;
  }
  std::printf("video frames: %llu submitted (%llu direct, %llu converted, %llu dropped), "
              "zero-copy import: %s\n",
              static_cast<unsigned long long>(sink->submittedFrameCount()),
              static_cast<unsigned long long>(sink->directFrameCount()),
              static_cast<unsigned long long>(sink->convertedFrameCount()),
              static_cast<unsigned long long>(sink->droppedFrameCount()),
              sink->isZeroCopy() ? "yes" : "no");
  std::fflush(stdout);
}

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

  auto session = PlayerSession::create(options, nullptr);
  if (session == nullptr) {
    return 1;
  }
  const auto engine = session->engine();
  const auto readback = session->readback();
  if (engine == nullptr || readback == nullptr) {
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
  if (options.seekUs > 0) {
    // Frames queued before the seek landed still carry the old positions; start sampling just past
    // the requested position instead of writing them into the dump.
    nextCaptureUs = options.seekUs + kCaptureIntervalUs;
  }
  auto lastNewFrame = std::chrono::steady_clock::now();

  while (written < wanted) {
    if (session->listener()->failed()) {
      break;
    }

    coral::EffectFrame frame;
    std::vector<uint8_t> pixels;
    // The callback runs on the render thread once the frame's readback has completed, so this wait
    // returns pixels that are already in their final order and need no GPU flush of their own.
    if (!readback->waitForFrame(&frame, &pixels, 1000)) {
      if (session->listener()->ended() && written > 0) {
        break;
      }
      if (std::chrono::steady_clock::now() - lastNewFrame >= 10s) {
        break;
      }
      continue;
    }

    const int64_t positionUs = frame.timestamp().toTicks(1'000'000);
    if (positionUs < nextCaptureUs) {
      lastNewFrame = std::chrono::steady_clock::now();
      continue;
    }char name[32] = {};
    std::snprintf(name, sizeof(name), "frame_%05d.png", written + 1);
    const auto path = std::filesystem::path(options.outputDirectory) / name;
    if (!writePng(frame.width(), frame.height(), pixels, path)) {
      std::fprintf(stderr, "error: cannot write '%s'\n", path.string().c_str());
      return 1;
    }
    std::printf("frame %d: %ux%u at %lldus\n", written + 1, frame.width(), frame.height(),
                static_cast<long long>(positionUs));
    std::fflush(stdout);
    ++written;
    nextCaptureUs = positionUs + kCaptureIntervalUs;
    lastNewFrame = std::chrono::steady_clock::now();

    if (session->listener()->ended() && written > 0) {
      break;
    }
  }

  if (session->listener()->failed()) {
    std::fprintf(stderr, "error: %s\n", session->listener()->error().c_str());
    return 1;
  }
  printVideoStatistics(session->windowsSink());
  std::printf("wrote %d frame(s) to %s\n", written, options.outputDirectory.c_str());
  return written > 0 ? 0 : 1;
}

// ==================================================================================================
// Win32 window
// ==================================================================================================

constexpr wchar_t kWindowClass[] = L"CoralPlaybackVideoPlayerDemo";
constexpr wchar_t kWindowTitle[] = L"CoralPlayback - video_player_demo";

/** Set by `WM_SIZE`; the message loop re-attaches the surface after the drag settles. */
std::atomic<bool> gResizePending{false};
/** `GetTickCount64()` of the latest `WM_SIZE`, for the debounce. */
std::atomic<uint64_t> gLastResizeTick{0};

uint32_t clientWidth(HWND window) {
  RECT client = {};
  if (::GetClientRect(window, &client) == FALSE) {
    return 0;
  }
  return static_cast<uint32_t>(std::max<LONG>(0, client.right - client.left));
}

uint32_t clientHeight(HWND window) {
  RECT client = {};
  if (::GetClientRect(window, &client) == FALSE) {
    return 0;
  }
  return static_cast<uint32_t>(std::max<LONG>(0, client.bottom - client.top));
}

LRESULT CALLBACK windowProc(HWND window, const UINT message, const WPARAM wParam,
                            const LPARAM lParam) {
  switch (message) {
    case WM_SIZE: {
      // Re-attaching the surface pauses the engine and rebuilds the swapchain, so it is debounced in
      // the message loop rather than done for every step of a drag.
      gLastResizeTick.store(::GetTickCount64(), std::memory_order_release);
      gResizePending.store(true, std::memory_order_release);
      return 0;
    }
    case WM_CLOSE: {
      ::DestroyWindow(window);
      return 0;
    }
    case WM_DESTROY: {
      ::PostQuitMessage(0);
      return 0;
    }
    case WM_KEYDOWN: {
      if (wParam == VK_ESCAPE || wParam == 'Q') {
        ::PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
      }
      break;
    }
    default:
      break;
  }
  return ::DefWindowProcW(window, message, wParam, lParam);
}

HWND createWindow(const uint32_t width, const uint32_t height) {
  const HINSTANCE instance = ::GetModuleHandleW(nullptr);

  WNDCLASSEXW windowClass = {};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = windowProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = ::LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  windowClass.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
  windowClass.lpszClassName = kWindowClass;
  if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return nullptr;
  }

  // The client area is the picture: the RHI renders at exactly this size and the swapchain presents
  // it without a scale, so the frame is not resampled by the window.
  RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
  ::AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
  HWND window = ::CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                                  rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
  if (window == nullptr) {
    return nullptr;
  }
  ::ShowWindow(window, SW_SHOW);
  ::UpdateWindow(window);
  ::SetForegroundWindow(window);
  return window;
}

std::unique_ptr<PlayerSession> gSession;

// ==================================================================================================
// Windowed playback
// ==================================================================================================

int runWindowed(const Options& options, HWND window) {
  gSession = PlayerSession::create(options, window);
  if (gSession == nullptr) {
    return 1;
  }
  const auto engine = gSession->engine();
  if (engine == nullptr) {
    std::fprintf(stderr, "error: the effect engine is not available\n");
    return 1;
  }

  uint64_t lastFrameCount = 0;
  int displayedFrames = 0;
  int64_t lastPositionUs = 0;
  int exitCode = 0;
  auto lastNewFrame = std::chrono::steady_clock::now();
  bool running = true;
  // The graph renders before playback starts as well (the output is attached and the engine is
  // running from `prepare()`), and those first frames are blank. Counting starts at the first
  // position the media clock reports, so `--frames N` counts pictures of the media, not warm-up.
  bool counting = false;
  // `createWindow` reports a WM_SIZE before the player exists; that one must not re-attach.
  gResizePending.store(false, std::memory_order_release);

  while (running) {
    MSG message = {};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
      if (message.message == WM_QUIT) {
        running = false;
        break;
      }
      ::TranslateMessage(&message);
      ::DispatchMessageW(&message);
    }
    if (!running) {
      break;
    }
    if (::IsWindow(window) == FALSE) {
      break;
    }

    // The player follows the client area: after a resize settles, re-attach the same window surface,
    // which resizes the effect engine and rebuilds the swapchain at the new size.
    if (gResizePending.load(std::memory_order_acquire) &&
        ::GetTickCount64() - gLastResizeTick.load(std::memory_order_acquire) >= 200) {
      gResizePending.store(false, std::memory_order_release);
      if (gSession != nullptr && engine->isRunning()) {
        const uint32_t width = clientWidth(window);
        const uint32_t height = clientHeight(window);
        // A minimized window has no client area, and a swapchain of zero size is refused; the restore
        // reports another `WM_SIZE`, which re-attaches at the size the window comes back with.
        if (width > 0 && height > 0) {
          gSession->player()->setOutputSurface(gSession->surface());
          std::printf("resized to %ux%u\n", width, height);
          std::fflush(stdout);
          lastNewFrame = std::chrono::steady_clock::now();
        }
      }
    }

    const auto now = std::chrono::steady_clock::now();
    if (!counting && gSession->player()->getCurrentPositionUs() > 0) {
      counting = true;
      lastFrameCount = engine->stats().frameCount;
      lastNewFrame = now;
    }
    const uint64_t frameCount = engine->stats().frameCount;
    if (counting && frameCount > lastFrameCount) {
      lastFrameCount = frameCount;
      ++displayedFrames;
      lastNewFrame = now;
    }

    if (gSession->listener()->failed()) {
      std::fprintf(stderr, "error: %s\n", gSession->listener()->error().c_str());
      exitCode = 1;
      break;
    }

    if (options.frames > 0 && displayedFrames >= options.frames) {
      std::printf("displayed %d frame(s)\n", displayedFrames);
      break;
    }

    const int64_t positionUs = gSession->player()->getCurrentPositionUs();
    if (options.loop && lastPositionUs > 0 && positionUs + 500'000 < lastPositionUs) {
      std::printf("loop: restarted at %.2fs\n", static_cast<double>(positionUs) / 1'000'000.0);
      std::fflush(stdout);
    }
    lastPositionUs = positionUs;
    const int64_t duration = gSession->player()->getDurationUs();
    // With `--loop` reaching the end restarts the media instead of ending the run.
    const bool ended =
        gSession->listener()->ended() ||
        (!options.loop && duration > 0 && positionUs >= duration);
    if (ended && displayedFrames > 0) {
      std::printf("displayed %d frame(s)\n", displayedFrames);
      break;
    }

    if (now - lastNewFrame >= 10s) {
      std::fprintf(stderr, "error: no frame was presented for 10s: %s\n",
                   engine->diagnostics().c_str());
      exitCode = 1;
      break;
    }

    // The message loop is pumped every millisecond; the engine renders on its own thread, so this
    // only has to be responsive to close, resize and the end of the media.
    ::MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }

  printVideoStatistics(gSession->windowsSink());
  gSession.reset();
  return exitCode;
}

} // namespace

// ==================================================================================================
// main
// ==================================================================================================

int main(int argc, char** argv) {
  auto options = parseOptions(argc, argv);
  if (!options.has_value()) {
    return 2;
  }
  if (!std::filesystem::exists(options->mediaPath)) {
    std::fprintf(stderr, "error: media file not found: %s\n", options->mediaPath.c_str());
    return 2;
  }
  if (!options->subtitlePath.empty() && !std::filesystem::exists(options->subtitlePath)) {
    std::fprintf(stderr, "error: subtitle file not found: %s\n", options->subtitlePath.c_str());
    return 2;
  }
  if (!options->lottiePath.empty() && !std::filesystem::exists(options->lottiePath)) {
    std::fprintf(stderr, "error: Lottie file not found: %s\n", options->lottiePath.c_str());
    return 2;
  }
  // A machine without a render endpoint (a headless CI host, a session whose device was removed)
  // cannot create the WASAPI stream; play the video instead of failing to present a frame. The
  // player deliberately leaves audio device policy to CoralAudio.
  if (options->audio && !hasAudioPlaybackEndpoint()) {
    std::fprintf(stderr, "warning: no audio playback endpoint is available; playing video only\n");
    options->audio = false;
    options->mute = false;
  }

  std::printf("media: %s\n", options->mediaPath.c_str());
  std::printf("audio: %s\n", options->audio ? "on" : "off");
  std::printf("speed: %.2fx\n", options->speed);
  if (options->seekUs > 0) {
    std::printf("seek: %.2fs\n", static_cast<double>(options->seekUs) / 1'000'000.0);
  }
  if (options->loop) {
    std::printf("loop: on\n");
  }
  if (!options->subtitlePath.empty()) {
    std::printf("subtitle file: %s\n", options->subtitlePath.c_str());
  }
  if (!options->subtitleLanguage.empty()) {
    std::printf("subtitle language: %s\n", options->subtitleLanguage.c_str());
  }
  if (!options->showWindow) {
    std::printf("output: readback node, headless\n");
    return runHeadless(*options);
  }
  std::printf("output: WindowsSwapChainOutputNode over a Win32 window\n");

  // The presented image is rendered at the client area's physical pixels; without DPI awareness the
  // window is scaled by the compositor and the picture is resampled.
  ::SetProcessDPIAware();

  uint32_t windowWidth = options->renderWidth;
  uint32_t windowHeight = options->renderHeight;
  if (windowWidth == 0 || windowHeight == 0) {
    if (!resolveVideoSize(options->mediaPath, &windowWidth, &windowHeight)) {
      windowWidth = 1280;
      windowHeight = 720;
    }
  }

  const HWND window = createWindow(windowWidth, windowHeight);
  if (window == nullptr) {
    std::fprintf(stderr, "error: the player window could not be created\n");
    return 1;
  }
  {
    RECT client = {};
    ::GetClientRect(window, &client);
    std::printf("window: client %ldx%ld\n", client.right - client.left,
                client.bottom - client.top);
  }

  const int exitCode = runWindowed(*options, window);
  if (::IsWindow(window) != FALSE) {
    ::DestroyWindow(window);
  }
  return exitCode;
}

