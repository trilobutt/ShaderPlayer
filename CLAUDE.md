# ShaderPlayer

## Project Overview

Real-time HLSL shader video player for Windows 11: D3D11 pixel shader pipeline applied to FFmpeg-decoded video, with live editor, hot-swappable presets, and uninterrupted recording.

## Architecture

### Technology Stack

- **Language**: C++20
- **Build System**: CMake 3.20+
- **Graphics API**: Direct3D 11 (HLSL shader model 5.0)
- **Video Decoding**: FFmpeg (libavcodec, libavformat, libavutil, libswscale)
- **Video Encoding**: FFmpeg (H.264/H.265 output)
- **UI Framework**: ImGui with Win32 backend (docking branch)
- **Code Editor**: ImGuiColorTextEdit (TextEditor)
- **JSON**: nlohmann/json

### Component Structure

```
src/
├── main.cpp              - WinMain: CoInitializeEx (COM required for IFileOpenDialog),
│                           DPI awareness, Application lifetime
├── Common.h              - Shared types: VideoFrame, ShaderPreset, RecordingSettings,
│                           AppConfig (shaderDirectory default = "shaders"), PlaybackState,
│                           Keyframe, KeyframeTimeline, BezierHandles, InterpolationMode
├── KeyframeTimeline.cpp  - KeyframeTimeline method implementations: Evaluate() (cubic
│                           bezier, smoothstep, linear interpolation with binary search),
│                           AddKeyframe() (sorted insert, overwrites duplicates),
│                           RemoveKeyframe() (bounds-checked erase).
├── Application.{cpp,h}   - Central coordinator. Owns all other components. Drives
│                           ProcessFrame() (video decode) + RenderFrame() (D3D + ImGui)
│                           each tick. Handles WndProc, drag-drop (.hlsl → shader,
│                           other → video), keyboard shortcuts, file dialogs including
│                           ScanFolderDialog() (IFileOpenDialog + FOS_PICKFOLDERS).
├── AudioAnalyzer.{cpp,h} - Pure DSP class. Owns KissFFT plan, ring buffer (2048
│                           samples), Hann window, and beat history. Fed by
│                           VideoDecoder::DrainAudioSamples() in Application::
│                           ProcessFrame(). Outputs AudioData (rms/bass/mid/high/
│                           beat/spectralCentroid + 256-bin spectrum). No threads.
│                           Reset() on seek/close/EOF loop.
├── VideoDecoder.{cpp,h}  - FFmpeg wrapper: Open/Close, DecodeNextFrame() → VideoFrame
│                           (RGBA8 in data[0]), SeekToTime(). Blocking decode, called
│                           from main thread in ProcessFrame().
├── VideoEncoder.{cpp,h}  - FFmpeg recording: StartRecording/StopRecording, SubmitFrame()
│                           from RenderFrame() after CopyRenderTargetToStaging().
├── D3D11Renderer.{cpp,h} - D3D11 device, swap chain, and fullscreen-triangle pipeline.
│                           Key methods: BeginFrame() sets entire pipeline state
│                           (PSSetShader with m_activePS, PSSetShaderResources,
│                           PSSetSamplers, PSSetConstantBuffers) — MUST be called before
│                           RenderToDisplay(). RenderToDisplay() draws to m_displayTexture
│                           (read back by ImGui::Image via GetDisplaySRV()). EndFrame()
│                           draws to backbuffer but is NOT called in RenderFrame (display
│                           goes via ImGui::Image only). SetActivePixelShader() stores the
│                           pointer in m_activePS; GPU state is updated on next BeginFrame.
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
├── UIManager.{cpp,h}     - ImGui panels: Video viewport (ImGui::Image of GetDisplaySRV),
│                           Shader Library (preset list, "Scan Folder" → ScanFolderDialog,
│                           "+ New" modal), Shader Editor (TextEditor + Compile button,
│                           auto-compile on change after 500 ms delay), Transport controls,
│                           Recording settings, Notifications overlay.
│                           Compile button calls Application::CompileCurrentShader().
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
                              exe for `.ini` files (custom [WorkspacePreset] header +
                              verbatim ImGui ini blob). Index 0 = built-in Default
                              (kDefaultLayoutIni constant — replace after first live run).
                              SavePreset calls ImGui::SaveIniSettingsToMemory; LoadPreset
                              calls ImGui::LoadIniSettingsFromMemory. Owned by Application.
```

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
    float4 custom[4];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET { ... }
```

Included shaders in `shaders/`:
- `audio_spectrum.hlsl` - Spectrum bar visualiser with beat flash (SHADER_TYPE: "audio")
- `audio_bass_pulse.hlsl` - Bass-reactive chromatic aberration + beat flash on video (SHADER_TYPE: "audio")
- `audio_waveform.hlsl` - Spectrum waveform overlay on video (SHADER_TYPE: "audio")
- `passthrough.hlsl` - Direct video pass-through (no effect)
- `grayscale.hlsl` - Luminance-based desaturation
- `vignette.hlsl` - Radial darkening
- `chromatic_aberration.hlsl` - RGB channel offset
- `sharpen.hlsl` - Convolution-based sharpening
- `false_colour.hlsl` - Luminance-based false colour mapping
- `pixelate.hlsl` - Pixelation with optional grid overlay; demonstrates all ISF widget types
- `receipt_bars.hlsl`, `halftone.hlsl`, `pixel_sdf.hlsl`, `pixel_matrix.hlsl`, `ascii_noise.hlsl` — luma-based pixelated pattern effects
- `led_panel.hlsl`, `crochet.hlsl`, `lego_bricks.hlsl`, `fluted_glass.hlsl`, `depixelation.hlsl` — advanced stylised effects

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
- UI: View → Noise Generator (`UIManager::DrawNoisePanel` / `m_showNoisePanel`).
- Config: `AppConfig::noise` (`NoiseSettings { float scale; int textureSize; }`), persisted as `noiseScale`/`noiseTextureSize` in `config.json`.
- Noise UV pattern for per-cell variation: `cellCoord / 64.0 + cellUv * (freq / 64.0)` — unique slice per cell, `freq` scales zoom.
- HSV helpers (`rgb2hsv`/`hsv2rgb`) used by some shaders — copy the compact float4-swizzle implementation from `crochet.hlsl` or `lego_bricks.hlsl`.

## Build Instructions

### Prerequisites

- Windows 11 (Windows 10 may work but untested)
- Visual Studio 2022 (or MinGW-w64 with C++20 support)
- CMake 3.20 or later
- FFmpeg development libraries (headers + libs)

### FFmpeg Setup

FFmpeg headers and import libs are bundled in `third_party/ffmpeg/` and committed to the repo (~3 MB). Only the runtime DLLs (~220 MB) must be provided locally — they are gitignored.

On a fresh clone:
1. Download a shared FFmpeg build from https://www.gyan.dev/ffmpeg/builds/ (`ffmpeg-release-full-shared.7z`)
2. Copy the DLLs into `third_party/ffmpeg/bin/`:
   ```
   xcopy "C:\path\to\ffmpeg\bin\*.dll" "third_party\ffmpeg\bin\"
   ```
3. CMake autodiscovers `third_party/ffmpeg/` — no flags needed

If you prefer a system-level FFmpeg install instead, pass `-DFFMPEG_ROOT=<path>` to CMake and leave `third_party/ffmpeg/` unpopulated.

### Building

```bash
cmake -B build
cmake --build build --config Release   # → build/Release/ShaderPlayer.exe
cmake --build build --config Debug     # → build/Debug/ShaderPlayer.exe
```

The executable and required DLLs will be in `build/Release/` (or `build/Debug/`). FFmpeg DLLs are copied there automatically at post-build.

**Run from the project root** (not from `build/Release/`) so the relative `shaders/` path resolves correctly, or use the Shader Library → "Scan Folder" button to point at the shaders directory manually. A fallback also looks for `shaders/` next to the executable at startup.

## Configuration

Configuration is stored in `config.json` next to the executable (created on first run if missing).

### Keybindings

Shader shortcut keys are stored per-preset as virtual key codes (`shortcutKey`, `shortcutModifiers`). Set them via right-click → "Set Keybinding..." in the Shader Library.

### Shader Presets

Presets saved to config include only the `filepath` and shortcut — source is re-read from disk on load. shaderDirectory is also saved so "Scan Folder" persists across sessions.

### Workspace Presets

Layout presets stored as `.ini` files in `layouts/` next to the executable (path in `AppConfig::layoutsDirectory`). Not referenced in `config.json` — discovered by `WorkspaceManager::ScanDirectory()` at startup. Keybindings are in the `.ini` file headers, not config.json. Access via View > Workspace Presets.

## Development Notes

### Render Loop (RenderFrame)

Each frame:
1. `UploadVideoFrame()` — maps video texture and DMA-copies current VideoFrame (RGBA8)
2. `BeginFrame()` — updates cbuffer, clears backbuffer, sets **entire** PS pipeline state including `m_activePS`
3. `RenderToDisplay()` — changes RT to `m_displayTexture`, calls `Draw(3,0)`, restores backbuffer RT
4. `VideoOutputWindow::BlitAndPresent()` (if open) — calls `BlitDisplayTo()` then presents the second swap chain
5. ImGui render pass — `ImGui::Image(GetDisplaySRV(), ...)` composites the processed frame
6. `Present(vsync=true)`

`BlitDisplayTo(rtv, w, h)` — draws `m_displaySRV` via passthrough PS into the given RTV, then restores main backbuffer RT, main viewport, `m_activePS`, and `m_videoSRV` as t0. Safe to call between `RenderToDisplay()` and recording capture.

`EndFrame()` (draws fullscreen triangle to backbuffer) is intentionally not called — the video is displayed via `ImGui::Image`, not a direct backbuffer draw.

### Shader Compile Path

- **Editor compile (F5)**: `Application::CompileCurrentShader(source)` → updates `preset->source`, calls `ShaderManager::RecompilePreset(activeIndex)` (index-based, reliable), then `SetActivePreset(activeIndex)` to push new `m_activePS` to the renderer. Effect appears on the next `BeginFrame`.
- **Initial load / scan**: `LoadShaderFromFile` → `CompilePreset(localPreset)` (pointer search finds nothing, compiled shader discarded) → `AddPreset(preset)` recompiles and stores. This double-compile is intentional to keep `AddPreset` as the single point of vector-sync.
- **No active preset + compile**: `AddPreset` creates the preset, `SetActivePreset` applies it.

### Parallel Vectors in ShaderManager

`m_presets` and `m_compiledShaders` are always the same length. Every `AddPreset` does both `push_back`s; every `RemovePreset` does both `erase`s. Never modify one without the other.

### COM Requirement

`ScanFolderDialog()` uses `IFileOpenDialog` (Vista+ shell COM API). `CoInitializeEx` / `CoUninitialize` are called in `WinMain`. The COM apartment must be initialised before any shell dialog is shown.

### shaderDirectory Path Resolution

`AppConfig::shaderDirectory` defaults to `"shaders"` (CWD-relative). At startup, if that path doesn't exist, `Application::Initialize` falls back to `<exe_dir>/shaders`. Config is saved with the resolved path after "Scan Folder" is used, so it persists.

### Adding a New Shader

Use the `/new-shader <name>` skill — it scaffolds the file with correct cbuffer layout and ISF block. Then Shader Library → "Scan Folder" to load it.

## Live Capture (Webcam / RTSP)

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

## Known Limitations

- Windows-only (Direct3D 11 requirement)
- No audio **playback** (audio is decoded and analysed for shader reactivity; playback through speakers is not implemented)
- ProRes support depends on FFmpeg build configuration
- Recording framerate matches playback framerate (no arbitrary output rates)

## ImGui Notes

- `ImGuiKey` is not 1:1 with Win32 VK codes (broken since ImGui 1.87) — use `GetKeyState(VK_*)` for key state in modal/input code
- `ImGui::SameLine(x)` takes absolute offset from window left — use `GetContentRegionMax().x` for right-alignment, not `GetContentRegionAvail().x`
- Static locals in modal draw functions persist across sessions; use an `s_wasOpen` bool sentinel to reset edge-detection state when a modal reopens
- `EndPopup()` closes ALL popups including modals — `EndPopupModal` does not exist; using it causes a compile error
- `ImGui::SaveIniSettingsToMemory(&size)` / `ImGui::LoadIniSettingsFromMemory(str, size)` — captures and restores full docking layout; safe to call outside a frame
- `ImGui_ImplDX11_RenderDrawData` **saves and restores all D3D11 pipeline state** (VS, PS, CBs, SRVs, RTVs, viewports). Code after `EndFrame()` has the same pipeline state as before `BeginFrame()` — do not assume ImGui has clobbered it.
- Toggle buttons using `PushStyleColor`/`PopStyleColor`: snapshot the bool BEFORE the button call (`bool wasActive = m_flag; if (wasActive) Push...; if (Button(...)) m_flag=!m_flag; if (wasActive) Pop...`). Checking `m_flag` after the button call breaks push/pop symmetry on the click frame.
- `DrawKeyframeDetail` receives `anyChanged` by reference — set it on ALL mutation paths including early returns, or `OnParamChanged()` won't fire for that edit.
- `ImGui::ArrowButton("##id", ImGuiDir_Left/Right/Up/Down)` renders a built-in arrow button — use instead of Unicode arrows (default font is ASCII-only)
- `ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)` — required to show tooltips on disabled widgets; plain `IsItemHovered()` returns false when the item is disabled
- Use `memcmp(a, b, N * sizeof(float)) == 0` for float-array equality checks (e.g. "is value at default"); reliable for exact IEEE 754 round-trips between storage and comparison

## VideoEncoder Notes

- `time_base = {1, fps*1000}` → one frame = **1000 time_base units**. PTS must be `frameIndex * 1000LL`, not `frameIndex`. Getting this wrong produces a valid-but-broken file where all frames are crammed into ~2ms, which players display as a frozen single frame.
- `RenderToTexture()` leaves the active RT as `m_renderTextureRTV` (not the backbuffer). Call `BeginFrame()` after to restore the backbuffer RT; otherwise ImGui's save/restore will lock in the render texture as the RT for the rest of the frame.
- Recording capture must happen **after** `RenderToDisplay()` (video pipeline state active) and **before** any subsequent `BeginFrame()` that might alter the video texture or cbuffer. Gate submission on `m_newVideoFrame` so the encoder receives exactly one frame per decoded video frame — not one per display frame.

## ShaderManager API

- `GetPreset(int)` is non-const; use `GetPresets()` (returns `const std::vector<ShaderPreset>&`) when calling from a `const` method
- `SetActivePreset` is called in **both** `Application.cpp` and `UIManager.cpp` — after any new call-site, always add `OnParamChanged()` (Application) or `m_app.OnParamChanged()` (UIManager)
- `GetActivePresetIndex()` returns `int` (−1 = passthrough); `GetActivePreset()` returns `ShaderPreset*` (null when passthrough). Never guess `GetActiveIndex` — it doesn't exist.

## Application API

- `FindBindingConflict(vkCode, modifiers, excludeShaderIdx, excludeWorkspaceIdx)` — returns human-readable conflict string (empty = free). Checks hardcoded reserved keys (Space, Escape, F1–F7, F9, Ctrl+N/O/S), all shader presets, all workspace presets. Use this whenever assigning any new keybinding. Reserved F-keys: F1 Editor, F2 Library, F3 Transport, F4 Recording, F5 Compile, F6 Keybindings, F7 Video Output Window, F8 Spout Output, F9 Record toggle.
- `GetConfig()` returns a non-const `AppConfig&` — UIManager can write preferences directly and call `SaveConfig()` to persist. Used by the `timeDisplayFrames` toggle.
- `RegenerateNoise()` — reads `AppConfig::noise`, calls `D3D11Renderer::UpdateNoiseTexture`, saves config. Use this; do not call `UpdateNoiseTexture` directly.
- `GetAudioData()` returns `const AudioData&` — live band/spectrum values; used by UIManager for AudioBand ProgressBar meters.
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
- `std::stoi` throws `std::invalid_argument`/`std::out_of_range` on malformed input — use `std::from_chars` (C++17, `<charconv>`) for parsing untrusted file content; it is noexcept and leaves the output unchanged on failure

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

### Cbuffer Packing Rules

Parameters packed into `custom[16]` (= `float4 custom[4]`) sequentially:
- `float`, `bool`, `long`, `event`: 1 float, no alignment
- `point2d`: 2 floats, aligned to next even offset
- `color`: 4 floats, aligned to next multiple-of-4 offset

Parameters exceeding 16 floats total are skipped with a warning appended to `ShaderPreset::compileError`.

**Diagnosing missing shaders**: if a shader doesn't appear after Scan Folder, it has a compile error. Check `ShaderPreset::compileError` in the debugger — no UI currently surfaces this field.

### Value Storage and GPU Upload

- `ShaderParam::values[4]` holds current values; `defaultValues[4]` holds parsed defaults.
- On any widget change: `Application::OnParamChanged()` packs all `params` into a `float[16]` scratch buffer at their `cbufferOffset`s, then calls `D3D11Renderer::SetCustomUniforms`. Effect visible on next `BeginFrame`.
- `event` type: set `values[0] = 1.0f` on button press; a one-frame flag in `Application` zeros it after the next `RenderFrame` submission.
- No per-frame CPU cost — `SetCustomUniforms` called only on user interaction and on shader activation. Exception: during keyframe playback, `OnParamChanged` fires every frame for animated parameters (the interpolated value changes each frame).

### Persistence

`ConfigManager` serialises `ShaderParam::values` and keyframe timelines to `config.json`. On load, parsed params matched by `name` to restore saved values and keyframes; unmatched params use `defaultValues`.

### Keyframe Animation

Per-parameter keyframe animation tied to absolute video time. Each `ShaderParam` has an `std::optional<KeyframeTimeline>` (nullopt until user enables keyframing via the KF toggle).

**Data model** (Common.h): `KeyframeTimeline` holds a sorted `std::vector<Keyframe>`. Each `Keyframe` has `time` (seconds), `values[4]`, `InterpolationMode` (Linear/EaseInOut/CubicBezier), and `BezierHandles` (two control points for cubic bezier curves).

**Evaluation pipeline** (Application.cpp): `EvaluateKeyframes()` runs in `RenderFrame()` after `SetShaderTime` and before `BeginFrame`. For each animated parameter, it calls `KeyframeTimeline::Evaluate()` at `m_playbackTime`, writes interpolated values to `param.values[]`, then calls `OnParamChanged()`. Bool/Long params use step interpolation (snap after lerp). Event params are not keyframeable.

**UI** (UIManager.cpp): KF toggle button per param (except Event). When enabled: "+ Key" button adds a keyframe at current playback time, timestamp chips seek on click, detail panel shows time/value editors, interpolation mode combo, and a 160x100px inline bezier curve editor with draggable control points. Widgets are disabled during keyframe playback. Diamond markers appear on the transport timeline for the selected parameter.

**Persistence** (ConfigManager.cpp): Keyframes serialised as `"keyframes": { "ParamName": { "enabled": true, "keys": [...] } }` in config.json, keyed by param name. Restored via `ShaderPreset::savedKeyframes` on load.

**Important**: `m_selectedKeyframeParam` and `m_selectedKeyframeIndex` (UIManager) must be reset to -1 whenever the active preset changes, to prevent stale indices into a different preset's param/keyframe vectors. Also reset `m_keyframeFollowMode` at the same sites, and when the KF toggle is disabled for a param.

**Keyframe reposition pattern**: copy the keyframe, call `RemoveKeyframe(idx)`, call `AddKeyframe(copy)`, update `m_selectedKeyframeIndex` with the returned index. Never modify `kf.time` in-place — sorted order is maintained only via remove/re-insert.

**`Application::GetPlaybackTime()` returns `float`** — no cast needed.

## Claude Code Automations

All automations live under `.claude/`. Do not edit `config.json` directly — it is runtime-generated by ShaderPlayer and blocked by a PreToolUse hook.

### MCP Server: context7

Live documentation lookup for D3D11, HLSL, FFmpeg, and ImGui APIs.

**Installed**: `claude mcp add context7 -- cmd /c npx -y @upstash/context7-mcp` (stored in `.claude.json`; Windows requires `cmd /c` wrapper)

**Usage**: Ask Claude to look up API docs mid-task, e.g. "use context7 to check the D3D11_TEXTURE2D_DESC fields" or "look up av_seek_frame parameters". Claude resolves current library docs rather than relying on training data.

### Skill: `/new-shader`

Scaffolds a new `.hlsl` file in `shaders/` with the correct cbuffer layout, ISF JSON block, and parameter packing — preventing the silent bugs documented in the HLSL gotchas above.

**Defined in**: `.claude/skills/new-shader/SKILL.md`

**Usage**: `/new-shader bloom` or `/new-shader film grain with intensity and speed controls`

Claude will write `shaders/<name>.hlsl` with ISF INPUTS tailored to the description, correct cbuffer structure, and no intrinsic name shadowing. After creation, use Shader Library → "Scan Folder" to load it.

### Subagent: shader-reviewer

Specialist reviewer for HLSL shaders. Checks cbuffer layout compliance, ISF packing offset correctness, intrinsic name shadowing, entry point signature, and UV sampling patterns.

**Defined in**: `.claude/agents/shader-reviewer.md`

**Usage**: After writing or modifying any `.hlsl` file, ask Claude: "use the shader-reviewer agent to check shaders/bloom.hlsl". The agent walks through every check and reports violations with line numbers and fixes.

### Hooks

Defined in `.claude/settings.json`:

**PreToolUse — block config.json edits**: Intercepts Write/Edit calls targeting `config.json` and exits with an error message. `config.json` is written by ShaderPlayer at runtime; source-of-truth for shader presets is the `.hlsl` files and the in-app library.

**PostToolUse — HLSL syntax validation**: After any Write/Edit to a `.hlsl` file, runs `fxc.exe /T ps_5_0 /E main` against the file using the Windows SDK compiler at `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe`. Compile errors appear immediately in the tool output without requiring a full CMake build cycle.
