# ShaderPlayer

Real-time HLSL shader video player for Windows. Decodes video with FFmpeg, runs every frame through a Direct3D 11 pixel shader pipeline, and lets you write, edit, and hot-swap shaders without interrupting playback or recording.

---

## Features

### Video Playback
- Plays any format FFmpeg supports (MP4, MOV, MKV, AVI, ProRes, and more)
- Looping playback with scrubber and frame-accurate seeking
- Drag-and-drop a video file onto the window to open it
- Live capture from webcams (DirectShow) and network streams (RTSP, RTMP, HTTP)
- Audio playback via WASAPI (miniaudio); volume and mute controls in the transport bar
- Time display switchable between seconds and frame numbers

### Shader Pipeline
- Every video frame passes through a user-selected HLSL pixel shader (Shader Model 5.0)
- Passthrough mode available (no effect)
- Hot-swap shaders mid-playback with no stutter
- Each frame exposes: `time`, `resolution`, `videoResolution`, a global noise texture, audio data, and 16 floats of user-defined custom parameters

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

Parameter values are saved in `config.json` and restored on next launch. See `docs/shader-parameter-guide.md` for the full reference.

### Keyframe Animation
- Per-parameter keyframe timeline tied to video playback time
- Three interpolation modes: Linear, Ease In/Out (smoothstep), Cubic Bezier
- Inline bezier curve editor with draggable control points
- Timestamp chips in the editor panel — click to seek to that keyframe
- Diamond markers on the transport scrubber for the selected parameter
- Keyframes are saved and restored via `config.json`

### Audio Reactivity
- Video audio is decoded and analysed every frame via KissFFT
- DSP: 2048-sample ring buffer, Hann window, beat detection, spectral centroid
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

### Audio Reactive

| Shader | Effect |
|---|---|
| `acoustic_geology` | Deposits virtual sedimentary layers from audio; low frequencies produce thick warm strata, highs create fine detail |
| `audio_bass_pulse` | Bass-reactive chromatic aberration with beat flash on video |
| `audio_fluid_vortex` | Audio-driven incompressible fluid vortex rendered via curl noise |
| `audio_spectrum` | Spectrum bar visualiser with beat flash |
| `audio_waveform` | Spectrum waveform overlay on video |
| `fourier_crystal_growth` | Audio-driven N-fold crystallographic growth patterns |
| `frequency_chromesthesia` | Pitch-colour synesthesia — each FFT bin mapped to hue via Scriabin/Newton/Rimington tables |
| `psychoacoustic_topography` | FFT spectrum rendered as a 3D terrain surface |
| `radial_burst` | FFT bins mapped to polar coordinates with N-fold rotational symmetry |
| `synaptic_fire_network` | Audio-driven synaptic fire network visualiser |

### Generative

| Shader | Effect |
|---|---|
| `arthropod_cuticle` | Meinhardt-Gierer activator-inhibitor Turing instability — insect cuticle patterning |
| `diffusion_limited_aggregation` | Procedural DLA fractal growth approximation |
| `electromagnetic_field` | N point charges in a rotating ring — E-field and potential visualisation |
| `game_of_life` | Conway Game of Life (B3/S23) with chromatic cell age |
| `hele_shaw_fingering` | Hele-Shaw Saffman-Taylor viscous fingering (Laplacian growth) |
| `hyperbolic_tiling` | Conformal Poincaré disc tiling of any valid {p, q} Schläfli symbol |
| `julia_set` | Julia set fractal with animatable C parameter |
| `mandelbulb` | Mandelbulb 3D fractal via ray-marched distance estimator |
| `newton_fractal` | Newton fractal for polynomials of degree 2–7 |
| `perlin_flow_field` | Perlin curl flow field LIC approximation |
| `physarum_slime_mould` | Physarum polycephalum transport network procedural approximation |
| `plasma` | Classic plasma animation |
| `reaction_diffusion` | Turing instability via multi-scale Difference-of-Gaussians on Perlin noise |
| `strange_attractor` | Chaotic ODE integration rendered as per-pixel density accumulator |
| `voronoi_cells` | Animated Voronoi diagram with three render modes |

### Video Effects

| Shader | Effect |
|---|---|
| `chromatic_aberration` | RGB channel offset simulating lens dispersion |
| `colour_grading` | Lift/gamma/gain colour grading |
| `crt_simulation` | CRT emulation: barrel distortion, phosphor mask, scanlines, bloom |
| `datamosh_drift` | Datamosh block drift using Perlin noise as proxy motion vectors |
| `domain_warp` | Recursive domain-warped noise distortion |
| `false_colour` | Luminance-based false colour mapping |
| `focus_peaking` | Sobel edge-detection overlay highlighting sharp regions |
| `fourier_sculpting` | Frequency-domain sculpting — low-pass, high-pass, band-pass, directional, annular, notch |
| `grayscale` | Luminance-based desaturation |
| `holographic_diffraction` | Physically grounded aperture diffraction starburst on bright regions |
| `kaleidoscope` | N-segment radial kaleidoscope mirror |
| `non_euclidean_lens` | Spherical and hyperbolic spacetime lens distortion |
| `oil_paint_filter` | Generalised Kuwahara structure-tensor oil-paint filter |
| `pixel_sort` | Threshold-based pixel sort approximation |
| `rgb_parade` | RGB parade scope overlay |
| `safe_areas` | Broadcast safe area guides (action/title safe) |
| `sharpen` | Convolution-based sharpening |
| `slit_scan` | Slit-scan temporal splice approximation |
| `thermal_false_colour` | Luminance-to-temperature false colour (inferno, ironbow, rainbow, greyscale palettes) |
| `vectorscope` | Vectorscope display overlay |
| `vignette` | Radial darkening |
| `waveform` | Luma waveform monitor overlay |
| `zebra` | Zebra-stripe overexposure indicator |

---

## Build

### Requirements

- Windows 11 (Windows 10 may work but is untested)
- Visual Studio 2022 with C++20 support
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

**Build via Visual Studio IDE, not the command line.** The CMake generator uses Ninja + MSVC; running `cmake --build` from a plain shell fails at link with `memcpy` unresolved because the MSVC CRT environment is not initialised outside the Visual Studio developer prompt.

```
cmake -B build
# Then open the project in Visual Studio and build from there.
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

`noiseTexture`/`noiseSampler` must be declared even if unused — they are always bound by the renderer.

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

Use `Strength` and `Tint` directly in the shader body — no `custom[]` indexing needed. See `docs/shader-parameter-guide.md` for all parameter types, packing rules, and gotchas.

### Shader Types

Add `"SHADER_TYPE"` to the ISF block to control how ShaderPlayer categorises and handles the shader:

| Value | Behaviour |
|---|---|
| *(absent)* or `"video"` | Video effect — video texture is the primary input |
| `"generative"` | No video required — `time` drives animation |
| `"audio"` | Audio reactive — audio uniforms auto-injected |

### Audio Reactive Shaders

Audio shaders receive additional uniforms automatically (injected by ShaderPlayer before compilation):

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

### New Shader Scaffolding

The `/new-shader` Claude Code skill scaffolds a new `.hlsl` file with the correct cbuffer layout and ISF block:

```
/new-shader bloom
/new-shader film grain with intensity and speed controls
```

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
| [miniaudio](https://miniaud.io/) | Audio playback (WASAPI) |
| [Spout2](https://github.com/leadedge/Spout2) | GPU texture sharing output |
| Direct3D 11 / HLSL | Shader pipeline and rendering |

All dependencies except FFmpeg runtime DLLs are fetched automatically via CMake FetchContent.
