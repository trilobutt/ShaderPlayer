# ShaderPlayer

Real-time HLSL shader video player for Windows. Decodes video with FFmpeg, runs every frame through a Direct3D 11 pixel shader pipeline, and lets you write, edit, and hot-swap shaders without interrupting playback or recording.

---

## Features

### Video Playback
- Plays any format FFmpeg supports (MP4, MOV, MKV, AVI, ProRes, and more)
- Looping playback with scrubber and frame-accurate seeking
- Drag-and-drop a video file onto the window to open it
- Live capture from webcams (DirectShow) and network streams (RTSP, RTMP, HTTP)
- Mute toggle and volume control
- Time display switchable between seconds and frame numbers

### Shader Pipeline
- Every video frame passes through a user-selected HLSL pixel shader (Shader Model 5.0)
- Passthrough mode available (no effect)
- Hot-swap shaders mid-playback with no stutter
- Each frame exposes: `time`, `resolution`, `videoResolution`, and 16 floats of user-defined custom parameters

### Shader Editor
- Built-in syntax-highlighted HLSL editor (ImGuiColorTextEdit)
- F5 or the Compile button recompiles and applies instantly
- Auto-compile on source change after a 500 ms idle delay
- Drag-and-drop a `.hlsl` file onto the window to load it into the editor

### Shader Library
- Panel listing all loaded presets, grouped into three sections: Audio Reactive, Generative, Video Effects
- Scan Folder to bulk-load `.hlsl`/`.fx`/`.ps` files from any directory (persists across sessions)
- Right-click any preset to assign a keyboard shortcut for instant switching
- "+ New" modal to create a named preset from scratch

### Shader Parameters (ISF System)
Shaders declare parameters in a JSON block comment at the top of the file. ShaderPlayer parses the block, generates HLSL `#define` aliases, and renders UI controls automatically — no manual cbuffer indexing.

Supported parameter types:

| Type | UI Control | HLSL alias |
|---|---|---|
| `float` | Slider | single float |
| `bool` | Checkbox | `(custom[n].x > 0.5)` |
| `long` | Dropdown | `int(custom[n].x)` |
| `color` | RGBA colour picker | `float4` |
| `point2d` | Draggable 2D pad | `float2` |
| `event` | Button (fires for one frame) | single float |
| `audio` | Read-only band meter | audio uniform |

Parameter values are saved in `config.json` and restored on next launch.

### Keyframe Animation
- Per-parameter keyframe timeline tied to video playback time
- Three interpolation modes: Linear, Ease In/Out (smoothstep), Cubic Bezier
- Inline bezier curve editor with draggable control points
- Timestamp chips in the editor panel — click to seek to that keyframe
- Diamond markers on the transport scrubber for the selected parameter
- Keyframes are saved and restored via `config.json`

### Audio Reactivity
- Video audio is decoded and analysed every frame (no audio playback to speakers)
- DSP: 2048-sample ring buffer, Hann window, KissFFT, beat detection, spectral centroid
- Shaders receive: `audioRms`, `audioBass`, `audioMid`, `audioHigh`, `audioBeat`, `audioSpectralCentroid`, plus a 256-bin spectrum texture at `t3`
- Declare an `audio` parameter with a `BAND` field to expose the value as a named uniform
- Audio-reactive shaders are tagged `SHADER_TYPE: "audio"` and listed separately in the library

### Generative Mode
- Shaders tagged `SHADER_TYPE: "generative"` run without any video input
- Wall-clock timer drives the `time` uniform
- Transport shows a running elapsed counter

### Noise Texture
- CPU-generated noise texture bound at `t1`/`s1` every frame (WRAP sampler)
- Red channel: Perlin gradient noise. Green channel: Voronoi F1 (inverted, bright at cell centres)
- Scale and texture size configurable via View > Noise Generator
- Settings persisted in `config.json`

### Video Output Window
- Second resizable Win32 window (F7) mirroring the processed output
- Shares the same D3D11 device — no cross-adapter copy
- Useful for sending clean output to a second monitor or projector

### Spout2 Output
- Sends processed frames as a Spout2 sender (F8 to toggle)
- Compatible with Resolume, MadMapper, TouchDesigner, and any Spout-capable application
- Opt-in; disabled by default

### Recording
- Records the shader-processed output to H.264 or H.265 via FFmpeg
- Optional audio track (AAC 128 kbps mono, sourced from the input video)
- One frame captured per decoded video frame — framerate matches source
- F9 or the Record button toggles recording; output path and codec configurable in the Recording panel

### Workspace Presets
- Save and restore the full ImGui panel layout as `.ini` files in the `layouts/` directory
- Assign keyboard shortcuts to workspace presets for instant recall
- Access via View > Workspace Presets

### Keyboard Shortcuts

| Key | Action |
|---|---|
| Space | Play / Pause |
| Escape | Stop (seek to start) |
| F1 | Toggle Shader Editor panel |
| F2 | Toggle Shader Library panel |
| F3 | Toggle Transport panel |
| F4 | Toggle Recording panel |
| F5 | Compile current shader |
| F6 | Toggle Keybindings panel |
| F7 | Toggle Video Output Window |
| F8 | Toggle Spout Output |
| F9 | Start / Stop recording |
| Ctrl+O | Open video file |
| Ctrl+N | New shader |
| Ctrl+S | Save current shader |

User-defined shortcuts for shader presets and workspace presets are set via right-click menus.

---

## Included Shaders

### Video Effects
| Shader | Effect |
|---|---|
| `passthrough` | No effect — direct output |
| `grayscale` | Luminance-based desaturation |
| `chromatic_aberration` | RGB channel offset |
| `vignette` | Radial darkening |
| `sharpen` | Convolution sharpening |
| `false_colour` | Luminance false colour mapping |
| `pixelate` | Pixel grid with optional overlay |
| `halftone` | Halftone dot pattern |
| `led_panel` | Stylised LED panel grid |
| `lego_bricks` | Lego brick effect |
| `crochet` | Crochet stitch pattern |
| `fluted_glass` | Fluted glass distortion |
| `depixelation` | Smooth depixelation blur |
| `pixel_sdf` | SDF-based pixel shapes |
| `pixel_matrix` | Matrix-style pixel pattern |
| `receipt_bars` | Receipt/barcode aesthetic |
| `ascii_noise` | ASCII noise overlay |
| `zebra` | Zebra-stripe exposure assist |
| `focus_peaking` | Edge-highlight focus peaking |
| `safe_areas` | Broadcast safe area overlays |
| `waveform` | Luma waveform monitor |
| `rgb_parade` | RGB parade scope |
| `vectorscope` | Colour vectorscope |

### Audio Reactive
| Shader | Effect |
|---|---|
| `audio_spectrum` | Spectrum bar visualiser with beat flash |
| `audio_bass_pulse` | Bass-reactive chromatic aberration |
| `audio_waveform` | Spectrum waveform overlay |

### Generative
| Shader | Effect |
|---|---|
| `plasma` | Classic plasma animation |

---

## Build

### Requirements

- Windows 11 (Windows 10 may work but is untested)
- Visual Studio 2022 with C++20 support, or MinGW-w64
- CMake 3.20 or later
- FFmpeg runtime DLLs (see below)

### FFmpeg Setup

Headers and import libs are bundled in `third_party/ffmpeg/` (committed, ~3 MB). The runtime DLLs (~220 MB) are gitignored and must be provided locally.

1. Download a shared FFmpeg build from [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) — the `ffmpeg-release-full-shared.7z` archive.
2. Copy the DLLs into `third_party/ffmpeg/bin/`:
   ```
   xcopy "C:\path\to\ffmpeg\bin\*.dll" "third_party\ffmpeg\bin\"
   ```
3. CMake autodiscovers `third_party/ffmpeg/` — no flags needed.

Alternatively, point CMake at a system FFmpeg install:
```
cmake -B build -DFFMPEG_ROOT=C:\path\to\ffmpeg
```

### Compile

```bash
cmake -B build
cmake --build build --config Release
```

Output: `build/Release/ShaderPlayer.exe` with FFmpeg DLLs copied alongside it.

### Running

Run from the project root so `shaders/` resolves correctly:
```
build\Release\ShaderPlayer.exe
```
Or use Shader Library > Scan Folder to point at the shaders directory manually. The path persists in `config.json`.

---

## Writing Shaders

All shaders must include this cbuffer structure:

```hlsl
Texture2D videoTexture : register(t0);
SamplerState videoSampler : register(s0);

Texture2D noiseTexture : register(t1);
SamplerState noiseSampler : register(s1);

cbuffer Constants : register(b0) {
    float time;
    float padding1;
    float2 resolution;       // output resolution in pixels
    float2 videoResolution;  // source video resolution in pixels
    float2 padding2;
    float4 custom[4];        // 16 floats of user parameters
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    return videoTexture.Sample(videoSampler, input.uv);
}
```

`noiseTexture`/`noiseSampler` must be declared even if unused.

### Adding Parameters

Declare an ISF JSON block before any non-comment HLSL. ShaderPlayer parses it and generates `#define` aliases mapping readable names to `custom[]` slots.

```hlsl
/*{
    "INPUTS": [
        { "NAME": "Strength", "LABEL": "Effect Strength", "TYPE": "float",
          "MIN": 0.0, "MAX": 1.0, "STEP": 0.01, "DEFAULT": 0.5 },
        { "NAME": "Tint", "LABEL": "Colour Tint", "TYPE": "color",
          "DEFAULT": [1.0, 1.0, 1.0, 1.0] }
    ]
}*/
```

Use `Strength` and `Tint` directly in the shader body — no `custom[]` indexing needed.

### Shader Types

Add `"SHADER_TYPE"` to the ISF block to control how ShaderPlayer categorises and handles the shader:

| Value | Behaviour |
|---|---|
| *(absent)* | Video effect — video texture is the primary input |
| `"generative"` | No video required — `time` drives animation |
| `"audio"` | Audio reactive — audio uniforms auto-injected |

### Audio Reactive Shaders

Audio shaders receive additional uniforms automatically:

```hlsl
cbuffer AudioConstants : register(b1) {
    float audioRms;
    float audioBass;
    float audioMid;
    float audioHigh;
    float audioBeat;
    float audioSpectralCentroid;
    float _pad[2];
};
Texture2D spectrumTexture : register(t3);  // 1x256, R32_FLOAT
```

Declare an `audio`-type parameter to expose a band as a named alias:

```json
{ "NAME": "Bass", "TYPE": "audio", "BAND": "bass" }
```

Valid `BAND` values: `rms`, `bass`, `mid`, `high`, `beat`, `centroid`.

### Parameter Types Reference

| Type | UI | Floats used | Alignment |
|---|---|---|---|
| `float` | Slider | 1 | none |
| `bool` | Checkbox | 1 | none |
| `long` | Dropdown | 1 | none |
| `event` | Button (one frame) | 1 | none |
| `point2d` | 2D drag pad | 2 | even offset |
| `color` | RGBA picker | 4 | multiple of 4 |
| `audio` | Read-only meter | 0 | n/a |

Maximum 16 floats total across all non-audio parameters.

---

## Configuration

`config.json` is written next to the executable and created on first run. It stores:

- Last-used shader directory
- Loaded shader presets and their shortcut keys
- Parameter values for each preset
- Keyframe timelines
- Recording settings
- Noise generator settings
- Audio settings
- Volume

Do not edit `config.json` by hand — it is overwritten at runtime.

Workspace layout presets are stored as `.ini` files in the `layouts/` directory next to the executable and are not referenced by `config.json`.

---

## Known Limitations

- Windows only (Direct3D 11)
- No audio playback to speakers — audio is decoded for shader analysis only
- Recording framerate matches source framerate (no arbitrary output rate)
- ProRes support depends on the FFmpeg build used

---

## Dependencies

| Library | Purpose |
|---|---|
| [FFmpeg](https://ffmpeg.org/) | Video decoding and encoding |
| [Dear ImGui](https://github.com/ocornut/imgui) | UI framework (docking branch) |
| [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) | Syntax-highlighted HLSL editor |
| [nlohmann/json](https://github.com/nlohmann/json) | Config file serialisation |
| [KissFFT](https://github.com/mborgerding/kissfft) | Audio spectrum analysis |
| [Spout2](https://github.com/leadedge/Spout2) | GPU texture sharing output |
| Direct3D 11 / HLSL | Shader pipeline and rendering |

All dependencies except FFmpeg runtime DLLs are fetched automatically via CMake FetchContent.
