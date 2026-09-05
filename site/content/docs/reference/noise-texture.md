---
title: The Noise Texture
nav_title: Noise Texture
---

# The Noise Texture

Every shader gets a noise texture at `t1` whether it asks for one or not, with a WRAP
sampler at `s1`. Two independent fields share it: **red is Perlin gradient noise, green is
Voronoi F1, inverted so cell centres are bright.** Blue is zero and alpha is one; neither
carries anything, so do not read them.

```hlsl
Texture2D noiseTexture : register(t1);
SamplerState noiseSampler : register(s1);   // WRAP addressing
```

Declare both in every shader, sampled or not. The texture is generated on the CPU and
uploaded as an immutable resource, so it is the same for every shader, the same for every
pixel of a frame, and completely static: **it does not animate.** Motion comes from what you
do to the coordinate, which is why almost every use of it scrolls or scales the UV by `time`.

## The two channels

Red is Perlin noise mapped into 0 to 1, so it sits around 0.5 with no signal. Subtract 0.5
for a signed field, which is what you want for a displacement or a curl.

Green is the distance to the nearest Voronoi feature point, clamped to 1 and then inverted:
1.0 exactly at a cell centre, falling toward 0 at the cell edges. Threshold it for cells,
take its gradient for edges, or use it directly as a scatter of soft blobs.

Both channels are 8-bit, which is 256 levels. That is plenty for a perturbation and visibly
short for a slow gradient: a full-frame ramp sampled straight out of the red channel bands.
Add a [dither](/docs/reference/shadercommon-helpers/) or keep the amplitude small.

## Size, scale and the seam

The Noise Generator panel (View, Noise Generator) owns two settings, persisted in
`config.json` as `noiseScale` and `noiseTextureSize`. Size is the square resolution of the
texture, 512 by default and never smaller than 64. Scale is how much of the underlying noise
field is packed into that square, 4.0 by default, so a higher scale means more, smaller
features per texture rather than a different kind of noise. Both are the user's to change,
and a shader that only works at one setting is a shader that will look wrong on someone
else's machine.

The tile is not seam-matched. The gradient hash behind the Perlin field is not periodic, so
the right edge of the texture does not continue into the left edge, and a coordinate that
crosses an integer UV boundary crosses that join. Tile at a frequency where it hides in the
detail, or keep the sampled coordinate inside one unit square.

## A worked example

```hlsl
/*{
    "SHADER_TYPE": "generative",
    "DESCRIPTION": "Worked noise example: a drifting Perlin field under Voronoi cells.",
    "INPUTS": [
        {"NAME": "Frequency", "LABEL": "Frequency", "TYPE": "float",
         "MIN": 0.5, "MAX": 16.0, "STEP": 0.1, "DEFAULT": 4.0},

        {"NAME": "DriftSpeed", "LABEL": "Drift Speed", "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "STEP": 0.01, "DEFAULT": 0.08},

        {"NAME": "CellSize", "LABEL": "Cell Size", "TYPE": "float",
         "MIN": 8.0, "MAX": 128.0, "STEP": 1.0, "DEFAULT": 48.0}
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
    float2 uv = input.uv;

    // Red: Perlin, centred on 0.5, so subtract to get a signed field. The
    // coordinate is continuous, so a plain Sample is correct here.
    float2 driftUV = uv * Frequency + time * DriftSpeed * float2(1.0, 0.6);
    float  perlin  = noiseTexture.Sample(noiseSampler, driftUV).r - 0.5;

    // Green: Voronoi F1, bright at cell centres.
    float cells = noiseTexture.Sample(noiseSampler, uv * Frequency * 0.5).g;

    // Per-cell variation: give every grid cell its own 1/64 slice of the
    // texture, so neighbours draw uncorrelated noise instead of a continuous
    // field with a grid drawn over it. frac() in the coordinate is why this
    // fetch is SampleLevel.
    float2 cellPx    = resolution / max(CellSize, 1.0);
    float2 cellCoord = floor(uv * cellPx);
    float2 cellUv    = frac(uv * cellPx);
    float2 sliceUV   = cellCoord / 64.0 + cellUv * (Frequency / 64.0);
    float  grain     = noiseTexture.SampleLevel(noiseSampler, sliceUV, 0).r;

    float t = perlin * 1.5 + cells * 0.35 + grain * 0.2;

    float3 col = spPalette(t,
                           float3(0.5, 0.5, 0.5),
                           float3(0.5, 0.45, 0.4),
                           float3(1.0, 1.0, 1.0),
                           float3(0.0, 0.15, 0.35));

    return float4(spLinearToSrgb(spTonemapACES(col)), 1.0);
}
```

The per-cell pattern is worth reading twice, because it is the one use of this texture that
is not obvious. `cellCoord / 64.0` picks a distinct starting point per cell, `cellUv *
(freq / 64.0)` walks a window from there, and dividing both by the same 64 keeps the window
inside its own slot. It gives 64 by 64 distinct slices before the WRAP addressing repeats
them, which is far more variation than a grid of cells ever shows at once.
