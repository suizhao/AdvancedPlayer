# AdvancedPlayer

A high-performance cross-platform multimedia player built with C++20, Qt6 Quick/QML, and FFmpeg 7.1. Supports hardware-accelerated GPU decoding, lock-free multi-threaded pipeline, and OpenGL rendering — capable of smooth 4K@60fps playback.

## Features

- **Hardware-Accelerated Decoding** — D3D11VA, DXVA2, NVDEC, CUDA, QSV, VAAPI, VideoToolbox; auto-detects the best available GPU backend with soft-decoding fallback
- **Lock-Free Pipeline** — 6-thread decoupled architecture (demux → decode → render) communicating via SPSC lock-free ring buffers with cache-line padding and acquire/release memory ordering
- **OpenGL 3.3 Video Rendering** — YUV→RGB conversion on GPU via fragment shader; PBO double-buffered async texture upload; zero-copy mapping for hardware frames
- **Audio-Clock-Master A/V Sync** — 4-tier adaptive strategy (micro-wait / delay / soft-skip / hard-skip) with ±50ms sync window; SoundTouch TSOLA for pitch-preserving speed control (0.25x–2.0x)
- **Network Streaming** — HTTP/HTTPS, HLS, RTMP, RTSP, RTP, UDP, MMS, DASH with buffering and auto-reconnect
- **Resume Playback** — remembers position per file, clears on natural completion
- **Screenshot Capture** — async frame grab to PNG/JPEG/BMP

## Supported Formats

| Category | Formats |
|----------|---------|
| Video Codecs | H.264, H.265/HEVC, VP8, VP9, AV1, MPEG-2, MPEG-4, VC-1, WMV |
| Audio Codecs | AAC, MP3, FLAC, Opus, Vorbis, AC3, DTS, WMA, PCM |
| Containers | MP4, MKV, AVI, MOV, FLV, WMV, WebM |
| Network Protocols | HTTP, HTTPS, RTMP, RTSP, RTP, UDP, MMS, HLS, DASH |
| Audio Only | MP3, FLAC, AAC, WAV, OGG |

## Screenshots

<!-- TODO: add screenshots (home page + player window) -->

## Architecture

```
[DemuxThread] ──→ [LockFreePacketQueue(V)] ──→ [VideoDecodeThread]
    │                                                    │
    │                                          [LockFreeVideoFrameQueue]
    │                                                    │
    └──→ [LockFreePacketQueue(A)] ──→ [AudioDecodeThread]
                                            │
                                   [LockFreeAudioFrameQueue]
                                            │
                    ┌───────────────────────┘
                    ▼
          [VideoRenderThread] ──→ [VideoOutput] (OpenGL FBO)
          [AudioRenderThread] ──→ [AudioOutput]  (SDL3 + SoundTouch)
```

- **PlaybackController** orchestrates all subsystems (demux, decode, render)
- **MediaPlayer** is the public QML-facing API with Q_PROPERTY bindings
- Inter-thread queues: `LockFreeRingBuffer<T>` (SPSC, power-of-2 capacity, `alignas(64)` head/tail separation, `std::atomic` with `memory_order_acquire/release`)
- Hardware frames mapped directly to OpenGL textures via platform-specific interop extensions; fallback to CPU download on failure

## Tech Stack

| Layer | Technology |
|-------|------------|
| Language | C++20 (Concepts, `std::jthread`, `std::stop_token`, `std::bit_ceil`) |
| UI | Qt 6.8+ Quick/QML (Qt Quick Controls Basic) |
| Demux / Decode | FFmpeg 7.1 (avformat, avcodec, avutil, swscale, swresample) |
| Audio Output | SDL3 |
| Speed Control | SoundTouch 2.3.3 (TSOLA algorithm) |
| Video Render | OpenGL 3.3 Core Profile via `QQuickFramebufferObject` |
| Build System | CMake 3.25+ |
| Platforms | Windows (MSVC/MinGW), Linux (X11), macOS (Cocoa) |

## Build

### Prerequisites

- **Qt 6.8+** with modules: Quick, Core, Qml, Gui, Svg, Network, Sql, Concurrent
- **CMake 3.25+**
- A C++20-capable compiler (MSVC 2022+, GCC 13+, Clang 16+)

### Dependencies

Third-party binaries and headers are **not** included in this repository. Acquire them from official sources and place under the following structure:

```
AdvancedPlayer/
├── bin/          # DLLs (Windows) / .so or .dylib (Linux/macOS)
├── lib/          # Import libraries (.dll.a, .lib)
└── include/      # Headers
```

| Library | Version | Required Files | Source |
|---------|---------|----------------|--------|
| **FFmpeg** | 7.x | `avcodec-61`, `avformat-61`, `avutil-59`, `swscale-8`, `swresample-5`, `avdevice-61`, `postproc-58` | [ffmpeg.org](https://ffmpeg.org/download.html) or [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) (Windows builds) |
| **SDL3** | 3.x | `SDL3.dll` / `libSDL3.so` + import lib + headers | [libsdl.org](https://github.com/libsdl-org/SDL/releases) |
| **SoundTouch** | 2.3+ | `SoundTouchDLL.dll` + import lib, or `libSoundTouch.so` | [soundtouch.org](https://www.surina.net/soundtouch/) |

**Quick setup on Windows (MSVC):**

1. Download FFmpeg shared build from gyan.dev → extract `bin/`, `lib/`, `include/` into the project root
2. Download SDL3 development package → extract into the project root
3. Build SoundTouch from source or download pre-built DLL → place in `bin/` and `lib/`

The CMakeLists.txt expects import libraries named `libavcodec.dll.a` etc. (MinGW-style). For MSVC, rename or symlink as needed, or adjust `CMakeLists.txt` accordingly.

### Build Steps

```bash
# Configure (Windows MSVC example)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release

# Or with Qt Creator
# Open CMakeLists.txt → configure with a Qt 6.8+ kit → Build
```

The build produces two targets:
- **appAdvancedPlayer** — the GUI application
- **endbackTest** — headless backend test harness

### Platform-Specific

| Platform | Extra Libraries |
|----------|----------------|
| Windows | `user32`, `d3d11`, `dxgi`, `opengl32` (auto-linked) |
| Linux | X11 (auto-detected via `find_package`) |
| macOS | Cocoa framework (auto-linked) |

## Usage

### Playback Controls

| Action | Shortcut / Method |
|--------|-------------------|
| Play / Pause | Click play button or `Space` |
| Stop | Click stop button |
| Volume | Slider or mouse wheel |
| Mute | Click volume icon |
| Speed (0.25x–2.0x) | SpeedControl panel |
| Seek | Drag progress bar |
| Previous / Next | Click prev/next buttons |
| Fullscreen | Click fullscreen button |
| Screenshot | Click screenshot button |

### Opening Media

- **Local files**: Drag & drop into the playlist, or use the file picker
- **Network streams**: Enter URL directly (supports RTMP, RTSP, HLS, HTTP, etc.)

### Settings

Accessible via the gear icon: hardware acceleration toggle, resume playback position, screenshot format/directory, auto-play-next, theme.

## Key Design Decisions

1. **Controller retirement pattern** — old PlaybackControllers are asynchronously reclaimed on a timer to avoid TLS crashes on MinGW/winpthread thread teardown, and to prevent dual-controller frame flickering
2. **SoundTouch output-side PTS tracking** — clock position is derived from SoundTouch output sample count rather than input PTS, eliminating the clock overestimation caused by TSOLA's internal latency
3. **HEVC hardware decode** — explicit HEVC codec path for D3D11VA hardware initialization, with automatic fallback detection
4. **Busy-loop avoidance** — DemuxThread uses `std::condition_variable` for pause/resume; no spin-waiting in the hot path

## License

[GNU General Public License v3.0](LICENSE)

This project is licensed under GPLv3. Note that FFmpeg and Qt are dual-licensed (LGPL / commercial); linking against their GPL-compatible builds is required.

---

Built with Qt, FFmpeg, SDL, and SoundTouch — each governed by their respective licenses.
