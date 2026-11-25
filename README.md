# CoralPlayback

The player framework. It turns a media item into a `MediaSource`, pulls samples from the renderers,
composes the video through the effect graph, burns subtitles, and drives audio playback. It is the
counterpart of Media3/ExoPlayer's player module: the playback engine and its media/effect adapter, not
the composition or export core of CoralCompose.

CoralPlayback depends on CoralCore (`core`, `event`, `utils`, `minilog`), CoralAudio (`audio`),
CoralMedia (`codec`, `media`, `datasource`, `extractor`), CoralGraphics (`graphics`, `effect`) and
CoralSubtitle (`subtitle`). It does not depend on CoralUI or CoralCompose.

The repository is both a standalone CMake project and the source of the playback library an
application links.

## Layout

```
coral/playback/         public player, renderer, clock and output API (<coral/playback/...>)
coral/source/           MediaSource, MediaPeriod, Timeline and the progressive source
coral/selection/        track selection
coral/upstream/         allocation, loading and bandwidth adapters
coral/analytics/        the analytics collector and listener
src/details/            AvLivePlayer, the renderers, the effect graph and the layout pass
src/source/             the media-source implementation
src/selection/          the track-selection implementation
src/upstream/           the loading implementation
src/platform/           the platform bridges (Win32 window, Android surface, Apple IOSurface)
tests/                  the unit suites
examples/               the standalone Cocoa demo and its IOSurface output factory
assets/                 the fixtures of the tests and the demo
scripts/                the bootstrap tools that fetch the dependency repositories
```

`Player` is the platform-independent interface. `LivePlayer` is the implementation an application
creates: it builds the renderers for the selected tracks, owns the playback thread and exposes the
media source, subtitle, volume and event-listener API.

Video presentation is the effect graph:

```
ProgressiveMediaSource -> DecodeVideoRenderer -> VideoFrameInputNode
    -> VideoLayoutPass -> SubtitleOverlayNode -> EffectOutputNode
```

The last step is platform-specific, and `VideoOutputNodeFactory` is the seam: the application hands
the player a `VideoSurface`, and the factory returns the `EffectOutputNode` that receives the composed
frames. The Windows bridge returns a swap-chain output; Android returns a surface output; macOS and
iOS return an IOSurface output (`AppleIOSurfaceOutputNode`) that Core Animation, Metal or a video
encoder consumes without a CPU copy. A host without a window (tests, export) can return
`ReadbackOutputNode` or `TextureOutputNode` instead.

## Building

CoralPlayback is a standalone CMake project. Its dependency repositories are declared in `.gitmodules`
but are not part of this checkout, so fetch them first with the bootstrap tool for the host:

macOS / Linux:

```sh
./scripts/bootstrap.sh
```

Windows PowerShell:

```powershell
.\scripts\bootstrap.ps1
```

If the PowerShell execution policy blocks local scripts, run it through
`powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap.ps1`.

The tool reads `.gitmodules` and, for every entry, clones the repository from its default remote branch
into the declared path. An existing checkout is updated with a fast-forward pull, and a detached
submodule checkout is switched back to the remote default branch. When a parent repository needs nested
repositories, the tool initializes those too (Minilog for CoralCore, zlib for libz and GoogleTest for
libgtest). Nested repositories that the dependencies mark as `update = none` are skipped by their own
recursive update, because this project supplies those targets itself. The tool is idempotent and stops
with an error instead of discarding local changes.

Because the checkouts follow the remote branches rather than the commits recorded by this repository,
the dependency directories are untracked; `.gitignore` excludes them.

The repositories it manages are:

| Path | Contents |
| --- | --- |
| `external/CoralCore` | `utils`, `core`, `event` and Minilog |
| `external/CoralAudio` | the audio graph and the platform audio stream |
| `external/CoralMedia` | codec, media, datasource, extractor and muxer |
| `external/CoralGraphics` | graphics, the RHI backends and the effect render graph |
| `external/CoralSubtitle` | the subtitle model and renderer |
| `third_party/libz` | zlib, used by the CoralMedia codec |
| `third_party/libyuv` | YUV conversion for the effect input |
| `third_party/libgtest` | GoogleTest for the test suites |

Requirements: Git, CMake 3.19 or newer and a C++20 compiler. macOS needs the Xcode command line tools
(Objective-C++ and the Metal/OpenGL SDKs), Linux needs the usual graphics/ALSA development packages
for the frameworks it links, and Windows needs Visual Studio with the C++ workload.

The standalone build is three steps:

```sh
# 1. Configure the project
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 2. Build the library, the tests and the demo
cmake --build build

# 3. Run the tests
cmake --build build --target check
# or select the suite in CTest
ctest --test-dir build -R test_playback_source --output-on-failure
```

On macOS, with the Xcode command line tools, the default generator works:

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
cmake --build build --target check
```

The CMake target is `playback` and the artifact is `coral-playback`: the default build produces
`libcoral-playback.a`, and `-DBUILD_SHARED_LIBS=ON` produces `libcoral-playback.dylib` (`.so` on Linux,
`coral-playback.dll` with an import library on Windows). `cmake --install` writes the library to `lib/`
(the DLL to `bin/`) and the public headers to `include/coral/...`, so a client compiles against
`<coral/playback/...>` with `include/` on its header path.

The configure locates the shared third-party tree itself, in this order, and uses the first candidate
that has the wrapper it needs:

1. `third_party/` — a standalone checkout (and what CLion sees first);
2. `../../third_party` — the application tree, two levels above `frameworks/CoralPlayback`;
3. `external/CoralMedia/third_party`, `external/CoralCore/third_party` and
   `external/CoralGraphics/third_party` — the copies the framework submodules carry.

`-DCORAL_SHARED_THIRD_PARTY_DIR=<directory>` overrides the search; the directory has to contain
`libgtest/coral`, `libz/coral` and `libyuv`.

When CoralPlayback is added to an application as a subproject, the application provides CoralCore,
CoralAudio, CoralMedia, CoralGraphics, CoralSubtitle and the third-party targets first; the
`NOT TARGET` guards in the root `CMakeLists.txt` then skip the submodules and no dependency is
configured twice. The tests and the demo are built only when CoralPlayback is the top-level project.

### CLion and other IDEs

Run `scripts/bootstrap.sh` (or `scripts/bootstrap.ps1` on Windows) once, then open the repository root
in CLion as a CMake project. The configure reports the tree it picked:

```
-- CoralPlayback third-party tree: /path/to/CoralPlayback/third_party
```

If GoogleTest is missing, the configure does not stop: it prints a warning and builds the library
without the tests, so an incomplete IDE checkout can still be used. Use `-DCORAL_BUILD_TESTS=OFF` in
the CLion CMake profile when the tests are not wanted at all.

## macOS

The playback core is portable; the pieces that differ are the output node, the audio stream and the
decoder, each supplied by the framework below CoralPlayback:

- **Decoder and demuxer** — CoralMedia uses VideoToolbox and AudioToolbox through its Darwin codec and
  extractor, so H.264/AAC in MP4 plays with the platform decoders.
- **Audio output** — CoralAudio's Core Audio (AUHAL) backend opens the default output device. It
  converts between the media format and the device format, so a 44.1 kHz track plays on a 48 kHz
  device. `CORAL_COREAUDIO_DEVICE=<name or UID>` selects a device. The player builds the audio
  renderer by default; `LivePlayer::Configuration::audioEnabled` lets a video-only host leave the
  device alone instead. The demo's headless `--output` mode does that unless `--audio` is passed.

  The macOS audio decoder returns the queued remainder of an AAC track in one buffer. The audio sink
  holds one second, so a renderer that wrote that buffer whole had it rejected and retried forever:
  playback stayed in `BUFFERING` and the position never moved. `DecodeAudioRenderer` now splits an
  output buffer into sink-sized chunks, which is also what makes it independent of the output
  granularity of any decoder. `check-audio` exercises this path end to end.
- **Video output** — the library provides the platform bridges for Win32, Android and Apple. On macOS
  and iOS an `AppleIOSurface` is the output target: `AppleVideoOutputNodeFactory` turns it into the
  graph's `AppleIOSurfaceOutputNode`, which copies the composed frame into the surface on the GPU.
  The host then presents that surface without a readback — `examples/video_player_demo.mm` assigns it
  to a layer-backed `NSView`'s `CALayer.contents`, and an export session would hand the same surface
  to its encoder.
- **Video input** — `DecodeVideoRenderer` never inspects a platform frame. It hands each decoded
  `AVPixelBuffer` to a `VideoFrameSink`, and the platform implementation converts or publishes it.
  A `VideoEffectInput` aggregates the graph's input node, the decoder surface and the sink (plus an
  optional platform graphics context) so the host has one entry point. The default CPU sink
  (`CpuVideoFrameSink`) converts I420/NV12 planes and uploads them through `VideoFrameInputNode`;
  Android adds a pull-model SurfaceTexture sink that only releases the codec buffer while
  `AndroidSurfaceTextureInputNode` samples the texture on the render thread. Apple's
  `AppleVideoInput::createZeroCopyInput` aggregates `AppleHardwareVideoSurface`,
  `AppleVideoFrameSink` and `AppleIOSurfaceInputNode`: VideoToolbox decodes into an IOSurface, the
  sink converts the current NV12 surface to one BGRA IOSurface on the GPU (a small ring), and the
  input node imports it in place. A bi-planar `AppleIOSurfaceInputNode` can later remove that
  conversion without changing the player core.

The standalone configure enables Objective-C++ before the dependencies are added, which is what the
Darwin sources of CoralCore, CoralMedia and CoralSubtitle need. CoralGraphics' Metal frame clock uses
`CADisplayLink`, declared from macOS 14, so pass `-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0` if the configure
warns about an older target.

## Tests

The suites live in `tests/` and are built only for a top-level configure. They read their fixtures
from `assets/`:

```
assets/video/video720p_5s.mp4       source playback, seeks, merge and subtitle side-loading
assets/subtitle/sample.srt          English side-loaded cues
assets/subtitle/sample_zho.srt      Chinese side-loaded cues
```

A test whose fixture is not in the checkout skips rather than fails, so the suite is usable in a
partial checkout. `CORAL_TEST_DATA_DIR` points the build at `assets/` (or `../../assets` in an
application tree); the tests that need CoralGraphics' Lottie and font resources compile in the
CoralGraphics checkout path.

```sh
cmake --build build --target check
# the audio path opens an output device, so it is a separate check
cmake --build build --target check-audio
# or select the suites in CTest
ctest --test-dir build -R test_playback_source --output-on-failure
ctest --test-dir build -R test_playback_audio --output-on-failure
# or one binary with GoogleTest's filters
./build/tests/test_playback_source --gtest_filter='MergePlayer.*'
./build/tests/test_playback_source --gtest_filter='AudioPlaybackTest.*'
./build/tests/test_playback_source --gtest_filter='Apple*'
```

`check` runs the suites that do not need an audio device. `check-audio` runs the two
`AudioPlaybackTest` cases: one drives the progressive source, the Darwin audio decoder and the Core
Audio sink through the whole five-second track; the other seeks inside it and verifies the audio
clock follows.

A plain `ctest` also lists the test cases the dependency trees register. Their executables are
`EXCLUDE_FROM_ALL` and are not built by this project, so CTest reports them as "Not Run"; the `check`
targets and the `-R` filters run only CoralPlayback's suites.

The GPU-backed cases (the effect graph and the layout pass) start a real device. On a host where the
Metal device cannot be created they skip with the engine's diagnostics instead of failing. The Apple
cases (`Apple*`) create a real `IOSurface`, bind it through `AppleVideoOutputNodeFactory`, render a
frame into it and play the fixture through it; they take the same skip when the device is missing.

## Examples

`examples/` is a separate project that links the framework through its public API. It is configured on
its own, or added by a top-level configure of this repository:

```sh
cmake -S examples -B build-demo -DCMAKE_BUILD_TYPE=Release
cmake --build build-demo
./build-demo/video_player_demo
```

The top-level configure builds the same target at `build/examples/video_player_demo`; the standalone
project above keeps it in `build-demo/`. The demo plays `assets/video/video720p_5s.mp4` from the
repository root by default; pass a file to play another one. It is macOS-specific because the window
is Cocoa:

```sh
./build-demo/video_player_demo                              # windowed, plays to the end
./build-demo/video_player_demo clip.mp4 --size 1280x720
./build-demo/video_player_demo clip.mp4 --frames 120        # stop after 120 presented frames
./build-demo/video_player_demo clip.mp4 --cpu               # CPU plane fallback
./build-demo/video_player_demo clip.mp4 --output frames --frames 30   # headless PNGs
./build-demo/video_player_demo clip.mp4 --no-audio          # video only, no output device
./build-demo/video_player_demo clip.mp4 --mute              # keep the device, at zero volume
```

The windowed demo plays the sound track by default; `--no-audio` builds no audio renderer, so it
also runs on a machine without an output device. It uses the zero-copy Apple sink unless `--cpu`
asks for the plane upload. `--output` is headless and leaves the audio off unless `--audio` is
passed. The window presents the output IOSurface through a layer-backed view and
updates it on every frame the effect graph commits; Esc/Q or closing the window ends it. With
`--output <dir>` the demo reads the same IOSurface back (after waiting for the GPU) and writes
`frame_00001.png` per sampled presentation position, without opening a window, so a build machine can
verify that decoding, composition and the IOSurface output produced pixels:

```sh
./build-demo/video_player_demo assets/video/video720p_5s.mp4 --output /tmp/coral-frames --frames 5
```

A project that consumes CoralPlayback as a submodule does not get the example executable: the root
`CMakeLists.txt` adds `examples/` only when it is the top-level project. See
[examples/README.md](examples/README.md) for what the demo verifies.

## License

Apache License 2.0; see [LICENSE](LICENSE).
