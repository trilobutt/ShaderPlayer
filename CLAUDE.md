# ShaderPlayer

## Project Overview

ShaderPlayer is a real-time HLSL shader video player for Windows 11. It allows users to apply custom HLSL pixel shaders to video playback with live switching between effects and real-time recording capabilities.

### Core Features

- **Video Playback**: H.264 and ProRes codec support via FFmpeg
- **HLSL Shader Pipeline**: Real-time shader effects using Direct3D 11
- **Live Shader Editor**: Integrated code editor with syntax highlighting and live preview
- **Hot-Swappable Filters**: Preload multiple shaders and switch between them with customisable keyboard shortcuts
- **Live Recording**: Capture shader-processed output with uninterrupted recording during shader switches
- **Configuration System**: JSON-based config for shader presets and keybindings

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
│                           AppConfig (shaderDirectory default = "shaders"), PlaybackState
├── Application.{cpp,h}   - Central coordinator. Owns all other components. Drives
│                           ProcessFrame() (video decode) + RenderFrame() (D3D + ImGui)
│                           each tick. Handles WndProc, drag-drop (.hlsl → shader,
│                           other → video), keyboard shortcuts, file dialogs including
│                           ScanFolderDialog() (IFileOpenDialog + FOS_PICKFOLDERS).
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
└── ConfigManager.{cpp,h} - Load/Save config.json next to the executable
                            (GetDefaultConfigPath uses GetModuleFileNameA). Serialises
                            AppConfig including shaderPresets (filepath + shortcutKey)
                            and shaderDirectory.
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
- `passthrough.hlsl` - Direct video pass-through (no effect)
- `grayscale.hlsl` - Luminance-based desaturation
- `vignette.hlsl` - Radial darkening
- `chromatic_aberration.hlsl` - RGB channel offset
- `sharpen.hlsl` - Convolution-based sharpening
- `false_colour.hlsl` - Luminance-based false colour mapping

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
cmake --build build --config Release
```

The executable and required DLLs will be in `build/Release/`. FFmpeg DLLs are copied there automatically at post-build.

**Run from the project root** (not from `build/Release/`) so the relative `shaders/` path resolves correctly, or use the Shader Library → "Scan Folder" button to point at the shaders directory manually. A fallback also looks for `shaders/` next to the executable at startup.

## Configuration

Configuration is stored in `config.json` next to the executable (created on first run if missing).

### Keybindings

Shader shortcut keys are stored per-preset as virtual key codes (`shortcutKey`, `shortcutModifiers`). Set them via right-click → "Set Keybinding..." in the Shader Library.

### Shader Presets

Presets saved to config include only the `filepath` and shortcut — source is re-read from disk on load. shaderDirectory is also saved so "Scan Folder" persists across sessions.

## Usage

1. Launch ShaderPlayer.exe
2. Load a video file via File → Open Video or drag-and-drop
3. Open Shader Library (F2) → "Scan Folder" to point at the `shaders/` directory
4. Click a shader in the library to activate it; source loads into the editor
5. Edit in the Shader Editor (F1); auto-compiles after 500 ms, or press Compile (F5)
6. Click "Start Recording" (F9) to capture shader-processed output
7. Switch shaders during recording without interruption

Drag-dropping a `.hlsl`/`.fx`/`.ps` file loads it as a shader; any other file is opened as video.

## Development Notes

### Render Loop (RenderFrame)

Each frame:
1. `UploadVideoFrame()` — maps video texture and DMA-copies current VideoFrame (RGBA8)
2. `BeginFrame()` — updates cbuffer, clears backbuffer, sets **entire** PS pipeline state including `m_activePS`
3. `RenderToDisplay()` — changes RT to `m_displayTexture`, calls `Draw(3,0)`, restores backbuffer RT
4. ImGui render pass — `ImGui::Image(GetDisplaySRV(), ...)` composites the processed frame
5. `Present(vsync=true)`

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

### Extending

To add a new shader:
1. Create `shaders/your_shader.hlsl` following the standard cbuffer interface above
2. Use Shader Library → "Scan Folder" to reload, or restart the app

## Known Limitations

- Windows-only (Direct3D 11 requirement)
- No audio playback currently implemented
- ProRes support depends on FFmpeg build configuration
- Recording framerate matches playback framerate (no arbitrary output rates)

## Licence

[To be determined]

## Dependencies and Licences

- **ImGui**: MIT Licence (docking branch)
- **ImGuiColorTextEdit**: MIT Licence
- **nlohmann/json**: MIT Licence
- **FFmpeg**: LGPL 2.1+ (or GPL 2+ depending on build configuration)

## Contact

Project created for ABL Films post-production workflows.
