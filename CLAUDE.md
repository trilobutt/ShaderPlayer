# ShaderPlayer

## Project Overview

Real-time HLSL shader video player for Windows 11: D3D11 pixel shader pipeline applied to FFmpeg-decoded video, with live editor, hot-swappable presets, and uninterrupted recording.

## Architecture

### Technology Stack

- **Language**: C++20
- **Build System**: CMake 3.21+ (driven by `CMakePresets.json`)
- **Graphics API**: Direct3D 11 (HLSL shader model 5.0)
- **Video Decoding**: FFmpeg (libavcodec, libavformat, libavutil, libswscale)
- **Video Encoding**: FFmpeg (H.264/H.265 output)
- **UI Framework**: Qt 6 Widgets (6.9.1 msvc2022_64, LGPL, dynamically linked, deployed
  with `windeployqt`)
- **Code Editor**: `QPlainTextEdit` driven by KSyntaxHighlighting (KDE Frameworks Tier 1,
  LGPLv2+), with an HLSL definition authored in `resources/syntax/hlsl.xml`
- **JSON**: nlohmann/json

### Component Structure

```
src/
├── main.cpp              - WinMain: DPI awareness, CoInitializeEx (COM is still required
│                           for the DirectShow device enumeration), the crash filter
│                           (WriteCrashLog → crash.log via DbgHelp), then QApplication,
│                           Theme::Apply, Application::InitializeQt, and a QTimer driving
│                           TickOnce. Timer interval follows what the last tick did: 0
│                           while the viewport presented (its vsync Present paces the
│                           loop), 16 ms when it did not (the empty state, where nothing
│                           else would pace it and 0 would spin a core).
├── Common.h              - Shared types: VideoFrame, ShaderPreset, RecordingSettings,
│                           AppConfig (shaderDirectory default = "shaders"), PlaybackState,
│                           Keyframe, KeyframeTimeline, BezierHandles, InterpolationMode
├── KeyframeTimeline.cpp  - KeyframeTimeline method implementations: Evaluate() (cubic
│                           bezier, smoothstep, linear interpolation with binary search),
│                           AddKeyframe() (sorted insert, overwrites duplicates),
│                           RemoveKeyframe() (bounds-checked erase).
├── Application.{cpp,h}   - Central coordinator. Owns all other components including
│                           MainWindow. InitializeQt() builds everything (a QApplication
│                           must already exist); TickOnce() runs ProcessFrame() (video
│                           decode) + RenderFrame() (D3D + window) and returns whether
│                           the viewport presented. Owns HandleKeyboardShortcuts (in VK
│                           codes) and the QFileDialog wrappers. No HWND, no WndProc:
│                           windowing belongs to Qt.
├── AudioAnalyzer.{cpp,h} - Pure DSP class. Owns KissFFT plan, ring buffer (2048
│                           samples), Hann window, and beat history. Fed by
│                           VideoDecoder::DrainAudioSamples() in Application::
│                           ProcessFrame(). Outputs AudioData (rms/bass/mid/high/
│                           beat/spectralCentroid + 256-bin spectrum). No threads.
│                           Reset() on seek/close/EOF loop.
├── AudioPlayer.{cpp,h}   - miniaudio WASAPI playback. SPSC ring buffer (524288 mono f32
│                           ≈10.9s at 48kHz); miniaudio callback drains independently.
│                           Submit() called from ProcessFrame; SWR resamples if source
│                           rate differs. Flush() on seek/pause/stop/close/EOF/open.
│                           MINIAUDIO_IMPLEMENTATION defined in AudioPlayer.cpp only;
│                           miniaudio.h included BEFORE Common.h (WASAPI COM ordering).
├── VideoDecoder.{cpp,h}  - FFmpeg wrapper: Open/Close, DecodeNextFrame() → VideoFrame
│                           (RGBA8 in data[0]), SeekToTime(). ReadAudioAhead(n) decodes
│                           audio eagerly and queues encountered video packets in
│                           m_videoPktQueue (av_packet_clone); DecodeNextFrame drains
│                           that queue before calling av_read_frame.
├── VideoEncoder.{cpp,h}  - FFmpeg recording: StartRecording/StopRecording, SubmitFrame()
│                           from RenderFrame() after CopyRenderTargetToStaging().
├── D3D11Renderer.{cpp,h} - D3D11 device and fullscreen-triangle pipeline. Owns no window
│                           and no swap chain: every surface it draws to is either an
│                           offscreen texture or an RTV handed in by a caller that owns
│                           its own swap chain (ViewportWidget, VideoOutputWindow).
│                           Initialize(width, height) creates the device only.
│                           BeginFrame() sets the entire pixel-shader pipeline state
│                           (PSSetShader with m_activePS, PSSetShaderResources,
│                           PSSetSamplers, PSSetConstantBuffers) and binds no RTV — MUST
│                           be called before RenderToDisplay(). RenderToDisplay() draws to
│                           m_displayTexture and leaves m_displayRTV bound; every consumer
│                           binds its own RTV before binding m_displaySRV, so the
│                           read-while-bound hazard cannot arise.
│                           Resize(w, h) tracks the viewport's client size for the
│                           `resolution` cbuffer field and the post-blit viewport restore;
│                           it deliberately does NOT recreate m_displayTexture, which is
│                           sized per frame from content resolution instead.
│                           SetActivePixelShader() stores the pointer in m_activePS; GPU
│                           state is updated on next BeginFrame.
├── ShaderManager.{cpp,h} - Owns two parallel vectors that MUST stay in sync:
│                           m_presets (ShaderPreset metadata) and m_compiledShaders
│                           (ComPtr<ID3D11PixelShader>). m_activeIndex = -1 means
│                           passthrough. Key methods:
│                           • RecompilePreset(int index) — preferred compile path for
│                             presets already in the vector; directly updates
│                             m_compiledShaders[index] by index (no pointer search).
│                           • CompilePreset(ShaderPreset&) — used for local/temporary
│                             presets during load; finds index by pointer comparison
│                             (only works if preset is already in m_presets).
│                           • AddPreset() — push_back to both vectors (always in sync),
│                             compiles source if isValid or source non-empty.
│                           • ScanDirectory() — scans for .hlsl/.fx/.ps, skips
│                             already-loaded paths.
│                           • SetActivePreset(index) — calls
│                             D3D11Renderer::SetActivePixelShader with
│                             m_compiledShaders[index].Get(); null → passthrough.
├── ui/                   - The Qt shell. See "Qt UI Structure" below.
├── ConfigManager.{cpp,h} - Load/Save config.json next to the executable
│                           (GetDefaultConfigPath uses GetModuleFileNameA). Serialises
│                           AppConfig including shaderPresets (filepath + shortcutKey)
│                           and shaderDirectory.
├── VideoOutputWindow.{cpp,h} - Second Win32 HWND + IDXGISwapChain on the same D3D11
│                               device. BlitAndPresent() copies m_displayTexture to the
│                               window each frame via D3D11Renderer::BlitDisplayTo().
│                               Opened/closed via View → Video Output Window (F7).
│                               Queries IDXGIFactory2 from the existing device — no
│                               cross-adapter copy. WndProc handles WM_SIZE (ResizeBuffers)
│                               and WM_CLOSE (sets m_hwnd = nullptr, no PostQuitMessage).
└── WorkspaceManager.{cpp,h} - Workspace layout presets. Scans `layouts/` dir next to
                              exe for `.ini` files (a [WorkspacePreset] header whose
                              `state=` key holds a base64 QMainWindow::saveState() blob).
                              Index 0 = built-in Default (kDefaultLayoutState constant —
                              Parameters left, Library over Editor right, Viewport centre,
                              Transport and Recording tabbed along the bottom; its
                              PanelVisibility is set in the ctor and must match the docks
                              in the blob). Owned by Application.
```

### Qt UI Structure

```
src/ui/
├── Theme.{h,cpp}         - The single owner of colour, type and motion for the C++ side,
│                           mirroring resources/shaderplayer.qss. Apply() sets Fusion, the
│                           QPalette, the application font, and the stylesheet, in that
│                           order. Motion(ms) returns 0 under AppConfig::reducedMotion.
├── MainWindow.{h,cpp}    - QMainWindow shell: menu bar, eight RegionDocks, and a
│                           QStackedWidget central area (index 0 ViewportWidget, index 1
│                           the empty state). Owns the refresh contract (below) and routes
│                           keyPressEvent into Application::HandleKeyboardShortcuts.
│                           RegionDock carries one region's hue on four channels: a tinted
│                           icon, the top hairline, the title colour, and a hover glow.
├── ViewportWidget.{h,cpp}- The one surface Qt must not paint: WA_PaintOnScreen +
│                           WA_NativeWindow, paintEngine() returns nullptr, and its own
│                           IDXGISwapChain1 from winId(). RenderAndPresent() letterboxes
│                           via BlitDisplayToRect and returns whether it presented.
├── EditorPanel, LibraryPanel, ParamsPanel, KeyframeDetail, BezierEditor,
│   TransportPanel, RecordingPanel, NoisePanel, SpoutPanel, AudioPanel
│                         - One class per dock, each taking SP::Application& and calling
│                           into it directly. No observer layer.
├── HlslHighlighter.{h,cpp} - KSyntaxHighlighting driving the editor document.
├── Toast.{h,cpp}         - Transient notices. Frameless Qt::Tool top-levels, not child
│                           widgets: the viewport is a native HWND that Windows composites
│                           over anything Qt paints into the parent, and raise() cannot
│                           reorder an alien widget against it.
├── KeyMap.h              - The one Qt-key → Win32 VK mapping, shared by MainWindow and
│                           KeybindingDialog. Refuses Space, Escape and the keypad.
└── dialogs/              - Dialog (shared elevation shell), KeybindingDialog,
                            KeybindingsReferenceDialog, NewShaderDialog, CaptureDialog,
                            WorkspacesDialog. Each is constructed per showing and destroyed
                            on return, so no modal state can leak between openings.
```

**The refresh contract.** This is the whole difference from the outgoing immediate-mode UI:

- `MainWindow::Tick()` runs every frame and touches only the viewport and the live meters
  (transport clock, audio bars, keyframe-driven parameter values, Spout status). A panel
  that rebuilt itself here would fight the user's cursor and burn CPU on an idle window.
- `RefreshLibrary()` when the preset list or the active preset changes.
- `RefreshParameters()` when the active preset's parameters change.
- `RefreshAll()` both, plus the editor document.

Every `SetActivePreset` call site owes `RefreshParameters()` and `Application::OnParamChanged()`
(see "ShaderManager API"); a site that also adds or renames a preset owes `RefreshLibrary()`.

### Shader System

All shaders use this cbuffer layout (must match `D3D11Renderer::ShaderConstants`):

```hlsl
Texture2D videoTexture : register(t0);
SamplerState videoSampler : register(s0);

cbuffer Constants : register(b0) {
    float time;
    float padding1;
    float2 resolution;      // output resolution
    float2 videoResolution; // source video resolution
    float2 padding2;
    float4 custom[8];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET { ... }
```

`default_shaders/` is the shipped shader set and the authoritative inventory — each file's ISF block carries its own description and `SHADER_TYPE`. Many generative shaders also declare AudioBand inputs plus an `Audio Amount` scale; setting that to 0 gives the unmodulated pattern.

### Output Alpha

The compositor multiplies the video blend by the shader's output alpha (`blendAmount * g.a`), so a shader can write `alpha < 1` to let the video through in those pixels under any blend mode. Shaders writing `alpha = 1` are unaffected. Alpha only has a visible effect when a Video Blend mode other than Off is selected — with blending off, the shader draws straight to the display texture.

Colour params expose an alpha channel in `ColorEdit4`, so `SomeColour.a` is the idiomatic per-element opacity control (used by `psychoacoustic_topography`); a standalone `float` is used when an element has no colour of its own (`audio_spectrum`'s `BgOpacity`/`BarOpacity`).

### Audio Data (b1 / t3)

`D3D11Renderer::BeginFrame()` always binds `AudioConstants` cbuffer at `b1` and a 1×256 `R32_FLOAT` DYNAMIC spectrum texture at `t3`. Updated each frame via `SetAudioData(const AudioData*)` (pass nullptr when no audio → zeros). Audio shaders do **not** declare these manually — preamble injection handles it automatically.

`AudioConstants` layout (must match `D3D11Renderer::AudioConstants`):
```hlsl
cbuffer AudioConstants : register(b1) {
    float audioRms; float audioBass; float audioMid; float audioHigh;
    float audioBeat; float audioSpectralCentroid; float _audioPad[2];
};
Texture2D spectrumTexture : register(t3);  // 1×256, sample at float2(x, 0.5)
```

### Global Noise Texture (t1 / s1)

`D3D11Renderer::BeginFrame()` always binds a CPU-generated noise texture at `t1` (WRAP sampler at `s1`). **R = Perlin gradient noise. G = Voronoi F1 (inverted — bright at cell centres).** All shaders must declare both even if unused:

```hlsl
Texture2D noiseTexture : register(t1);
SamplerState noiseSampler : register(s1);   // WRAP addressing
```

- `D3D11Renderer::UpdateNoiseTexture(scale, texSize)` — regenerates (`D3D11_USAGE_IMMUTABLE`, fully recreated each call). Called at startup and via `Application::RegenerateNoise()`.
- UI: View → Noise Generator (`src/ui/NoisePanel`). The panel's preview is a genuine GPU
  readback of the bound texture (`GetNoiseSRV()` → `CopyResource` into a STAGING clone →
  map), not a CPU re-simulation, so it cannot drift from what the shaders sample.
- Config: `AppConfig::noise` (`NoiseSettings { float scale; int textureSize; }`), persisted as `noiseScale`/`noiseTextureSize` in `config.json`.
- Noise UV pattern for per-cell variation: `cellCoord / 64.0 + cellUv * (freq / 64.0)` — unique slice per cell, `freq` scales zoom.

### Sampling and Screen-Space Derivatives

Every texture the pipeline creates (video, noise, spectrum, display, compositor source, render target) is `MipLevels = 1`, and both samplers are `D3D11_FILTER_MIN_MAG_MIP_LINEAR` with no anisotropy. Consequences worth knowing before writing a comment about filtering:

- `Sample`, `SampleLevel(..., 0)` and `SampleGrad` return **identical values**. Nothing consumes a derivative, so no fetch can "pick the wrong mip" and no fetch gets minification filtering for free. A shader that magnifies its source aliases unless it band-limits explicitly.
- Use `SampleLevel(..., 0)` for any fetch on a coordinate that is loop-carried, `frac()`-wrapped, folded, or downstream of a varying branch. The reason is that an implicit-LOD fetch in varying flow control is undefined and fxc rejects some forms of it (X3595), not mip selection.
- `fwidth`/`ddx` must be taken on the **continuous** coordinate, before any `frac()`, fold or wrap, and outside divergent flow. Past a discontinuity it reports an infinitely wide pixel and smears a grey seam along the boundary. Where no continuous coordinate exists, supply the footprint analytically (see `pxCell` in `game_of_life.hlsl`, `maskW` in `crt_simulation.hlsl`, the angular footprint in `kaleidoscope.hlsl`).

## Build Instructions

### Prerequisites

- Windows 11 (Windows 10 may work but untested)
- Visual Studio 2022 or later (Build Tools are sufficient; MSVC only, since the build
  depends on Qt's msvc2022_64 package)
- Qt 6.9 or later, msvc2022_64, at a path given to CMake as `CMAKE_PREFIX_PATH`
- CMake 3.21 or later (`CMakePresets.json` is version 3)
- FFmpeg development libraries (headers + libs)

extra-cmake-modules and KSyntaxHighlighting are fetched and built by CMake; nothing to
install. ECM is configured and installed into `build/ecm-install` at configure time (it is
pure `.cmake` files, so nothing compiles) because KSyntaxHighlighting does
`find_package(ECM NO_MODULE)` and then overwrites `CMAKE_MODULE_PATH` with
`ECM_MODULE_PATH` — a populated source tree on `CMAKE_MODULE_PATH` cannot satisfy it.

### FFmpeg Setup

FFmpeg headers and import libs are bundled in `third_party/ffmpeg/` and committed to the repo (~3 MB). Only the runtime DLLs (~220 MB) must be provided locally — they are gitignored.

On a fresh clone:
1. Download a shared FFmpeg build from https://www.gyan.dev/ffmpeg/builds/ (`ffmpeg-release-full-shared.7z`)
2. Copy the DLLs into `third_party/ffmpeg/bin/`:
   ```
   xcopy "C:\path\to\ffmpeg\bin\*.dll" "third_party\ffmpeg\bin\"
   ```
3. CMake autodiscovers `third_party/ffmpeg/` — no flags needed

The copy runs at build time and fails the build with a named error if the directory holds
no DLLs, so an empty `third_party/ffmpeg/bin/` can no longer yield an executable that links
but dies at load with `avcodec-62.dll was not found`. Adding the DLLs needs a rebuild, not
a re-configure.

If you prefer a system-level FFmpeg install instead, pass `-DFFMPEG_ROOT=<path>` to CMake and leave `third_party/ffmpeg/` unpopulated.

### Building

`CMakePresets.json` is the source of truth for configuration. `windows-msvc` (Ninja,
RelWithDebInfo, `build/`) is the only supported configuration; single-config, so there is
no `--config` flag and the output is `build/ShaderPlayer.exe`.

**Never ship or hand over a Debug build.** Debug links `ucrtbased.dll`, `msvcp140d.dll` and
friends, which `windeployqt` does not deploy and which exist only on machines with the
Windows SDK installed. `windows-msvc-debug` therefore targets a separate `build-debug/` so
the two cannot clobber each other; use it to step through code, not to give to anyone.

Configure, then build:

```
cmake --preset windows-msvc
cmake --build build
```

Command-line builds work only from a shell where the MSVC environment has been initialised
— otherwise the link fails with `memcpy` unresolved. From a plain shell:

```
cmd /c "set ""PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;C:\Qt\6.9.1\msvc2022_64\bin;%PATH%"" & call ""<vcvars64.bat>"" >nul & cmake --build build"
```

Four things in that line are load-bearing:

- **The PATH additions must come BEFORE `call vcvars64.bat`, not after.** `cmd` expands
  `%PATH%` when it *parses* the line, so `set PATH=...;%PATH%` written after the call
  expands to the pre-vcvars value and silently discards everything vcvars added. A build
  that survives this anyway is living on absolute tool paths cached by CMake; the next
  clean configure fails with `LNK1158: cannot run 'rc.exe'`.
- **The Visual Studio `Installer` directory must be on PATH**, because `vcvars64.bat`
  invokes `vswhere.exe` by bare name. Without it vcvars prints `'vswhere.exe' is not
  recognized`, then reports "Environment initialized" having set neither `INCLUDE`, `LIB`
  nor the tool paths — which surfaces as `fatal error C1083: Cannot open include file:
  'type_traits'` from every target.
- **Qt's `bin` must be on PATH**: KSyntaxHighlighting builds `katehighlightingindexer.exe`
  and runs it during the build, which fails with `0xc0000135` without it.
- Adjust the vcvars path to the installed edition (`vswhere.exe -latest -property
  installationPath` locates it). It may be a **BuildTools** install under
  `Program Files (x86)`, not Community.

Building from the Visual Studio IDE works without the extra step.

Post-build steps copy the FFmpeg DLLs (`tools/copy_ffmpeg.cmake`), the
KSyntaxHighlighting DLL, and the Qt runtime (`tools/deploy_qt.cmake` wrapping
`windeployqt`).

**Run from the project root** (not from `build/`) so the relative `shaders/` path resolves correctly, or use the Shader Library → "Scan Folder" button to point at the shaders directory manually. A fallback also looks for `shaders/` next to the executable at startup.

## Configuration

Configuration is stored in `config.json` next to the executable (created on first run if missing).

### Keybindings

Shader shortcut keys are stored per-preset as virtual key codes (`shortcutKey`, `shortcutModifiers`). Set them via right-click → "Set Keybinding..." in the Shader Library.

### Shader Presets

Presets saved to config include only the `filepath` and shortcut — source is re-read from disk on load. shaderDirectory is also saved so "Scan Folder" persists across sessions.

### Workspace Presets

Layout presets stored as `.ini` files in `layouts/` next to the executable (path in `AppConfig::layoutsDirectory`). Not referenced in `config.json` — discovered by `WorkspaceManager::ScanDirectory()` at startup. Keybindings are in the `.ini` file headers, not config.json. Access via View > Workspace Presets.

The layout itself is a base64 `QMainWindow::saveState()` blob under the header's `state=`
key. `ParsePresetFile` splits on the **first** `=` only, so base64 padding round-trips; a
file with no `state=` key is skipped by `ScanDirectory` rather than crashing it.

`saveState`/`restoreState` are passed `kWorkspaceStateVersion` (`MainWindow.cpp`). **Bump
it whenever the dock set or any dock object name changes**, so a stale layout is refused
rather than half-applied. `restoreState` is atomic: on a version mismatch or a corrupt blob
it leaves the layout untouched and returns `false`, and every caller falls back to
`MainWindow::ArrangeDefaultLayout()`.

Dock object names (`dockLibrary`, `dockEditor`, `dockParams`, `dockTransport`,
`dockRecording`, `dockNoise`, `dockSpout`, `dockAudio`) are load-bearing: `saveState` keys
off them.

A preset must record the visibility of **every** closable dock, carried as `PanelVisibility`
(`Common.h`) in `WorkspacePreset::panels` and moved through `MainWindow::GetVisibility()`/
`ApplyVisibility()`. `restoreState` restores a dock's geometry but not the decision to have
it hidden, so a preset that omits one reopens that panel wherever the blob last placed it.
Adding a closable dock therefore needs a `PanelVisibility` field, a `show*` key in
`WorkspaceManager::ParsePresetFile`/`WritePresetFile`, a line in `GetVisibility`/
`ApplyVisibility`, and a matching flag on the built-in Default in the `WorkspaceManager`
ctor. `Video` and `Shader Parameters` are always submitted and are deliberately excluded.

**First run** is distinguished by `AppConfig::windowState` being empty, since only
`MainWindow::closeEvent` ever writes it. Empty → apply the factory layout via
`LoadPreset(0, ...)` + `ApplyVisibility`; non-empty → restore the user's own geometry and
state. The returning-user path deliberately does not call `ApplyVisibility`: `saveState`
already records each dock's visibility.

## Development Notes

### Render Loop (RenderFrame)

`RenderFrame()` returns whether the viewport presented; `TickOnce()` passes that up to
WinMain's timer, which is where the loop's pacing comes from. Each frame:

1. `UploadVideoFrame()` — maps video texture and DMA-copies current VideoFrame (RGBA8)
2. `BeginFrame()` — updates cbuffer, sets the **entire** PS pipeline state including
   `m_activePS`. Binds no render target: every draw path below binds its own.
3. `RenderToDisplay()` — binds `m_displayRTV`, calls `Draw(3,0)`, and leaves it bound
4. `VideoOutputWindow::BlitAndPresent()` (if open) — `BlitDisplayTo()` then presents the
   second swap chain
5. `SpoutOutput::SendFrame(GetDisplayTexture())`
6. Recording capture, gated on `m_newVideoFrame`, then a restoring `BeginFrame()`
7. `MainWindow::Tick()` — the viewport blits and presents on its own swap chain (the vsync
   block that paces the loop), and the live meters move

`BlitDisplayTo(rtv, w, h)` — draws `m_displaySRV` via passthrough PS into the given RTV,
then restores the main viewport, `m_activePS`, and `m_videoSRV` as t0. **The caller's RTV
is left bound**: the renderer owns no backbuffer to restore. Safe to call between
`RenderToDisplay()` and recording capture.

`BlitDisplayToRect(rtv, x, y, w, h, clearColor)` — the same, but clears the whole RTV to
`clearColor` and draws into a sub-rectangle. `ClearRenderTargetView` ignores the viewport,
which is what lets the letterbox borders show the clear colour. Used by `ViewportWidget`.

### Shader Compile Path

- **Editor compile (F5)**: `Application::CompileCurrentShader(source)` → updates `preset->source`, calls `ShaderManager::RecompilePreset(activeIndex)` (index-based, reliable), then `SetActivePreset(activeIndex)` to push new `m_activePS` to the renderer. Effect appears on the next `BeginFrame`.
- **Initial load / scan**: `LoadShaderFromFile` → `CompilePreset(localPreset)` (pointer search finds nothing, compiled shader discarded) → `AddPreset(preset)` recompiles and stores. This double-compile is intentional to keep `AddPreset` as the single point of vector-sync.
- **No active preset + compile**: `AddPreset` creates the preset, `SetActivePreset` applies it.

### Parallel Vectors in ShaderManager

`m_presets` and `m_compiledShaders` are always the same length. Every `AddPreset` does both `push_back`s; every `RemovePreset` does both `erase`s. Never modify one without the other.

Removing the **active** preset must go through `SetPassthrough()`, not a bare `m_activeIndex = -1`. The renderer holds a raw `ID3D11PixelShader*` handed to it by `SetActivePreset`, and the erase releases the only `ComPtr` keeping it alive — clearing the index alone leaves the renderer drawing with a freed shader until the next activation.

### COM Requirement

File pickers are `QFileDialog` and need no COM apartment of their own, but the DirectShow
device enumeration behind `CaptureDialog` does. `CoInitializeEx` / `CoUninitialize` stay in
`WinMain` for it.

### shaderDirectory Path Resolution

`AppConfig::shaderDirectory` defaults to `"shaders"` (CWD-relative). At startup, if that path doesn't exist, `Application::InitializeQt` falls back to `<exe_dir>/shaders`. `ScanFolderDialog` writes the chosen path into the config and calls `SaveConfig()` immediately, so it persists across an abnormal exit as well as a clean one.

### Adding a New Shader

Use the `/new-shader <name>` skill — it scaffolds the file with correct cbuffer layout and ISF block. Then Shader Library → "Scan Folder" to load it.

### Shared HLSL Helper Library

`src/ShaderCommon.hlsli` is injected ahead of every shader by `ShaderManager::BuildDefinesPreamble`, before the AudioConstants block and the param `#define`s. Every symbol is `sp`-prefixed (`SP_` for macros) so it cannot collide with per-shader helpers. Contents: tonemaps (`spTonemapACES`/`Tanh`/`Unreal`), sRGB conversion, `spPalette` (IQ cosine), hashes (`spHash12`/`22`/`33`, `spIGN`), `spDither`, `spAAStep`/`spAALine`/`spBandLimitedCos`, `spHsv2rgb`/`spRgb2hsv`, `spLuma`, `spVignette`, `spAspectUV`. Read the file for signatures and usage notes.

The `.hlsli` is the single source of truth. CMake runs `tools/embed_hlsli.cmake` to generate `build/generated/ShaderCommonEmbedded.h` (a raw string literal, `kShaderCommonHLSL`) which `ShaderManager.cpp` includes, and `tools/validate_shaders.py` reads the `.hlsli` directly. Nothing is hand-mirrored and the exe has no runtime file dependency. The generated header carries CRLF where the source has LF; only line endings differ, so line counts still match.

fxc dead-strips unused functions — a shader that calls none of the helpers compiles to byte-identical bytecode.

The preamble ends with `#line 1 "<preset name>"` so fxc error line numbers refer to the shader file on disk, not to preamble-inflated positions.

### Validating Shaders

```
python tools/validate_shaders.py                 # all of default_shaders/
python tools/validate_shaders.py <file.hlsl>     # one file
python tools/validate_shaders.py --dump <file>   # print the combined preamble+source
```

Reproduces the injected preamble exactly, compiles with fxc at `/O3` (matching `D3DCOMPILE_OPTIMIZATION_LEVEL3`), reports the packed `custom[]` float count per shader, and fails on budget overflow as well as on compile errors. Non-zero exit on any failure. Running fxc on a raw shader file instead is worthless — every ISF param reads as an undeclared identifier.

## Live Capture (Webcam / RTSP)

- **libavdevice must be linked and registered.** `avdevice` is in `FFMPEG_LIBRARIES` (CMakeLists) and `VideoDecoder.cpp` calls `avdevice_register_all()` once via `EnsureDevicesRegistered()`. avformat's static init does not register device demuxers — without this `av_find_input_format("dshow")` returns null and every webcam open fails silently before touching the device.
- `VideoDecoder::OpenCapture(deviceOrUrl, isDshow)` — opens a dshow device (`"video=<name>"`) or any URL (RTSP/RTMP/HTTP). Sets `AVFMT_FLAG_NONBLOCK`; `DecodeNextFrame` returns false on `AVERROR(EAGAIN)` (no frame ready, not an error).
- DirectShow device enumeration: `#include <dshow.h>` + `strmiids.lib`. `CoCreateInstance(CLSID_SystemDeviceEnum)` → `CreateClassEnumerator(CLSID_VideoInputDeviceCategory)` → `IPropertyBag::Read(L"FriendlyName")`. COM already initialised by WinMain.
- Live timing uses wall-clock accumulation (`m_generativeTime`), not frame PTS (device clock starts at arbitrary values). `IsLiveCapture()` gate in `ProcessFrame` skips the file-mode frame-rate gate and the end-of-stream `SeekToTime(0.0)`.
- `Stop()` / `SeekToTime(0.0)` called on a live source fails silently — harmless, no special guard needed.
- Transport: show LIVE badge + wall-clock elapsed + Stop button instead of the scrubber when `decoder.IsLiveCapture()`.

## Spout2 Integration (SpoutOutput)

`SpoutOutput.h/.cpp` — pImpl wrapper around `spoutDX` sender. Initialised in `Application::Initialize()` after D3D, called in `RenderFrame()` after `RenderToDisplay()` + `BlitAndPresent()`, before the recording path. Opt-in: `AppConfig::spoutEnabled` defaults false.

### Spout2 SDK build notes (CMakeLists.txt)

- Repo folder is `SPOUTSDK` (no underscore). DX11 API: `SPOUTSDK/SpoutDirectX/SpoutDX/`. Core impl: `SPOUTSDK/SpoutGL/` (SpoutDirectX, SpoutSenderNames, SpoutSharedMemory, SpoutUtils, SpoutFrameCount, SpoutCopy).
- Use `FetchContent_Populate` (not `FetchContent_MakeAvailable`) — MakeAvailable runs Spout2's own CMakeLists which builds GL targets that fail with `WIN32_LEAN_AND_MEAN`.
- `SpoutFrameCount.cpp` needs `<mmsystem.h>` (timeGetDevCaps etc.). Fix: `/UWIN32_LEAN_AND_MEAN` on spout_lib compile options + explicit `winmm` link.

## Audio Playback (AudioPlayer)

- Ring buffer fill is deficit-driven: `targetFill = deviceRate * 2`; `deficit = targetFill − GetBufferedSamples()`. Only submit the deficit — never more. Prevents ring filling at 20× real-time on high-fps main loops.
- `ReadAudioAhead(deficit)` then drain-loop in `kAudioBuf`-sized chunks. No per-tick cap — must recover full deficit in one ProcessFrame tick (handles Windows background throttling to 1 fps).
- **Audio EOF loop**: `ReadAudioAhead` sets `m_audioEOFReached` on `AVERROR_EOF`. ProcessFrame detects this and calls `SeekToTime(0.0)` + immediate refill WITHOUT `Flush()` — remaining ring audio plays through, then new audio from position 0 appends seamlessly. Without this, audio goes silent ~2 seconds before video EOF (ring drains while video packet queue keeps video alive).
- Flush must be called at: seek, pause, stop, close, open-video. The EOF loop path does NOT flush (appends to ring for seamless looping). Missing a flush site at any other transition causes stale audio.
- `MINIAUDIO_IMPLEMENTATION` + `#include "miniaudio.h"` must appear before any Windows headers (i.e. before `Common.h`) in `AudioPlayer.cpp`. Wrong order breaks INITGUID / WASAPI COM initialisation silently.
- miniaudio added via FetchContent (GIT_TAG master); `ole32` and `winmm` must be in target_link_libraries.
- `m_audioPlayer.GetDeviceSampleRate()` returns 0 until `Initialize()` completes — fall back to source rate when computing targetFill.

## Known Limitations

- Windows-only (Direct3D 11 requirement; the Qt UI itself is portable, the renderer is not)
- Audio playback via miniaudio (WASAPI). Volume/mute in transport bar. `AppConfig::audioVolume`/`muteAudio` persisted in config.json.
- ProRes support depends on FFmpeg build configuration
- Recording framerate matches playback framerate (no arbitrary output rates)

## Qt Notes

- **A `font-size` or `font-family` in a QSS rule overrides `QWidget::setFont()`** on every
  widget the selector matches, and Qt propagates a stylesheet font to child widgets. A base
  `QWidget { font-size: 10pt; }` therefore silently flattens every deliberate type step set
  in C++ — display headings render at body size and the whole hierarchy collapses to one
  size, with no warning and nothing wrong-looking in the code. The base font is set as the
  **application** font in `Theme::Apply` instead (an inherited default, not an override).
  Only the object-name rules in the sheet (`#PanelTitle`, `#Caption`, `#Mono`,
  `QDockWidget`) carry `font-size`, and those are meant to win.
- **KSyntaxHighlighting headers cannot be included as `<KSyntaxHighlighting/Theme>`** from
  `src/ui`. The CamelCase forwarder is a one-line `#include "theme.h"`, and MSVC resolves a
  quoted include against the directories of every already-open file before the `-I` list, so
  on a case-insensitive filesystem it resolves to our own `src/ui/Theme.h` and the
  `KSyntaxHighlighting` namespace never appears. `format.h` quotes `"theme.h"` too, so
  `<KSyntaxHighlighting/Format>` is equally poisoned. Use the lowercase real headers with
  angle brackets: `<theme.h>`, `<format.h>`, `<syntaxhighlighter.h>`, `<definition.h>`,
  `<repository.h>`.
- `Repository::addCustomSearchPath(p)` scans `p/syntax` and `p/themes`, never `p` itself.
  The call passes `":/syntax"`, so `resources/syntax/*` is aliased into `:/syntax/syntax/`
  and `:/syntax/themes/` in the qrc while staying in `resources/syntax/` on disk.
- **A `QScrollArea` with `setWidgetResizable(true)` and no horizontal scrollbar clips the
  overflow rather than scrolling it**, and the column that goes first is the rightmost.
  Any such panel must instead refuse to be narrower than its content: take the width from
  `m_content->minimumSizeHint().width()` plus the vertical scrollbar's extent, never from a
  hand-summed constant that drifts when a column is added.
- **`ViewportWidget` owns a native child HWND**, which Windows composites over everything
  Qt paints into the parent's client area. `raise()` cannot fix it (it reorders native
  siblings; an alien widget is not one), so a plain child widget placed over the picture is
  invisible. Anything that must appear over the viewport is a frameless `Qt::Tool`
  top-level — which is also the only form `setWindowOpacity` works on, since it
  early-returns on `!isWindow()`.
- **Qt paints nothing into the viewport's region**, so a `PrintWindow`/backing-store capture
  of the window shows stale pixels there rather than the rendered frame. A screenshot that
  appears to show the empty state under a live shader is a capture artefact, not a bug.
- Menu accelerators are **displayed, not bound**: the text after `\t` in an action's label.
  `Application::HandleKeyboardShortcuts` owns every key in the product and
  `MainWindow::keyPressEvent` routes into it, so a real `QShortcut` would fire the action
  twice.
- Qt key codes must be mapped to Win32 VK codes before reaching any binding code — every
  stored binding is a VK code. `src/ui/KeyMap.h` is the single mapping; it covers A-Z, 0-9
  and F1-F12 and refuses the keypad (Windows dispatches `VK_NUMPAD*`, so a binding stored
  as `'0'` would never fire) and Space/Escape (reserved actions, named by the dispatch side
  itself).
- Escape is the one shortcut a focused text field does not swallow — no `QLineEdit`,
  `QPlainTextEdit` or spin box claims it — so it reaches `keyPressEvent` mid-edit and is
  guarded there. Every printable key and Space is already consumed as typed input.
  `qobject_cast<QLineEdit*>(QApplication::focusWidget())` covers every spin box too, since
  `QAbstractSpinBox` sets its internal `QLineEdit` as focus proxy.
- **Do not style** `QComboBox::drop-down`, `QSpinBox::up-button` or `::down-button`. Any
  rule on those subcontrols switches them to CSS box layout and the arrow glyph disappears.
  The same applies to `QDockWidget`'s float and close buttons.
- QSS alpha is an integer 0-255, never 0-1: `rgba(255,255,255,0.06)` is invisible.
- QSS has no `box-shadow`. A lift is a `QGraphicsDropShadowEffect` attached in C++; note
  that the effect renders the whole widget through a pixmap, so it is the expensive channel.
- Top-level popups (`QMenu`, `QToolTip`, combo views) cannot be translucent over the canvas
  — they are their own windows and would show the desktop through. They use the token colour
  pre-composited on canvas instead.
- Tooltips work on disabled widgets with no workaround, unlike the outgoing UI.
- Every mutation path in `KeyframeDetail` must end in `Mutated()`, which is the single site
  that emits `Changed()`. A branch that returns early without it means `OnParamChanged()`
  never fires for that edit.
- Use `memcmp(a, b, N * sizeof(float)) == 0` for float-array equality checks (e.g. "is value at default"); reliable for exact IEEE 754 round-trips between storage and comparison

## VideoEncoder Notes

- `time_base = {1, fps*1000}` → one frame = **1000 time_base units**. PTS must be `frameIndex * 1000LL`, not `frameIndex`. Getting this wrong produces a valid-but-broken file where all frames are crammed into ~2ms, which players display as a frozen single frame.
- `RenderToTexture()` leaves the active RT as `m_renderTextureRTV`. Call `BeginFrame()` after it so the pipeline state the rest of the frame depends on is put back.
- Recording capture must happen **after** `RenderToDisplay()` (video pipeline state active) and **before** any subsequent `BeginFrame()` that might alter the video texture or cbuffer. Gate submission on `m_newVideoFrame` so the encoder receives exactly one frame per decoded video frame — not one per display frame.

## ShaderManager API

- `GetPreset(int)` is non-const; use `GetPresets()` (returns `const std::vector<ShaderPreset>&`) when calling from a `const` method
- `SetActivePreset` is called from `Application.cpp`, `ui/LibraryPanel.cpp`, `ui/MainWindow.cpp` and `ui/dialogs/NewShaderDialog.cpp` — every new call site owes `OnParamChanged()` and `MainWindow::RefreshParameters()`, plus `RefreshLibrary()` if it also added or renamed a preset (see "The refresh contract")
- `GetActivePresetIndex()` returns `int` (−1 = passthrough); `GetActivePreset()` returns `ShaderPreset*` (null when passthrough). Never guess `GetActiveIndex` — it doesn't exist.

## Application API

- **Lifecycle**: `InitializeQt()` (a `QApplication` must already exist), `TickOnce()` →
  `bool` (whether the viewport presented), `Shutdown()`, `RequestExit()` (sets the flag and
  calls `QCoreApplication::quit()`; without it File > Exit is dead, since there is no
  message loop of ours to break out of). `GetMainWindow()` returns `MainWindow*`, which is
  null until the window is built — every call site must tolerate that, because `OpenVideo`
  and friends are reachable from inside `InitializeQt`.
- **Initialisation order in `InitializeQt` is load-bearing**: renderer (device only, from a
  provisional size), then `ShaderManager` and `WorkspaceManager`, and only then `MainWindow`
  + `show()` + `ViewportWidget::CreateSwapChain()`. Panels read those subsystems from their
  own constructors (`LibraryPanel::Refresh` dereferences `GetShaderManager()`), so building
  the window earlier is an access violation. `RefreshAll()` and `OnParamChanged()` go last,
  after the preset and workspace restore.
- **Teardown order too**: `Shutdown()` resets `m_mainWindow` before `m_renderer.Shutdown()`.
  The window owns the viewport, the viewport owns a swap chain created from the renderer's
  device, and a swap chain must not outlive its device.
- `OpenRecordingOutputDialog(const std::string& currentPath)` returns the chosen path, or an
  empty string on cancel.
- `FindBindingConflict(vkCode, modifiers, excludeShaderIdx, excludeWorkspaceIdx)` — returns human-readable conflict string (empty = free). Checks hardcoded reserved keys (Space, Escape, F1–F7, F9, Ctrl+N/O/S), all shader presets, all workspace presets. Use this whenever assigning any new keybinding. Reserved F-keys: F1 Editor, F2 Library, F3 Transport, F4 Recording, F5 Compile, F6 Keybindings, F7 Video Output Window, F8 Spout Output, F9 Record toggle.
- `GetConfig()` returns a non-const `AppConfig&` — a panel can write preferences directly and call `SaveConfig()` to persist. Used by the `timeDisplayFrames` toggle and by `MainWindow::SetReducedMotion`.
- `RegenerateNoise()` — reads `AppConfig::noise`, calls `D3D11Renderer::UpdateNoiseTexture`, saves config. Use this; do not call `UpdateNoiseTexture` directly.
- `GetAudioData()` returns `const AudioData&` — live band/spectrum values; read by `ParamsPanel` for AudioBand meters and by `AudioPanel` for the bands and spectrum.
- `UpdateAudioSettings()` — writes `AppConfig::audio` from UI sliders to `m_audioAnalyzer`, then calls `SaveConfig()`.

## AppConfig Persistence Pattern

Adding a new config field requires three changes: default value in `Common.h` (`AppConfig` struct), entry in `to_json`, and a `if (j.contains(...))` guard in `from_json` — both in `ConfigManager.cpp`.

## VideoDecoder API

`VideoDecoder` exposes `GetFPS()`, `GetFrameCount()`, `GetDuration()`, `GetCurrentTime()` — sufficient for any frame-based UI without new API. `Keyframe::time` and all playback state is always stored in seconds; display layers convert via `fps`. Never store frame numbers in the data model.

Audio stream support: `HasAudio()`, `GetAudioSampleRate()`, `DrainAudioSamples(buf, maxFloats)`. `OpenAudioStream()` called internally during `Open()`. Uses libswresample: `swr_alloc_set_opts2` with `AV_CHANNEL_LAYOUT_MONO` + `AV_SAMPLE_FMT_FLTP` handles all source channel counts. Decoded samples accumulate in `m_audioPending`; `DrainAudioSamples` copies and erases consumed floats. Audio packets in the decode loop call `DecodeAudioPacket()` + `continue` instead of being dropped.

## C++ / Dependency Gotchas

- KissFFT `.c` files require `LANGUAGES CXX C` in the CMake `project()` declaration — omitting `C` causes a linker language error on the static lib target.
- KissFFT include: use `#include <kiss_fftr.h>` (angle-bracket), not quoted — the kissfft root is on the include path via `target_include_directories`.
- PowerShell `Set-Content -Encoding UTF8` writes a BOM that fxc rejects with a parse error. Use `[System.IO.File]::WriteAllText($path, $content, [System.Text.Encoding]::ASCII)` when writing HLSL to temp files for fxc validation.
- `nlohmann/json.hpp` is ~25,000 lines — include it only in `.cpp` files, never in headers
- nlohmann/json `try/catch` must wrap the full processing loop, not just `json::parse` — `get<>()` and `value()` throw `type_error` on type mismatches
- HLSL intrinsic shadowing: never name local variables after HLSL built-ins (`frac`, `min`, `max`, `abs`, `lerp`, etc.) or HLSL reserved words (`line`, `point`, `triangle`, `linear`, `sample`, etc.)
- `atanh` is NOT a built-in in HLSL ps_5_0 — using it produces X3004 "undeclared identifier". Implement manually: `float myAtanh(float x) { x = clamp(x,-0.9999,0.9999); return 0.5*log((1.0+x)/(1.0-x)); }`
- ISF `long` params without a `VALUES` array produce an empty, un-selectable dropdown at runtime. `MIN`/`MAX` alone are not sufficient — always include `"VALUES": [...]` and `"LABELS": [...]`. A `DEFAULT` not present in `VALUES` leaves the combo stuck.
- Audio band values (`audioBass`, `audioHigh`, etc.) are typically 0.01–0.3 for music. Shader multipliers need to be 3–5× higher than intuition suggests to produce a visible effect.
- For offset-based video effects (chromatic aberration, lens warp, etc.), default `Strength` must produce ≥10px offset at 1080p to be perceptible. The lens formula `offset = (uv-0.5) * s` gives only ~4px at the corner with `s=0.003`; use `s≥0.01` and `MAX≥0.05` as a baseline.
- `std::stoi` throws `std::invalid_argument`/`std::out_of_range` on malformed input — use `std::from_chars` (C++17, `<charconv>`) for parsing untrusted file content; it is noexcept and leaves the output unchanged on failure
- `AV_CHANNEL_LAYOUT_MONO` is a compound literal — MSVC C++ mode rejects `&AV_CHANNEL_LAYOUT_MONO` directly. Assign to a local first: `AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO; av_channel_layout_copy(&ctx->ch_layout, &mono);`

## Shader Parameter System

See `docs/shader-parameter-guide.md` for the author-facing reference. Technical notes for development:

### ISF JSON Block Parsing

`SHADER_TYPE` values: `"generative"` (sets `isGenerative`), `"audio"` (sets `isAudio`), absent/other = video effect. Shader Library shows three sections: AUDIO REACTIVE (`isAudio`), GENERATIVE (`isGenerative`), VIDEO EFFECTS (neither). `isAudio` shaders use AudioBand inputs, have `SHADER_TYPE: "audio"` in the ISF block, and receive auto-injected audio preamble.

`ShaderManager::ParseISFParams(const std::string& source)` extracts parameter metadata:
1. Find first `/*{` in source; extract up to matching `}*/`.
2. Parse body with nlohmann/json; silent failure (throw) returns empty vector — this IS a compile failure: empty params generates no `#define` aliases, causing undeclared identifier errors in `D3DCompile`. The shader is silently dropped by `ScanDirectory`.
3. Iterate `"INPUTS"` array; construct `ShaderParam` per entry with `cbufferOffset` assigned sequentially.
4. Called from `CompilePreset` and `RecompilePreset` before `D3DCompile` — updates `preset.params`.

ISF `long` parameters: `VALUES` array contains **integers** (the actual selectable values), `LABELS` contains **strings** (display text). Never call `get<string>()` on `VALUES`. `ShaderParam::longValues` (parallel to `longLabels`) stores these int values; the Long combo UI maps combo index→value via `longValues[idx]` and stores the actual int (not the index) into `values[0]`.

The ISF block is **not stripped** — HLSL ignores block comments. No source modification required.

### #define Alias Generation

After parsing, a `#define` preamble is prepended to the source passed to `D3DCompile`. Mapping:
- float offset `N` → array index `N/4`, component `"xyzw"[N%4]`
- `float`/`event`: `#define Name custom[idx].comp`
- `bool`: `#define Name (custom[idx].comp > 0.5)`
- `long`: `#define Name int(custom[idx].comp)`
- `point2d` (2 floats, even-aligned): `#define Name float2(custom[idx].ab, custom[idx].cd)`
- `color` (4 floats, 4-aligned): `#define Name custom[idx]`
- `audio` (AudioBand): `cbufferOffset = -1`, consumes NO `custom[]` slot. `"BAND"` field maps to: `"rms"→audioRms`, `"bass"→audioBass`, `"mid"→audioMid`, `"high"→audioHigh`, `"beat"→audioBeat`, `"centroid"→audioSpectralCentroid`. Preamble auto-injects the `AudioConstants` cbuffer + `spectrumTexture` declaration when any AudioBand param is present. AudioBand params show as read-only `ProgressBar` in the UI; not persisted to config; not keyframeable.

The original source on disk is never modified.

`ShaderParam::min`/`max`/`step` are **scalar floats**. `ParseISFParams` reads `MIN`/`MAX`/`STEP` only when the JSON value `is_number()` — array-form values (e.g. `"MIN": [0.0, 0.0]` for `point2d`) are silently skipped and the field stays at its default (0.0/1.0/0.01). Write scalar `MIN`/`MAX` for all param types, including `point2d` and `color`, if you want non-default UI bounds. Array-form used to throw `nlohmann::detail::type_error` and silently discard the entire param list; the `is_number()` guard fixed this.

### Cbuffer Packing Rules

Parameters packed into `custom` (`float4 custom[8]` = 32 floats, `kCustomFloats` in `Common.h`) sequentially:
- `float`, `bool`, `long`, `event`: 1 float, no alignment
- `point2d`: 2 floats, aligned to next even offset
- `color`: 4 floats, aligned to next multiple-of-4 offset

Parameters exceeding 32 floats total are skipped with a warning appended to `ShaderPreset::compileError`.

**Diagnosing missing shaders**: if a shader doesn't appear after Scan Folder, it has a compile error. Check `ShaderPreset::compileError` in the debugger — no UI currently surfaces this field.

**Dead parameters**: a parameter declared in the ISF block but never read by the shader body still parses, still packs a `custom[]` slot and still renders a working widget that does nothing. Neither the compiler nor `validate_shaders.py` can see it, because the `#define` alias is simply unused. When editing a shader, check every declared name appears in the body.

### Value Storage and GPU Upload

- `ShaderParam::values[4]` holds current values; `defaultValues[4]` holds parsed defaults.
- On any widget change: `Application::OnParamChanged()` packs all `params` into a `float[kCustomFloats]` scratch buffer at their `cbufferOffset`s, then calls `D3D11Renderer::SetCustomUniforms`. Effect visible on next `BeginFrame`.
- `event` type: set `values[0] = 1.0f` on button press; a one-frame flag in `Application` zeros it after the next `RenderFrame` submission.
- No per-frame CPU cost — `SetCustomUniforms` called only on user interaction and on shader activation. Exception: during keyframe playback, `OnParamChanged` fires every frame for animated parameters (the interpolated value changes each frame).

### Randomiser

`ParamsPanel::RandomiseParam(ShaderParam&)` rolls one parameter and returns false for types with no value to roll (Event, AudioBand, and Long with an empty `VALUES` list). Every branch draws from the same range its widget exposes, so a rolled value is always one the user could have set by hand. Colour alpha is deliberately left alone — it is an opacity everywhere it is read, and randomising it makes effects invisible. The per-parameter `R` button and the panel's "Randomise all" both skip parameters under active keyframe control, since the timeline would overwrite the roll on the next frame.

### Persistence

`ConfigManager` serialises `ShaderParam::values` and keyframe timelines to `config.json`. On load, parsed params matched by `name` to restore saved values and keyframes; unmatched params use `defaultValues`.

### Keyframe Animation

Per-parameter keyframe animation tied to absolute video time. Each `ShaderParam` has an `std::optional<KeyframeTimeline>` (nullopt until user enables keyframing via the KF toggle).

**Data model** (Common.h): `KeyframeTimeline` holds a sorted `std::vector<Keyframe>`. Each `Keyframe` has `time` (seconds), `values[4]`, `InterpolationMode` (Linear/EaseInOut/CubicBezier), and `BezierHandles` (two control points for cubic bezier curves).

**Evaluation pipeline** (Application.cpp): `EvaluateKeyframes()` runs in `RenderFrame()` after `SetShaderTime` and before `BeginFrame`. For each animated parameter, it calls `KeyframeTimeline::Evaluate()` at `m_playbackTime`, writes interpolated values to `param.values[]`, then calls `OnParamChanged()`. Bool/Long params use step interpolation (snap after lerp). Event params are not keyframeable.

**UI** (`ui/ParamsPanel`, `ui/KeyframeDetail`, `ui/BezierEditor`): a KF toggle per param (except Event). When enabled, `ParamsPanel::KeyframeDetailHost(paramIndex)` reveals a full-width host in the grid row beneath that parameter, carrying the "+ Key" button, the timestamp chips (click to seek), the time/value editors, the interpolation combo, and the inline bezier editor (200x140 px minimum, 12 px grab targets). Widgets are disabled during keyframe playback, with a tooltip saying why. Diamond markers appear on the transport scrubber for the selected parameter, and can be dragged there under follow mode or Shift.

**Persistence** (ConfigManager.cpp): Keyframes serialised as `"keyframes": { "ParamName": { "enabled": true, "keys": [...] } }` in config.json, keyed by param name. Restored via `ShaderPreset::savedKeyframes` on load.

**Important**: the keyframe selection lives in `ParamsPanel` (`SetKeyframeSelection`, `SelectedKeyframeParam`, `SelectedKeyframeIndex`, signal `KeyframeSelectionChanged`) and must be cleared at three sites — when the preset changes, when a stale index outruns the new param list, and when a KF toggle goes off — or it indexes into a different preset's vectors. `TransportPanel`'s follow mode resets with it.

**Keyframe reposition pattern**: copy the keyframe, call `RemoveKeyframe(idx)`, call `AddKeyframe(copy)`, update `m_selectedKeyframeIndex` with the returned index. Never modify `kf.time` in-place — sorted order is maintained only via remove/re-insert.

**`Application::GetPlaybackTime()` returns `float`** — no cast needed.

### Blend Mode

Video blend is available to all shader types (video effects, generative, audio). The UI condition is simply `GetDecoder().IsOpen()` — any shader can overlay or blend against a video or live source, including audio visualisers (e.g. waveform over video). Do not gate by `isGenerative` or exclude `isAudio`.

## Claude Code Automations

All automations live under `.claude/`. Do not edit `config.json` directly — it is runtime-generated by ShaderPlayer and blocked by a PreToolUse hook.

### MCP Server: context7

Live documentation lookup for D3D11, HLSL, FFmpeg, and Qt APIs.

**Installed**: `claude mcp add context7 -- cmd /c npx -y @upstash/context7-mcp` (stored in `.claude.json`; Windows requires `cmd /c` wrapper)

**Usage**: Ask Claude to look up API docs mid-task, e.g. "use context7 to check the D3D11_TEXTURE2D_DESC fields" or "look up av_seek_frame parameters". Claude resolves current library docs rather than relying on training data.

### Skill: `/new-shader`

Scaffolds a new `.hlsl` file in `default_shaders/` with the correct cbuffer layout, ISF JSON block, and parameter packing — preventing the silent bugs documented in the HLSL gotchas above.

**Defined in**: `.claude/skills/new-shader/SKILL.md`

**Usage**: `/new-shader bloom` or `/new-shader film grain with intensity and speed controls`

Claude will write `default_shaders/<name>.hlsl` with ISF INPUTS tailored to the description, correct cbuffer structure, and no intrinsic name shadowing. After creation, use Shader Library → "Scan Folder" to load it.

### Subagent: shader-reviewer

Specialist reviewer for HLSL shaders. Checks cbuffer layout compliance, ISF packing offset correctness, intrinsic name shadowing, entry point signature, and UV sampling patterns.

**Defined in**: `.claude/agents/shader-reviewer.md`

**Usage**: After writing or modifying any `.hlsl` file, ask Claude: "use the shader-reviewer agent to check shaders/bloom.hlsl". The agent walks through every check and reports violations with line numbers and fixes.

### Hooks

Defined in `.claude/settings.json`:

**PreToolUse — block config.json edits**: Intercepts Write/Edit calls targeting `config.json` and exits with an error message. `config.json` is written by ShaderPlayer at runtime; source-of-truth for shader presets is the `.hlsl` files and the in-app library.

**PostToolUse — HLSL syntax validation**: After any Write/Edit to a `.hlsl` file, runs `tools/validate_shaders.py` on it (see Validating Shaders), so compile errors appear immediately without a CMake build cycle. Editing `src/ShaderCommon.hlsli` prints a reminder to run the full pass instead — a 45-shader sweep takes ~15 s, too slow to block every edit.
