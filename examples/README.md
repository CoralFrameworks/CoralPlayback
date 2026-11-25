# CoralPlayback examples

The framework's standalone demo project. `video_player_demo` plays a video through the real playback
engine and presents the composed frames in a Cocoa window:

```
ProgressiveMediaSource -> DecodeVideoRenderer -> VideoFrameInputNode
    -> VideoLayoutPass -> SubtitleOverlayNode -> AppleIOSurfaceOutputNode
    -> IOSurface -> layer-backed NSView
```

The player owns the media source, the decoder and the effect graph. The demo supplies the three
public pieces a macOS/iOS host supplies:

- a `VideoSurface` over an output `IOSurface` (`AppleIOSurface`), which tells the player an output
  target exists and how large it is;
- a `VideoOutputNodeFactory` (`AppleVideoOutputNodeFactory`) that turns that surface into the graph's
  `AppleIOSurfaceOutputNode`;
- a `VideoEffectInput` aggregate containing the input node (`AppleVideoInput::createFrameInput`)
  for the decoded frames. The demo leaves its sink empty, so the player installs the CPU sink.

The decoded frame enters through the CPU sink: CoralMedia's VideoToolbox decoder converts its NV12
`CVPixelBuffer` into I420 planes, `CpuVideoFrameSink` converts them to `VideoFrameDesc`, and
`VideoFrameInputNode` copies them into its queue and uploads them on the effect render thread. The
graph composes the frame and `AppleIOSurfaceOutputNode` copies the final image into the output
surface on the GPU. The window is a layer-backed `NSView`: its `CALayer.contents` is the surface, so
presenting a frame costs one property assignment and no pixel copy. A production host can hand the
same surface to a Metal compositor or a video encoder instead.

The zero-copy node (`AppleVideoInput::createIOSurfaceInput` over `AppleIOSurfaceInputNode`) is part of
the library. Wiring it needs an Apple `VideoFrameSink` and a decoder layout it can import; that work
stays in the Apple platform layer and does not change `DecodeVideoRenderer`.

## What it verifies

| Check | What it proves |
| --- | --- |
| the player reaches `STATE_READY` and starts | the progressive source, the extractor and the decoder agree on the file |
| the effect engine commits frames | the graph, the layout pass and the IOSurface output are connected |
| the window shows a new frame each time | the layer's contents was updated from a new effect frame, not the first one repeated |
| `PLAYER_STATE_ENDED` / `--frames` reaches the exit | playback advanced through the media rather than stalling |
| `--output` writes non-empty PNGs | the IOSurface holds the composed picture and the graphics encoder received it |

`--output <dir>` is the headless form: it does not open a window, waits for each sampled presentation
position, calls `EffectEngine::flushAndWait()`, reads the output IOSurface back and writes
`frame_00001.png`. A run that produces no frame exits non-zero.

## Running

```sh
./video_player_demo [media] [options]
```

`media` defaults to `assets/video/video720p_5s.mp4` in the repository root, which is the directory
compiled in as `CORAL_ASSETS_DIR`; pass a file to play another one.

| Option | Meaning |
| --- | --- |
| `--size WxH` | size of the output IOSurface; default is the video's own size |
| `--frames N` | stop after N presented effect frames; default is to play to the end |
| `--output DIR` | write PNGs read from the output IOSurface into `DIR` and do not open a window |
| `--audio` | play the sound track (the default in a window; `--output` needs it explicitly) |
| `--no-audio` | build no audio renderer, so no output device is needed |
| `--mute` | set the player volume to zero |

The windowed demo plays the sound track through CoralAudio by default. `--no-audio` is the
video-only form for a machine without an output device. Esc/Q or closing the window ends the windowed
form.

## Building

As part of the framework, a top-level configure builds it with the repository:

```sh
./scripts/bootstrap.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/examples/video_player_demo assets/video/video720p_5s.mp4
```

On its own, which is what an IDE opens when working on the demo:

```sh
cmake -S examples -B build-demo -DCMAKE_BUILD_TYPE=Release
cmake --build build-demo
./build-demo/video_player_demo assets/video/video720p_5s.mp4
```

The standalone configure resolves the dependency frameworks from `external/` and names the shared
`third_party/` tree, so the submodules have to be initialized first (see the header of the repository
README). The target is only added on macOS; another platform supplies its own window output node.
