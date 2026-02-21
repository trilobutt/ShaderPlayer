# Shader Parameters Design

Date: 2026-02-21

## Overview

Add a dynamic shader parameter system to ShaderPlayer. Shader authors annotate uniforms via an ISF-style JSON block comment at the top of the `.hlsl` file. ShaderPlayer parses this metadata, generates `#define` aliases so the shader body uses readable names, and renders ImGui controls embedded in the Shader Editor panel. Parameter values are stored in `ShaderPreset`, persist across sessions, and feed the existing `custom[16]` cbuffer slot with zero per-frame CPU overhead.

---

## Data Model

### New `ShaderParam` struct (`Common.h`)

```cpp
enum class ShaderParamType { Float, Bool, Long, Color, Point2D, Event };

struct ShaderParam {
    std::string name;             // HLSL identifier — must match #define alias
    std::string label;            // ImGui display label
    ShaderParamType type;
    float values[4] = {};         // Current values: float→[0], bool→[0], long→[0],
                                  //   color→RGBA, point2d→XY
    float defaultValues[4] = {};  // Restored on "Reset to defaults"
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.01f;
    std::vector<std::string> longLabels; // Dropdown labels for type=Long
    int cbufferOffset = 0;        // Float index into custom[16]; set at parse time
};
```

### `ShaderPreset` additions (`Common.h`)

```cpp
std::vector<ShaderParam> params;  // Empty if shader has no ISF block
```

---

## ISF JSON Format

The shader file optionally begins with a `/*{ }*/` block comment containing a JSON object. The block must appear before any non-comment HLSL. The HLSL compiler ignores it; ShaderPlayer's parser extracts it before calling `D3DCompile`.

```hlsl
/*{
    "INPUTS": [
        {"NAME": "PixelSize", "LABEL": "Pixel Size",   "TYPE": "float",
         "MIN": 1.0, "MAX": 64.0, "STEP": 1.0, "DEFAULT": 8.0},

        {"NAME": "Tint",      "LABEL": "Tint Colour",  "TYPE": "color",
         "DEFAULT": [1.0, 0.8, 0.6, 1.0]},

        {"NAME": "Center",    "LABEL": "Center",        "TYPE": "point2d",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": [0.5, 0.5]},

        {"NAME": "Enable",    "LABEL": "Enable FX",     "TYPE": "bool",
         "DEFAULT": true},

        {"NAME": "Mode",      "LABEL": "Blend Mode",    "TYPE": "long",
         "VALUES": ["Normal", "Hard Light", "Overlay"], "DEFAULT": 0},

        {"NAME": "Flash",     "LABEL": "Flash",         "TYPE": "event"}
    ]
}*/

Texture2D videoTexture : register(t0);
// ...
```

### Field reference

| Field | Required | Notes |
|---|---|---|
| `NAME` | Yes | Must be a valid HLSL identifier; used for `#define` alias |
| `TYPE` | Yes | `"float"`, `"bool"`, `"long"`, `"color"`, `"point2d"`, `"event"` |
| `LABEL` | No | Defaults to `NAME` if absent |
| `MIN` / `MAX` | No | Defaults: 0.0 / 1.0; ignored for `bool`, `long`, `event` |
| `STEP` | No | Default 0.01; slider granularity for `float` |
| `DEFAULT` | No | Scalar or array; defaults to 0/false |
| `VALUES` | Yes (long) | Array of strings for dropdown labels |

---

## Cbuffer Packing and `#define` Alias Generation

Parameters are packed sequentially into `custom[16]` (16 floats total, stored as `float4 custom[4]` in the cbuffer). Natural alignment rules apply:

- `point2d` aligns to the next even float offset
- `color` aligns to the next multiple-of-4 float offset
- All other types consume 1 float with no alignment padding

If declared parameters exceed 16 floats in total, excess parameters are silently skipped; their names are appended to `ShaderPreset::compileError` as a warning.

### Alias generation

After parsing, `ShaderManager` prepends `#define` directives to the source before `D3DCompile`:

```hlsl
// float at offset 0  →  custom[0].x
#define PixelSize custom[0].x

// color at offset 4  →  custom[1]  (full float4, must be 4-aligned)
#define Tint custom[1]

// point2d at offset 2  →  float2(custom[0].z, custom[0].w)
#define Center float2(custom[0].z, custom[0].w)

// bool at offset 1  →  compare to 0.5
#define Enable (custom[0].y > 0.5)

// long at offset 8  →  cast to int
#define Mode int(custom[2].x)

// event at offset 9  →  raw float (1.0 for one frame on trigger)
#define Flash custom[2].y
```

Offset-to-component mapping: for float index `N`, array index = `N / 4`, component = `"xyzw"[N % 4]`.

---

## Parse Implementation

`ShaderManager` gains a static method:

```cpp
static std::vector<ShaderParam> ParseISFParams(const std::string& source);
```

Algorithm:
1. Search for `/*{` in the source; if absent, return empty vector.
2. Extract text up to the matching `}*/`.
3. Parse with `nlohmann::json`; catch parse errors and return empty (no hard failure).
4. Iterate `"INPUTS"` array; construct `ShaderParam` per entry.
5. Assign `cbufferOffset` sequentially with alignment rules above.
6. Return populated vector.

Called from `CompilePreset` and `RecompilePreset` before invoking `D3DCompile`. The generated `#define` block is prepended to the source string passed to the compiler — the file on disk is never modified.

---

## UI (Shader Editor Panel)

A collapsible "Parameters" section is appended below the editor text area when `activePreset.params` is non-empty.

| Type | Widget |
|---|---|
| `float` | `ImGui::SliderFloat` with min/max; step enforced by rounding after drag |
| `bool` | `ImGui::Checkbox` |
| `long` | `ImGui::Combo` (dropdown) |
| `color` | `ImGui::ColorEdit4` (RGBA) |
| `point2d` | Square `ImGui::InvisibleButton` pad; click/drag maps to [min,max] in X and Y |
| `event` | `ImGui::Button`; press sets `values[0] = 1.0f`; zeroed after one frame |

A "Reset to defaults" button at the bottom of the section restores all `values` from `defaultValues`.

Every widget change immediately calls `Application::OnParamChanged()`, which packs `params` into a `float[16]` scratch buffer and calls `D3D11Renderer::SetCustomUniforms`. Effect is visible on the next `BeginFrame`.

---

## Data Flow

```
User moves slider / clicks widget
  → ShaderParam::values[] updated in UIManager
  → Application::OnParamChanged()
      → pack params into float[16] at their cbufferOffsets
      → D3D11Renderer::SetCustomUniforms(float[16])
          → cbuffer updated on next BeginFrame()
```

`event` reset: after `SetCustomUniforms` is called with `values[0] = 1.0f`, a flag is set in `Application`; on the next `RenderFrame` call, the value is zeroed and `SetCustomUniforms` called again.

---

## Persistence

`ConfigManager` serialises `ShaderPreset::params` to `config.json`. Only the **current values** are persisted — metadata (label, min, max, etc.) is always re-parsed from source on load. On load, parsed params are matched by `name` to restore saved values; unmatched params use their `DEFAULT`.

---

## Constraints

- Maximum 16 floats of parameter data per shader (maps to `custom[16]` cbuffer slot).
- ISF block must appear before any non-comment HLSL token (first `/*{` match is used).
- Parameter `NAME` values must be valid HLSL identifiers (no spaces, no reserved words).
- No per-frame CPU cost: `SetCustomUniforms` is called only on user interaction and on shader activation.
