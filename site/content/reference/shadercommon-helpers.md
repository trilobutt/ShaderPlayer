---
title: ShaderCommon Helpers
nav_title: ShaderCommon Helpers
---

# ShaderCommon Helpers

`src/ShaderCommon.hlsli` is prepended to every shader before compilation, so everything on
this page is already in scope in your file with nothing to include. Every symbol is
`sp`-prefixed, `SP_` for the macros, so the library cannot collide with a helper you write
yourself; the flip side is that a function of yours named `spSomething` might one day collide
with the library, so leave the prefix alone.

`fxc` dead-strips what you do not call. A shader that uses none of these compiles to
byte-identical bytecode, which is why the whole library is injected unconditionally rather
than opted into.

Two constants come with it: `SP_PI` (3.14159265358979) and `SP_TAU` (6.28318530717959).

## Tonemapping

| Signature | Notes |
|---|---|
| `float3 spTonemapACES(float3 x)` | Narkowicz ACES fit. Punchy, saturated, sharp highlight roll-off |
| `float3 spTonemapTanh(float3 x)` | Darker mids, stronger colour. The natural partner for inverse-distance glow accumulation, which produces unbounded values |
| `float3 spTonemapUnreal(float3 x)` | Brightest of the three, and sRGB gamma is baked in, so do **not** follow it with `spLinearToSrgb` |
| `float spTanh1(float x)` | `tanh` for a scalar, which `ps_5_0` does not have. Clamped to ±40 so `exp` cannot overflow |

A tonemap belongs at the end of the shader, after everything that accumulates light and
before the sRGB conversion. Accumulating into an unbounded value and then saturating instead
is what produces flat white blobs where a highlight should roll off.

## Colour

| Signature | Notes |
|---|---|
| `float3 spLinearToSrgb(float3 c)` | Piecewise exact, not a 2.2 power approximation |
| `float3 spSrgbToLinear(float3 c)` | The inverse. Use it before any physically-shaped maths on video pixels |
| `float3 spHsv2rgb(float3 c)` | `c` is (hue, saturation, value), hue in 0 to 1 |
| `float3 spRgb2hsv(float3 c)` | The inverse |
| `float spLuma(float3 c)` | Rec.709 luminance |
| `float3 spPalette(float t, float3 a, float3 b, float3 c, float3 d)` | Inigo Quilez cosine palette: `a` bias, `b` amplitude, `c` frequency, `d` phase |

`spPalette` is the reason no shipped shader carries a hand-rolled gradient. It is continuous,
cheap, and better behaved than a piecewise `lerp` between stops, which bands at every stop and
needs a branch per segment.

## Hashing and dither

| Signature | Notes |
|---|---|
| `float spHash12(float2 p)` | Hoskins hash without sine: deterministic, no trig, no texture fetch |
| `float2 spHash22(float2 p)` | Two outputs from a 2D input |
| `float3 spHash33(float3 p)` | Three outputs from a 3D input |
| `float spIGN(float2 pixel)` | Jimenez interleaved gradient noise. Takes a **pixel coordinate** (`input.pos.xy`), not a UV. Spectrally flat enough to dither with and cheaper than a hash |
| `float3 spDither(float3 col, float2 pixel, float amount)` | Triangular-PDF dither. `amount` is `1.0 / 255.0` for an 8-bit target |

Dither last, after the tonemap, immediately before the return. Applied earlier it gets
squashed by whatever curve follows and stops doing the one job it has, which is breaking up
the banding an 8-bit output puts into a smooth gradient.

## Band limiting

These three take a screen-space derivative of their argument, so they must not be called from
inside divergent flow control: compute the field first, branch afterwards. See
[Sampling and derivatives](/reference/sampling-and-derivatives/) for what goes wrong when the
argument is discontinuous.

| Signature | Notes |
|---|---|
| `float spAAStep(float threshold, float value)` | `step(threshold, value)` filtered over one pixel of the value's own footprint |
| `float spAALine(float signedDist, float width)` | 1 inside a line of half-width `width` about `signedDist == 0`, 0 outside |
| `float spBandLimitedCos(float x, float w)` | `cos(x)` attenuated by its average over a footprint of width `w` (pass `fwidth(x)`). Stripes fade to flat grey instead of aliasing |

## Composition

| Signature | Notes |
|---|---|
| `float spVignette(float2 uv, float amount, float softness)` | Returns a multiplier: 1 at the centre, falling to `1 - amount` at the corners. `softness` is the width of the falloff in normalised corner distance |
| `float2 spAspectUV(float2 uv, float2 res)` | Centred, aspect-corrected coordinates: (0,0) at the centre, y spanning -1 to 1 |

`spAspectUV` is the first line of most generative shaders here. Without it every circle is an
ellipse the moment the window stops being square, and every distance field is wrong by the
aspect ratio.

## A worked example

```hlsl
/*{
    "SHADER_TYPE": "generative",
    "DESCRIPTION": "Band-limited rings, as a worked example of the helper library.",
    "INPUTS": [
        {"NAME": "Rings", "LABEL": "Ring Count", "TYPE": "float",
         "MIN": 1.0, "MAX": 64.0, "STEP": 0.5, "DEFAULT": 12.0},

        {"NAME": "Speed", "LABEL": "Speed", "TYPE": "float",
         "MIN": 0.0, "MAX": 4.0, "STEP": 0.05, "DEFAULT": 1.0}
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
    float2 p = spAspectUV(input.uv, resolution);
    float  r = length(p);

    // The phase is continuous everywhere, so its footprint is exact. Measure it
    // before anything folds or wraps the coordinate.
    float phase = r * Rings * SP_TAU - time * Speed;
    float w     = fwidth(phase);

    // Rings that fade to flat grey where they outrun the sampling rate, instead
    // of turning into a moire pattern.
    float rings = spBandLimitedCos(phase, w) * 0.5 + 0.5;

    float3 col = spPalette(rings * 0.35 + r * 0.2 + time * 0.02,
                           float3(0.5, 0.5, 0.5),
                           float3(0.5, 0.5, 0.5),
                           float3(1.0, 1.0, 1.0),
                           float3(0.0, 0.33, 0.67));

    // A single hard-edged ring, antialiased from its signed distance.
    col += spAALine(r - 0.75, 0.004) * 0.6;

    col *= spVignette(input.uv, 0.45, 0.8);

    // Tonemap, then sRGB, then dither, in that order and last.
    col = spLinearToSrgb(spTonemapACES(col));
    return float4(spDither(col, input.pos.xy, 1.0 / 255.0), 1.0);
}
```

Read the file itself for anything this page compresses: it is 157 lines, every function
carries the reason it exists, and it is the definition rather than a description of one. Note
also that the preamble ends with `#line 1 "<preset name>"`, so a compile error reports the
line number in your file and not a number inflated by the library sitting above it.
