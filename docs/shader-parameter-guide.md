# ShaderPlayer: Shader Parameter Guide

ShaderPlayer supports dynamic per-shader parameters. Declare them in an ISF-style JSON block comment at the top of your `.hlsl` file. ShaderPlayer parses the block, generates `#define` aliases so you can use readable names in your shader body, and renders the appropriate UI controls in the Shader Editor panel automatically.

---

## Quick Example

```hlsl
/*{
    "INPUTS": [
        {"NAME": "PixelSize", "LABEL": "Pixel Size", "TYPE": "float",
         "MIN": 1.0, "MAX": 64.0, "STEP": 1.0, "DEFAULT": 8.0}
    ]
}*/

Texture2D videoTexture : register(t0);
SamplerState videoSampler : register(s0);

Texture2D noiseTexture : register(t1);
SamplerState noiseSampler : register(s1);

cbuffer Constants : register(b0) {
    float time;
    float padding1;
    float2 resolution;
    float2 videoResolution;
    float2 padding2;
    float4 custom[4];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;
    float2 normalizedPixelSize = float2(PixelSize, PixelSize) / resolution;
    float2 uvPixel = normalizedPixelSize * floor(uv / normalizedPixelSize);
    return videoTexture.Sample(videoSampler, uvPixel);
}
```

`PixelSize` is resolved at compile time to the appropriate `custom[N].x` slot — you never write `custom[]` directly.

---

## ISF Block Syntax

Place the block **before any non-comment HLSL**. It must begin with `/*{` and end with `}*/`.

```hlsl
/*{
    "INPUTS": [
        { ...param... },
        { ...param... }
    ]
}*/
```

The block is a standard HLSL block comment — the compiler ignores it. ShaderPlayer reads it before passing the source to `D3DCompile` and prepends `#define` aliases. The source on disk is never modified.

If the JSON block is missing or malformed, no aliases are generated and any `NAME`-referenced identifier will produce an `X3004 undeclared identifier` compile error.

---

## Parameter Types

### `float` — Slider

```json
{
    "NAME": "Strength",
    "LABEL": "Effect Strength",
    "TYPE": "float",
    "MIN": 0.0,
    "MAX": 1.0,
    "STEP": 0.01,
    "DEFAULT": 0.5
}
```

Renders as a draggable slider. In the shader, `Strength` expands to a single float component (e.g. `custom[0].x`).

---

### `bool` — Checkbox

```json
{
    "NAME": "EnableVignette",
    "LABEL": "Enable Vignette",
    "TYPE": "bool",
    "DEFAULT": true
}
```

Renders as a checkbox. In the shader, `EnableVignette` expands to `(custom[N].x > 0.5)` — use it in a branch:

```hlsl
if (EnableVignette) { ... }
```

---

### `long` — Dropdown

```json
{
    "NAME": "BlendMode",
    "LABEL": "Blend Mode",
    "TYPE": "long",
    "VALUES": [0, 1, 2],
    "LABELS": ["Normal", "Multiply", "Screen"],
    "DEFAULT": 0
}
```

Renders as a dropdown. `VALUES` is an integer array (the actual selectable values). `LABELS` is a parallel string array (display text). **Both are required** — omitting `VALUES` produces an empty, un-selectable dropdown.

In the shader, `BlendMode` expands to `int(custom[N].x)`. Compare with integer literals:

```hlsl
if (BlendMode == 1) { /* Multiply */ }
```

A `DEFAULT` not present in `VALUES` leaves the combo stuck on the first entry.

---

### `color` — Colour Picker

```json
{
    "NAME": "Tint",
    "LABEL": "Tint Colour",
    "TYPE": "color",
    "DEFAULT": [1.0, 0.8, 0.6, 1.0]
}
```

Renders as an RGBA colour picker. `DEFAULT` is `[R, G, B, A]` in 0–1 range. In the shader, `Tint` expands to a `float4` (e.g. `custom[1]`). The `color` type always consumes 4 floats and is aligned to a `float4` boundary.

```hlsl
float4 col = videoTexture.Sample(videoSampler, input.uv);
return col * Tint;
```

---

### `point2d` — 2D Pad

```json
{
    "NAME": "Center",
    "LABEL": "Center",
    "TYPE": "point2d",
    "MIN": 0.0,
    "MAX": 1.0,
    "DEFAULT": [0.5, 0.5]
}
```

Renders as a clickable/draggable 2D square pad. `DEFAULT` is `[X, Y]`. In the shader, `Center` expands to a `float2` expression (e.g. `float2(custom[0].z, custom[0].w)`). The `point2d` type consumes 2 floats and is aligned to an even float offset.

`MIN`/`MAX` are scalar and apply to both axes.

```hlsl
float2 offset = input.uv - Center;
```

---

### `event` — Button

```json
{
    "NAME": "Flash",
    "LABEL": "Trigger Flash",
    "TYPE": "event"
}
```

Renders as a button. When pressed, the parameter is `1.0` for exactly one rendered frame, then automatically resets to `0.0`. In the shader, `Flash` expands to a raw float:

```hlsl
if (Flash > 0.5) { return float4(1, 1, 1, 1); }
```

Event parameters are not keyframeable.

---

### `audio` — Audio Band (read-only)

```json
{
    "NAME": "Bass",
    "LABEL": "Bass",
    "TYPE": "audio",
    "BAND": "bass"
}
```

Renders as a read-only level meter. Consumes no `custom[]` slot — the alias maps directly to an `AudioConstants` uniform. Valid `BAND` values: `rms`, `bass`, `mid`, `high`, `beat`, `centroid`.

When any `audio` parameter is present, ShaderPlayer auto-injects the `AudioConstants` cbuffer and `spectrumTexture` declarations into the source before compilation. You do not need to declare these manually.

Audio band values for typical music are in the range 0.01–0.3. Shader multipliers need to be 3–5× higher than intuition suggests for a visible effect.

Audio parameters are not persisted to `config.json` and are not keyframeable.

---

## Cbuffer Layout and Limits

All parameters are packed into the `custom[4]` (`float4[4]` = 16 floats) slot in the existing cbuffer. Packing is sequential with alignment:

| Type | Floats consumed | Alignment |
|---|---|---|
| `float` | 1 | none |
| `bool` | 1 | none |
| `long` | 1 | none |
| `event` | 1 | none |
| `point2d` | 2 | even float offset |
| `color` | 4 | multiple-of-4 float offset |
| `audio` | 0 | n/a (no custom[] slot) |

**Maximum: 16 floats total across all non-audio parameters.** Parameters that would exceed this limit are skipped with a warning in the compile error field.

---

## Multiple Parameters — Full Example

```hlsl
/*{
    "INPUTS": [
        {"NAME": "PixelSize",  "LABEL": "Pixel Size",    "TYPE": "float",
         "MIN": 1.0, "MAX": 64.0, "STEP": 1.0, "DEFAULT": 8.0},

        {"NAME": "Tint",       "LABEL": "Tint Colour",   "TYPE": "color",
         "DEFAULT": [1.0, 1.0, 1.0, 1.0]},

        {"NAME": "Center",     "LABEL": "Center",         "TYPE": "point2d",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": [0.5, 0.5]},

        {"NAME": "Greyscale",  "LABEL": "Greyscale",      "TYPE": "bool",
         "DEFAULT": false},

        {"NAME": "BlendMode",  "LABEL": "Blend Mode",     "TYPE": "long",
         "VALUES": [0, 1], "LABELS": ["Add", "Multiply"], "DEFAULT": 0},

        {"NAME": "Flash",      "LABEL": "Trigger Flash",  "TYPE": "event"}
    ]
}*/
```

Resulting packing (assuming this declaration order):

| Parameter | Type | Floats | Offset | Alias expands to |
|---|---|---|---|---|
| PixelSize | float | 1 | 0 | `custom[0].x` |
| (alignment pad for color) | — | 3 | 1–3 | — |
| Tint | color | 4 | 4 | `custom[1]` |
| Center | point2d | 2 | 8 | `float2(custom[2].x, custom[2].y)` |
| Greyscale | bool | 1 | 10 | `(custom[2].z > 0.5)` |
| BlendMode | long | 1 | 11 | `int(custom[2].w)` |
| Flash | event | 1 | 12 | `custom[3].x` |

Total: 13 floats used, 3 remaining.

---

## Shader Type

Add `"SHADER_TYPE"` to the ISF block to control how ShaderPlayer categorises and handles the shader:

```json
{
    "SHADER_TYPE": "audio",
    "INPUTS": [ ... ]
}
```

| Value | Behaviour |
|---|---|
| *(absent)* | Video effect — video texture is the primary input |
| `"video"` | Explicit video effect (same behaviour as absent) |
| `"generative"` | No video required — `time` drives animation |
| `"audio"` | Audio reactive — audio uniforms auto-injected |

The Shader Library groups presets into three sections: **Audio Reactive**, **Generative**, and **Video Effects**.

---

## Rules and Gotchas

- `NAME` must be a valid HLSL identifier (letters, digits, underscores; no spaces; not a reserved keyword or HLSL built-in). Avoid names like `frac`, `min`, `max`, `abs`, `lerp`, `linear`, `sample` — these shadow HLSL intrinsics and cause compile errors.
- The ISF block must appear before any non-comment HLSL. The first `/*{` in the file is used.
- Do not declare `custom[]` yourself in the cbuffer — it is part of the standard ShaderPlayer cbuffer. The standard cbuffer block must still appear in your shader.
- `noiseTexture` and `noiseSampler` must be declared in every shader even if unused — they are always bound by the renderer.
- Parameter metadata (min, max, label, etc.) is always read from the shader source on load. Only the **current values** are saved in `config.json` — changing metadata in the file takes effect on the next compile.
- `color` defaults are RGBA in 0–1 range, not 0–255.
- `long` `VALUES` contains integers; `LABELS` contains strings. Never put strings in `VALUES`.
- `MIN`/`MAX`/`STEP` must be scalar numbers. Array-form (e.g. `"MIN": [0.0, 0.0]`) is silently ignored and the field stays at its default.
- If a shader disappears after Scan Folder, it has a compile error. The `ShaderPreset::compileError` field holds the message (no UI surface yet — check in the debugger or add a `// compile error:` guard temporarily).
