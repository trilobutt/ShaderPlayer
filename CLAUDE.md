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
- **UI Framework**: ImGui with Win32 backend
- **Code Editor**: ImGuiColorTextEdit
- **JSON**: nlohmann/json

### Component Structure

```
src/
├── main.cpp              - Entry point, Windows-specific initialisation
├── Application.{cpp,h}   - Main application loop and window management
├── VideoDecoder.{cpp,h}  - FFmpeg video decoding (H.264, ProRes)
├── VideoEncoder.{cpp,h}  - FFmpeg video encoding for recording
├── D3D11Renderer.{cpp,h} - Direct3D 11 rendering pipeline
├── ShaderManager.{cpp,h} - HLSL shader compilation and management
├── UIManager.{cpp,h}     - ImGui interface and shader editor
├── ConfigManager.{cpp,h} - JSON config loading/saving
└── Common.h              - Shared types and utilities
```

### Shader System

All shaders follow a standard interface:

```hlsl
Texture2D videoTexture : register(t0);
SamplerState samplerState : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target {
    // Shader implementation
}
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

## Configuration

Configuration is stored in `config.json` (created on first run if missing).

### Keybindings

```json
{
  "keybindings": {
    "1": "passthrough",
    "2": "grayscale",
    "3": "vignette",
    "4": "chromatic_aberration",
    "5": "sharpen",
    "6": "false_colour"
  }
}
```

Keybindings use virtual key codes or character literals.

### Shader Presets

```json
{
  "shaders": [
    {"name": "passthrough", "path": "shaders/passthrough.hlsl"},
    {"name": "grayscale", "path": "shaders/grayscale.hlsl"}
  ]
}
```

## Usage

1. Launch ShaderPlayer.exe
2. Load a video file (H.264 or ProRes supported)
3. Shaders are automatically loaded from the `shaders/` directory
4. Use number keys (1-6) to switch between loaded shaders
5. Use the integrated editor to modify shaders in real-time
6. Click "Record" to begin capturing output with current shader
7. Switch shaders during recording without interruption

## Development Notes

### Direct3D Pipeline

The rendering pipeline uses a full-screen quad with the video frame as a texture input to the pixel shader. Each shader receives:
- `videoTexture` (t0): Current video frame as R8G8B8A8_UNORM
- `samplerState` (s0): Linear filtering sampler

### Performance Considerations

- Shader compilation happens on load and when edited
- Video decoding runs on a separate thread
- Recording uses hardware-accelerated encoding where available (NVENC/QuickSync)
- Frame presentation is vsync-locked by default

### Extending

To add a new shader:
1. Create `shaders/your_shader.hlsl` following the standard interface
2. Add entry to `config.json` shaders array
3. Assign a keybinding in the keybindings section
4. Shader will be automatically compiled on next launch

## Known Limitations

- Windows-only (Direct3D 11 requirement)
- No audio playback currently implemented
- ProRes support depends on FFmpeg build configuration
- Recording framerate matches playback framerate (no arbitrary output rates)

## Future Enhancements

- Compute shader support for more complex effects
- Multi-pass shader pipeline
- Audio playback and passthrough to recording
- Export/import shader preset bundles
- Timeline markers for shader switches
- Constant buffer support for shader parameters

## Licence

[To be determined]

## Dependencies and Licences

- **ImGui**: MIT Licence
- **ImGuiColorTextEdit**: MIT Licence
- **nlohmann/json**: MIT Licence
- **FFmpeg**: LGPL 2.1+ (or GPL 2+ depending on build configuration)

## Contact

Project created for ABL Films post-production workflows.
