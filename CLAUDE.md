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
- `pixelate.hlsl` - Pixelation with optional grid overlay; demonstrates all ISF widget types

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

### Adding a New Shader

Use the `/new-shader <name>` skill — it scaffolds the file with correct cbuffer layout and ISF block. Then Shader Library → "Scan Folder" to load it.

## Known Limitations

- Windows-only (Direct3D 11 requirement)
- No audio playback currently implemented
- ProRes support depends on FFmpeg build configuration
- Recording framerate matches playback framerate (no arbitrary output rates)

## ImGui Notes

- `ImGuiKey` is not 1:1 with Win32 VK codes (broken since ImGui 1.87) — use `GetKeyState(VK_*)` for key state in modal/input code
- `ImGui::SameLine(x)` takes absolute offset from window left — use `GetContentRegionMax().x` for right-alignment, not `GetContentRegionAvail().x`
- Static locals in modal draw functions persist across sessions; use an `s_wasOpen` bool sentinel to reset edge-detection state when a modal reopens

## ShaderManager API

- `GetPreset(int)` is non-const; use `GetPresets()` (returns `const std::vector<ShaderPreset>&`) when calling from a `const` method
- `SetActivePreset` is called in **both** `Application.cpp` and `UIManager.cpp` — after any new call-site, always add `OnParamChanged()` (Application) or `m_app.OnParamChanged()` (UIManager)

## C++ / Dependency Gotchas

- `nlohmann/json.hpp` is ~25,000 lines — include it only in `.cpp` files, never in headers
- nlohmann/json `try/catch` must wrap the full processing loop, not just `json::parse` — `get<>()` and `value()` throw `type_error` on type mismatches
- HLSL intrinsic shadowing: never name local variables after HLSL built-ins (`frac`, `min`, `max`, `abs`, `lerp`, etc.)

## Shader Parameter System

See `docs/shader-parameter-guide.md` for the author-facing reference. Technical notes for development:

### ISF JSON Block Parsing

`ShaderManager::ParseISFParams(const std::string& source)` extracts parameter metadata:
1. Find first `/*{` in source; extract up to matching `}*/`.
2. Parse body with nlohmann/json; silent failure returns empty vector (no crash, no compile failure).
3. Iterate `"INPUTS"` array; construct `ShaderParam` per entry with `cbufferOffset` assigned sequentially.
4. Called from `CompilePreset` and `RecompilePreset` before `D3DCompile` — updates `preset.params`.

The ISF block is **not stripped** — HLSL ignores block comments. No source modification required.

### #define Alias Generation

After parsing, a `#define` preamble is prepended to the source passed to `D3DCompile`. Mapping:
- float offset `N` → array index `N/4`, component `"xyzw"[N%4]`
- `float`/`event`: `#define Name custom[idx].comp`
- `bool`: `#define Name (custom[idx].comp > 0.5)`
- `long`: `#define Name int(custom[idx].comp)`
- `point2d` (2 floats, even-aligned): `#define Name float2(custom[idx].ab, custom[idx].cd)`
- `color` (4 floats, 4-aligned): `#define Name custom[idx]`

The original source on disk is never modified.

### Cbuffer Packing Rules

Parameters packed into `custom[16]` (= `float4 custom[4]`) sequentially:
- `float`, `bool`, `long`, `event`: 1 float, no alignment
- `point2d`: 2 floats, aligned to next even offset
- `color`: 4 floats, aligned to next multiple-of-4 offset

Parameters exceeding 16 floats total are skipped with a warning appended to `ShaderPreset::compileError`.

### Value Storage and GPU Upload

- `ShaderParam::values[4]` holds current values; `defaultValues[4]` holds parsed defaults.
- On any widget change: `Application::OnParamChanged()` packs all `params` into a `float[16]` scratch buffer at their `cbufferOffset`s, then calls `D3D11Renderer::SetCustomUniforms`. Effect visible on next `BeginFrame`.
- `event` type: set `values[0] = 1.0f` on button press; a one-frame flag in `Application` zeros it after the next `RenderFrame` submission.
- No per-frame CPU cost — `SetCustomUniforms` called only on user interaction and on shader activation.

### Persistence

`ConfigManager` serialises only `ShaderParam::values` (not metadata) to `config.json`. On load, parsed params matched by `name` to restore saved values; unmatched params use `defaultValues`.

## Claude Code Automations

All automations live under `.claude/`. Do not edit `config.json` directly — it is runtime-generated by ShaderPlayer and blocked by a PreToolUse hook.

### MCP Server: context7

Live documentation lookup for D3D11, HLSL, FFmpeg, and ImGui APIs.

**Installed**: `claude mcp add context7 -- npx -y @upstash/context7-mcp` (stored in `.claude.json`)

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
