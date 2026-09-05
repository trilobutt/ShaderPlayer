---
title: Sampling and Derivatives
nav_title: Sampling and Derivatives
---

# Sampling and Derivatives

`Sample`, `SampleLevel(..., 0)` and `SampleGrad` return identical values in this pipeline.
Every texture the renderer creates has `MipLevels = 1`, and both samplers are
`MIN_MAG_MIP_LINEAR` with no anisotropy, so nothing in the pipeline consumes a derivative for
filtering. Two things follow, and both matter more than the choice of fetch:

No fetch can pick the wrong mip, so a soft or crawling result is never a mip selection bug
and chasing one wastes an afternoon. And no fetch gets minification filtering for free: a
shader that magnifies its source, or that pulls in fine detail from the frame edges under a
zoom, aliases unless it band-limits the result itself.

## When to write SampleLevel

Use `SampleLevel(..., 0)` for any fetch whose coordinate is loop-carried, `frac()`-wrapped,
folded, or computed downstream of a varying branch. The reason is flow control rather than
mip selection: an implicit-LOD fetch inside varying flow control is undefined, and `fxc`
rejects some forms of it outright with `X3595`. Since the explicit fetch returns the same
texels anyway, there is no cost to reaching for it, and the shipped set uses it as the
default inside any loop.

`videoSampler` at `s0` clamps on all three axes. A warp that runs off the frame therefore
smears the edge row of pixels rather than wrapping or going black, which is usually what you
want and occasionally very much not: `kaleidoscope.hlsl` implements clamp, mirror and wrap by
hand in `applyEdgeMode` because the sampler only offers the first. `noiseSampler` at `s1`
wraps.

## Where fwidth lies to you

`fwidth` and `ddx` report the difference between neighbouring pixels in a 2x2 quad. Take them
on the **continuous** coordinate, before any `frac()`, fold or wrap, and outside divergent
flow control. Past a discontinuity, the quad straddles the jump, the derivative reports an
infinitely wide pixel, and the antialiasing that depends on it smears a grey seam along every
boundary. That grey line down the middle of every cell is the signature of this bug.

Three shipped shaders show the three ways out.

**Derive the footprint from the scale you already have.** `game_of_life.hlsl` works in cell
space, where `frac()` makes the coordinate jump at every cell boundary. Rather than measuring
the jump, it computes the pixel size directly from the cell size in pixels:

```hlsl
    float  cellPix  = max(cellSz * (1.0 - aBass * 0.45), 2.0);
    float2 cellPx   = resolution / cellPix;

    // Position within the cell, and the size of one screen pixel in cell units.
    // Derived from cellPix directly: frac() makes the cell coordinate jump at
    // every boundary, so fwidth() on it reports a whole cell, not a pixel.
    float2 q      = frac(uv * cellPx) - 0.5;
    float  pxCell = 1.0 / cellPix;
```

`pxCell` is then the smoothstep width for the rounded tile edge:

```hlsl
        float  body  = 1.0 - smoothstep(-pxCell, pxCell, d);
```

**Measure first, fold second.** `crt_simulation.hlsl` needs the footprint of the phosphor
mask under a barrel curvature warp. The warped coordinate is continuous, so its derivative is
exact; the fold to a three-phosphor triad happens inside `phosphorMask`, after the width has
been taken:

```hlsl
    // Mask coordinate is continuous, so its derivative is exact even under the
    // curvature warp. Fold to the triad only after measuring the footprint.
    float2 maskUV = warpUV * resolution / max(MaskScale, 0.01);
    float  maskW  = max(max(fwidth(maskUV.x), fwidth(maskUV.y)), 1e-4) * 0.5;
    col *= phosphorMask(maskUV, maskW, MaskType);
```

**Supply the footprint analytically when no continuous coordinate exists.**
`kaleidoscope.hlsl` folds the frame into N mirrored segments, and at the centre the angular
size of a pixel goes to infinity: no derivative of any coordinate in that shader is
meaningful there. So it computes the angular footprint from geometry instead, and fades to
the colour at the centre point, which is the correct limit of the average as the radius goes
to zero:

```hlsl
    // Centre band-limit. One screen pixel subtends pw/r radians, and once that
    // exceeds half a segment the fold is packing more copies of the source into
    // the pixel than it can hold: the middle of the frame turns into a crawling
    // pinwheel of noise. Fade to the colour at the centre point instead, which is
    // the correct limit of the average as the radius goes to zero.
    float pw   = 1.0 / (resolution.y * zoomVal);
    float angW = pw / max(r, 1e-6);
    float centreFade = saturate(angW / (segAngle * 0.5));
    if (centreFade > 1e-3) {
        float3 hub = videoTexture.SampleLevel(videoSampler, applyEdgeMode(centreUV, EdgeMode), 0).rgb;
        col = lerp(col, hub, centreFade);
    }
```

## Measure, then fold

The pattern in full, as a shader you can drop into the shader directory. `spAAStep` would be
wrong below the fold, because it takes `fwidth` of whatever you hand it and the folded value
is discontinuous; the width measured before the fold is the one to carry down.

```hlsl
/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "Mirrored stripes over the frame, with the footprint measured before the fold.",
    "INPUTS": [
        {"NAME": "Stripes", "LABEL": "Stripe Count", "TYPE": "float",
         "MIN": 1.0, "MAX": 200.0, "STEP": 1.0, "DEFAULT": 40.0},

        {"NAME": "Depth", "LABEL": "Depth", "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "STEP": 0.01, "DEFAULT": 0.5}
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
    float3 col = videoTexture.Sample(videoSampler, input.uv).rgb;

    // Continuous across the whole frame. Measure the footprint here, once.
    float s = input.uv.x * max(Stripes, 1.0);
    float w = max(fwidth(s), 1e-6);

    // The fold. Everything below this line sees a coordinate that jumps at every
    // stripe boundary, so nothing below may take a derivative of it.
    float f = abs(frac(s) - 0.5) * 2.0;

    // Band-limited edge, using the width from above the fold.
    float edge = smoothstep(0.5 - w, 0.5 + w, f);

    col *= lerp(1.0, edge, Depth);
    return float4(col, 1.0);
}
```

Push `Stripes` to 200 on a 1080p frame and the stripes hold until each is about a pixel wide,
then flatten to an even shade. Replace the width with a hard `step(0.5, f)` and the same
setting gives a moire pattern instead, which is the aliasing the measurement was there to
prevent.
