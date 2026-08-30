---
title: The ISF Block and Parameters
nav_title: ISF Block and Parameters
---

# The ISF Block and Parameters

Parameters are declared in a JSON block comment at the top of the shader, and everything
downstream follows from it: the widgets in the Shader Parameters panel, the slot each value
occupies in `custom[]`, the `#define` alias you write in the body, and what `config.json`
persists between sessions. There is no second place to register a parameter.

ShaderPlayer looks for the first `/*{` in the file and reads to the first `}*/` after it,
wraps the contents in braces, and parses that as JSON. Put the block before any non-comment
HLSL. It is an ordinary block comment, so the compiler ignores it and the file on disk is
never rewritten.

```hlsl
/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "One sentence about what this shader does.",
    "INPUTS": [
        {"NAME": "Strength", "LABEL": "Strength", "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "STEP": 0.01, "DEFAULT": 0.5}
    ]
}*/
```

`SHADER_TYPE` is `generative`, `audio`, or `video`; anything else, including its absence, is
treated as a video effect. It decides which of the three Shader Library sections the preset
appears in and nothing else. `DESCRIPTION` is read by the documentation extractor that builds
this site, and the app ignores it; every shipped shader carries one, and a shader without one
is refused by `site/tools/extract_shaders.py`.

## Every type at once

This shader declares all seven parameter types and reads every one of them. It is also the
worked packing example below.

```hlsl
/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "Every parameter type at once, as a packing demonstration.",
    "INPUTS": [
        {"NAME": "PixelSize", "LABEL": "Pixel Size", "TYPE": "float",
         "MIN": 1.0, "MAX": 64.0, "STEP": 1.0, "DEFAULT": 8.0},

        {"NAME": "Tint", "LABEL": "Tint", "TYPE": "color",
         "DEFAULT": [1.0, 1.0, 1.0, 1.0]},

        {"NAME": "Centre", "LABEL": "Centre", "TYPE": "point2d",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": [0.5, 0.5]},

        {"NAME": "Greyscale", "LABEL": "Greyscale", "TYPE": "bool",
         "DEFAULT": false},

        {"NAME": "BlendMode", "LABEL": "Blend Mode", "TYPE": "long",
         "VALUES": [0, 1], "LABELS": ["Add", "Multiply"], "DEFAULT": 0},

        {"NAME": "Flash", "LABEL": "Trigger Flash", "TYPE": "event"},

        {"NAME": "BassIn", "LABEL": "Bass", "TYPE": "audio", "BAND": "bass"}
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
    float4 custom[8];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    // point2d and float: quantise the frame to a block grid anchored at Centre.
    float2 block = max(PixelSize, 1.0) / resolution;
    float2 uv    = (floor((input.uv - Centre) / block) + 0.5) * block + Centre;

    float3 col = videoTexture.Sample(videoSampler, uv).rgb;

    // bool expands to a comparison, so it reads as a condition.
    if (Greyscale) col = spLuma(col).xxx;

    // long expands to int(), so compare it against integer literals.
    if (BlendMode == 1) col *= Tint.rgb;
    else                col += Tint.rgb - 1.0;

    // An audio band is a plain float in [0,1], and a small one: 3x here.
    col *= 1.0 + BassIn * 3.0;

    // An event is 1.0 for exactly one frame after its button is pressed.
    col = lerp(col, float3(1.0, 1.0, 1.0), saturate(Flash));

    return float4(col, 1.0);
}
```

`tools/validate_shaders.py` reports `13/32 floats, 7 params` for that file, which is the
number to compare against the packing table below.

## The seven types

Every entry needs `NAME` and `TYPE`; an entry missing either is skipped without comment.
`NAME` must be a valid HLSL identifier and must not shadow an intrinsic or a reserved word
(see [Gotchas](/reference/gotchas/)). `LABEL` is the text in the panel and defaults to
`NAME`.

**`float`** renders a slider and expands to one component.

```json
{"NAME": "Strength", "LABEL": "Strength", "TYPE": "float",
 "MIN": 0.0, "MAX": 1.0, "STEP": 0.01, "DEFAULT": 0.5}
```

**`bool`** renders a checkbox and expands to `(custom[i].c > 0.5)`, so it reads as a
condition directly: `if (Greyscale) { ... }`. `DEFAULT` is a JSON `true` or `false`.

**`long`** renders a dropdown over `VALUES`, with `LABELS` as the display strings in
parallel. It expands to `int(custom[i].c)`, and what is stored is the entry from `VALUES`
rather than the combo index, so the numbers you compare against in the body are the numbers
you wrote in the JSON.

```json
{"NAME": "BlendMode", "LABEL": "Blend Mode", "TYPE": "long",
 "VALUES": [0, 1, 2], "LABELS": ["Normal", "Multiply", "Screen"], "DEFAULT": 0}
```

A `long` takes no `MIN` or `MAX`. It is a dropdown over a fixed list, the panel applies no
numeric range to it, and no shipped shader declares bounds on one. `VALUES` is required:
without it the dropdown is empty and cannot be operated at all, and a `DEFAULT` that is not
present in `VALUES` leaves the combo stuck on the first entry.

**`color`** renders an RGBA picker and expands to the whole `float4`, so `Tint.rgb` and
`Tint.a` both work. `DEFAULT` is `[R, G, B, A]` in the 0 to 1 range, not 0 to 255. The alpha
channel is the idiomatic per-element opacity control, and the parameter randomiser
deliberately leaves it alone rather than rolling an effect into invisibility.

**`point2d`** renders a pair of spin boxes prefixed `X ` and `Y `, and expands to a `float2`
built from two adjacent components. `DEFAULT` is `[X, Y]`; `MIN` and `MAX` are scalars and
apply to both axes.

**`event`** renders a button, takes no `MIN`, `MAX` or `DEFAULT`, and expands to a raw float.
Pressing it sets the value to 1.0 for exactly one rendered frame, after which the application
zeroes it again. Event parameters cannot be keyframed.

**`audio`** renders a read-only level meter and consumes no `custom[]` slot at all: the alias
points straight at an `AudioConstants` field. `BAND` is one of `rms`, `bass`, `mid`, `high`,
`beat`, `centroid`. Audio parameters are not persisted to `config.json` and cannot be
keyframed. See [Audio bands and the spectrum](/reference/audio-and-spectrum/).

```json
{"NAME": "BassIn", "LABEL": "Bass", "TYPE": "audio", "BAND": "bass"}
```

## Packing and the aliases it generates

Parameters are packed into `float4 custom[8]`, 32 floats, in declaration order. Below, `i` is
`offset / 4` and `c` is the component named by `"xyzw"` at `offset % 4`.

| TYPE | Floats | Alignment | Alias generated |
|---|---|---|---|
| `float` | 1 | none | `custom[i].c` |
| `bool` | 1 | none | `(custom[i].c > 0.5)` |
| `long` | 1 | none | `int(custom[i].c)` |
| `event` | 1 | none | `custom[i].c` |
| `point2d` | 2 | next even offset | `float2` of two adjacent components |
| `color` | 4 | next multiple of 4 | `custom[i]` |
| `audio` | 0 | not packed | `audioBass`, `audioRms`, and so on |

For the shader above, that gives:

| Parameter | Type | Floats | Offset | Alias expands to |
|---|---|---|---|---|
| PixelSize | float | 1 | 0 | `custom[0].x` |
| *pad to a float4 boundary* | | 3 | 1 to 3 | |
| Tint | color | 4 | 4 | `custom[1]` |
| Centre | point2d | 2 | 8 | `float2(custom[2].x, custom[2].y)` |
| Greyscale | bool | 1 | 10 | `(custom[2].z > 0.5)` |
| BlendMode | long | 1 | 11 | `int(custom[2].w)` |
| Flash | event | 1 | 12 | `custom[3].x` |
| BassIn | audio | 0 | none | `audioBass` |

Thirteen floats used, nineteen left. Three of those thirteen are the pad in front of `Tint`,
which is the cost of putting a colour after a single float: group your colours together, or
declare them first, and a shader that is close to the budget gains a few slots for free.

Declaration order is the whole of the packing rule, so inserting a parameter near the top of
a long `INPUTS` list moves every offset after it. That is harmless as long as you never write
an offset down, which is the reason the aliases exist.

Reorder freely while a shader is under development, but know what `config.json` keys its
saved values on. Metadata (type, label, bounds, defaults) is re-read from the source on every
load and every recompile; the saved current values are matched back to it by `NAME`. Rename a
parameter and its saved value is dropped and the default takes over.

## What the parser skips without telling you

Five failures are silent by design, and each has a different symptom.

- **A malformed JSON block** yields no parameters at all, so every alias is missing and the
  shader fails to compile with `X3004: undeclared identifier` on the first parameter name.
  The preset still appears in the Shader Library carrying a red status dot, with the
  compiler's message as its tooltip, and selecting it draws the untreated picture: with no
  compiled shader the renderer falls back to passthrough.
- **An unknown `TYPE`**, or an entry missing `NAME` or `TYPE`, is dropped, and the rest of
  the list packs as though it had never been written.
- **Array-form `MIN`, `MAX` or `STEP`** is ignored. Those three keys are read only when the
  JSON value is a number, so `"MIN": [0.0, 0.0]` on a `point2d` leaves the bound at its
  default of 0.0, with `MAX` 1.0 and `STEP` 0.01. Write scalars for every type, `point2d`
  and `color` included.
- **Parameters past the 32-float budget** stop the parse. The first parameter that does not
  fit, and everything after it, are dropped, and the shader then fails to compile on the
  first dropped name.
- **A parameter declared but never read** packs its slot and renders a working widget that
  moves nothing. Nothing can detect this: an unused `#define` is not an error.

The offline validator catches the first four before you launch anything:

```
python tools/validate_shaders.py default_shaders/my_shader.hlsl
```

It reproduces the injected preamble exactly, compiles with `fxc` at `/O3`, prints the packed
float count per shader, and exits non-zero on a compile error or a budget overflow.

The author-facing version of this material ships with the source as
`docs/shader-parameter-guide.md`.
