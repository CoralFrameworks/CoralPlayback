# CoralPlayback examples

The framework's standalone demo project. `video_player_demo` plays a video through the real playback
engine and presents the composed frames in the platform's window: Cocoa on macOS, Win32 on Windows.

The player owns the media source, the decoder and the effect graph. The demo supplies the public
pieces the host supplies; the graph between them is the same on every platform.

macOS:

```
ProgressiveMediaSource -> DecodeVideoRenderer -> AppleVideoFrameSink
    -> AppleIOSurfaceInputNode -> VideoLayoutPass -> SubtitleOverlayNode
    -> AppleIOSurfaceOutputNode -> IOSurface -> layer-backed NSView
```

Windows:

```
ProgressiveMediaSource -> DecodeVideoRenderer -> CpuVideoFrameSink -> VideoFrameInputNode
    -> VideoLayoutPass -> SubtitleOverlayNode -> WindowsSwapChainOutputNode -> HWND
```

`--zero-copy` replaces that input with the shared-texture path (experimental):
`WindowsVideoFrameSink (D3D11 video processor) -> WindowsD3D11InputNode`.

## macOS

The demo supplies the three public pieces a macOS/iOS host supplies:

- a `VideoSurface` over an output `IOSurface` (`AppleIOSurface`), which tells the player an output
  target exists and how large it is;
- a `VideoOutputNodeFactory` (`AppleVideoOutputNodeFactory`) that turns that surface into the graph's
  `AppleIOSurfaceOutputNode`;
- a `VideoEffectInput` aggregate from `AppleVideoInput::createZeroCopyInput`: the hardware decoder
  surface, the Apple `VideoFrameSink` and the `AppleIOSurfaceInputNode`.

Zero-copy is the default. `AppleHardwareVideoSurface` asks VideoToolbox for IOSurface-backed frames;
`AppleVideoFrameSink` asks the decoder for NV12 on Metal, so the decoder's own IOSurface goes straight
to `AppleIOSurfaceInputNode`, which imports both planes and converts them in one GPU pass. On
OpenGL/ES the sink asks for BGRA and the decoder does the YUV-to-RGB conversion. A decoder that
ignores the request falls back to a GPU NV12-to-BGRA conversion ring in the sink. The graph composes
the frame and `AppleIOSurfaceOutputNode` copies the final image into the output surface on the GPU.
The window is a layer-backed `NSView`: its `CALayer.contents` is the surface, so presenting a frame
costs one property assignment and no pixel copy.

`--cpu` selects the fallback instead: `CpuVideoFrameSink` converts the decoder's NV12 planes to
`VideoFrameDesc`, and `VideoFrameInputNode` queues and uploads them on the effect render thread. It
is the reference for a host whose decoder cannot produce a hardware surface.

## Windows

The demo supplies the same pieces, with the Win32 bridge:

- a `VideoSurface` over an output `HWND` (`CreateWin32WindowSurface`), which tells the player an
  output target exists and how large its client area is;
- a `VideoOutputNodeFactory` (`WindowsVideoOutputNodeFactory`) that turns that surface into the
  graph's `WindowsSwapChainOutputNode`. The node presents the composed frame into the window through
  the RHI's swapchain - the OpenGL backend uses the window's WGL context, the Vulkan backend uses
  `VK_KHR_win32_surface`;
- the video input aggregate. The default is `WindowsVideoInput::createCpuInput`: a `VideoFrameInputNode`
  and a `CpuVideoFrameSink`, the same upload input the macOS `--cpu` demo uses. Media Foundation still
  decodes in hardware when the machine has a hardware transform; without a `HardwareVideoSurface` it
  copies the picture into system memory.

`--zero-copy` selects the GPU path instead:
`WindowsVideoInput::createZeroCopyInput` returns a `WindowsD3D11InputNode`, a
`WindowsHardwareVideoSurface` over the demo's Direct3D 11 device (`WindowsVideoDevice`) and a
`WindowsVideoFrameSink`. The device is created with `D3D11_CREATE_DEVICE_VIDEO_SUPPORT` and
multi-thread protection, so Media Foundation's device manager lets the WMF decoder allocate its NV12
textures on it. `WindowsVideoFrameSink` converts each decoded NV12 texture to BGRA with a
`ID3D11VideoProcessor` - on the GPU, from a small ring of shared textures - and
`WindowsD3D11InputNode` imports the shared texture into the engine's Vulkan device. No decoded pixel
visits the CPU, and the converted frame carries its media timestamp through the graph.

The zero-copy path is experimental on Media Foundation: the transform can hand a frame out before the
driver's GPU write into its surface has completed, and it exposes no fence for that write. Roughly one
frame in ten can then arrive torn, which is why the CPU upload is the default. The demo also falls back
to the CPU input automatically when `--zero-copy` produces no frame at all (no hardware MFT, no video
processor), so it still plays on a machine without hardware decoding.
The zero-copy decoder is the D3D11VA one, which handles H.264 only: an HEVC stream is sent to the CPU
input directly, and a Dolby Vision file is reported as unsupported by the Windows decoder instead of
starting a pipeline that cannot present a frame. HDR (10-bit) sources use the CPU input as well. Both
inputs print a `video frames:` line, so a run can be told apart from a fallback without guessing.

The engine renders at the window's client area, so a resize changes the picture size rather than
scaling a fixed surface. The demo debounces `WM_SIZE` and then re-attaches the same window surface
(`setOutputSurface`), which is the player's late-surface path: the engine is resized to the new
client area and the swapchain rebuilt. Esc/Q or closing the window ends the windowed form.

`--output <dir>` does not open a window on either platform. macOS reads the output IOSurface back;
Windows installs a `ReadbackOutputNode` as the graph's output instead, so no swapchain or window is
needed. Both write `frame_00001.png` per sampled presentation position, which is what a build machine
checks.

## What it verifies

| Check | What it proves |
| --- | --- |
| the player reaches `STATE_READY` and starts | the progressive source, the extractor and the decoder agree on the file |
| the effect engine commits frames | the graph, the layout pass and the output node are connected |
| the window shows a new frame each time | the surface or swapchain was updated from a new effect frame, not the first one repeated |
| `PLAYER_STATE_ENDED` / `--frames` reaches the exit | playback advanced through the media rather than stalling |
| `--output` writes non-empty PNGs | the output holds the composed picture and the image encoder received it |
| the Windows sink's counters | the decoder's textures reached the graph (`converted`/`direct`) and the shared texture was imported (`zero-copy import: yes`) |
| `--subtitle` / `--subtitle-language` | the subtitle renderer and the graph's overlay burn the cues into the presented frame; the listener prints the selected text track and every cue change |

When the effect engine cannot start (no GPU device, a driver that refuses the surface), the demo
reports the engine's diagnostics and exits non-zero instead of waiting for frames that never come.

`--output <dir>` is the headless form: it does not open a window, waits for each sampled presentation
position, reads the frame back and writes `frame_00001.png`. A run that produces no frame exits
non-zero.

## Running

```
video_player_demo [media] [options]          # video_player_demo.exe on Windows
```

`media` defaults to `assets/video/video720p_5s.mp4` in the repository root, which is the directory
compiled in as `CORAL_ASSETS_DIR`; pass a file to play another one.

| Option | Meaning |
| --- | --- |
| `--size WxH` | size of the output surface on macOS, of the client area on Windows; default is the video's own size |
| `--frames N` | stop after N presented effect frames; default is to play to the end |
| `--seek S` | seek to S seconds before playback starts; the headless dump starts sampling just past it |
| `--loop` | repeat the media instead of ending at its duration |
| `--zero-copy` | sample the decoder's surface (the default on macOS; experimental on Windows, which uploads the planes by default) |
| `--subtitle FILE` | side-load an SRT/ASS/WebVTT file and burn it into the picture |
| `--subtitle-language L` | prefer the embedded text track with language tag `L` (a forced track whose language differs from the audio is not selected by default) |
| `--lottie FILE` | composite a Lottie animation (JSON) over the video |
| `--output DIR` | write PNGs into `DIR` and do not open a window |
| `--audio` | play the sound track (the default in a window; `--output` needs it explicitly) |
| `--no-audio` | build no audio renderer, so no output device is needed |
| `--mute` | set the player volume to zero |

The windowed demo plays the sound track through CoralAudio by default. `--no-audio` is the
video-only form for a machine without an output device. When the machine exposes no audio playback
endpoint at all, the demo warns and plays the video only; the fallback lives in the demo because the
player leaves audio device policy to CoralAudio. Esc/Q or closing the window ends the windowed form.

## Building

As part of the framework, a top-level configure builds it with the repository:

```sh
./scripts/bootstrap.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/examples/video_player_demo assets/video/video720p_5s.mp4
```

On Windows PowerShell:

```powershell
.\scripts\bootstrap.ps1
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\examples\video_player_demo.exe assets\video\video720p_5s.mp4
```

On its own, which is what an IDE opens when working on the demo:

```sh
cmake -S examples -B build-demo -DCMAKE_BUILD_TYPE=Release
cmake --build build-demo
./build-demo/video_player_demo assets/video/video720p_5s.mp4
```

The standalone configure resolves the dependency frameworks from `external/` and names the shared
`third_party/` tree, so the submodules have to be initialized first (see the header of the repository
README). The target is added on macOS and Windows; another platform supplies its own window and
output node.
