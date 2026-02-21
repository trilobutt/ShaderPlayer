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

The block is stripped before compilation — it is never seen by the HLSL compiler.

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
    "VALUES": ["Normal", "Multiply", "Screen"],
    "DEFAULT": 0
}
```

Renders as a dropdown. In the shader, `BlendMode` expands to `int(custom[N].x)`. Compare with integer literals:

```hlsl
if (BlendMode == 1) { /* Multiply */ }
```

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

**Maximum: 16 floats total across all parameters.** Parameters that would exceed this limit are ignored (a warning appears in the compile error field).

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
         "VALUES": ["Add", "Multiply"], "DEFAULT": 0},

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

## Rules and Gotchas

- `NAME` must be a valid HLSL identifier (letters, digits, underscores; no spaces; not a reserved keyword).
- The ISF block must appear before any non-comment HLSL. The first `/*{` in the file is used.
- Do not declare `custom[]` yourself in the cbuffer — it is part of the standard ShaderPlayer cbuffer. The standard cbuffer block must still appear in your shader.
- Parameter metadata (min, max, label, etc.) is always read from the shader source on load. Only the **current values** are saved in `config.json` — so changing metadata in the file takes effect on the next compile.
- `color` defaults are RGBA in 0–1 range, not 0–255.
- `point2d` `MIN`/`MAX` are scalar and apply to both axes.
