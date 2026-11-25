# CoralPlayback examples

The framework's standalone demo project. `video_player_demo` plays a video through the real playback
engine and presents the composed frames in a Cocoa window:

```
ProgressiveMediaSource -> DecodeVideoRenderer -> VideoFrameInputNode
    -> VideoLayoutPass -> SubtitleOverlayNode -> ReadbackOutputNode -> NSWindow
```

The player owns the media source, the decoder and the effect graph. The demo implements the two public
seams a host supplies:

- a `VideoOutputNodeFactory` that returns the output node the graph presents into;
- a `VideoSurface` that tells the player an output target exists.

Here both are CPU-backed: the output node is a `ReadbackOutputNode`, and the window copies each
completed frame into an `NSImage`. A production player on macOS would instead return a swap-chain or
Metal texture output and draw the final texture directly, avoiding the readback; the rest of the
pipeline is the same.

## What it verifies

| Check | What it proves |
| --- | --- |
| the player reaches `STATE_READY` and starts | the progressive source, the extractor and the decoder agree on the file |
| frames arrive at the output node | the effect graph, the layout pass and the readback are connected |
| the window shows a new frame each time | the frame's sequence advanced, so the window is not repeating the first image |
| `PLAYER_STATE_ENDED` / `--frames` reaches the exit | playback advanced through the media rather than stalling |
| `--output` writes non-empty PNGs | the frame contains pixels and the graphics encoder received them |

`--output <dir>` is the headless form: it does not open a window, waits for the requested number of
frames through `ReadbackOutputNode::waitForFrame`, and writes `frame_00001.png` per frame. A run that
produces no frame exits non-zero.

## Running

```sh
./video_player_demo [media] [options]
```

`media` defaults to `assets/video/video720p_5s.mp4` in the repository root, which is the directory
compiled in as `CORAL_ASSETS_DIR`; pass a file to play another one.

| Option | Meaning |
| --- | --- |
| `--size WxH` | render size of the effect graph; default `1280x720` |
| `--frames N` | stop after N displayed frames; default is to play to the end |
| `--output DIR` | write PNG frames into `DIR` and do not open a window |
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
./build-demo/video_player_demo assets/video/video720p_5s.mp4
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
