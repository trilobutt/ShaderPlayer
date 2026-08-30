---
title: The Cbuffer Contract
nav_title: Cbuffer Contract
---

# The Cbuffer Contract

A ShaderPlayer shader is a `ps_5_0` pixel shader with a fixed header. Copy the header
below, write a body, and it compiles against the real pipeline on the first try. Every
field, register and semantic in it is checked by the renderer, so a shader that spells
one of them differently either fails to compile or reads the wrong bytes.

```hlsl
/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "Minimal video effect: samples the frame and tints it.",
    "INPUTS": [
        {"NAME": "Tint", "LABEL": "Tint", "TYPE": "color",
         "DEFAULT": [1.0, 0.9, 0.8, 1.0]}
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
    float4 col = videoTexture.Sample(videoSampler, input.uv);
    return float4(col.rgb * Tint.rgb, 1.0);
}
```

The cbuffer is uploaded as a `memcpy` of a C++ struct, so its layout is positional. Reorder
the fields, drop `padding1`, or make `resolution` a `float3` and the shader still compiles
and reads whatever now sits at that offset. There is no diagnostic for this; the picture
just goes wrong.

## What is bound

`BeginFrame()` sets the whole pixel-shader state before any draw, so these are bound for
every shader whether it declares them or not.

| Register | Declaration | Contents |
|---|---|---|
| `b0` | `cbuffer Constants` | Time, resolutions, and the 32-float `custom[]` parameter block |
| `b1` | `cbuffer AudioConstants` | The six audio bands. Injected for you when the shader declares an audio parameter |
| `t0` | `Texture2D videoTexture` | The decoded frame, RGBA8. Unbound and black with no video open |
| `s0` | `SamplerState videoSampler` | Bilinear, CLAMP on all three axes |
| `t1` | `Texture2D noiseTexture` | The [global noise texture](/reference/noise-texture/), Perlin in R and Voronoi in G |
| `s1` | `SamplerState noiseSampler` | Bilinear, WRAP on all three axes |
| `t3` | `Texture2D spectrumTexture` | 256x1 `R32_FLOAT` [FFT magnitudes](/reference/audio-and-spectrum/). Injected alongside `b1` |

Declare `videoTexture`, `videoSampler`, `noiseTexture` and `noiseSampler` in every shader,
whether or not you sample them. Do not declare `AudioConstants` or `spectrumTexture`: once
any parameter is an audio band the preamble has already written both, and a second
declaration fails with `error X3003: redefinition of 'spectrumTexture'`. `t2` belongs to the
video-blend compositor and is not yours.

## The fields

`time` is seconds, as a float. With a video open it is the playback position, so it jumps
when you scrub and returns to zero on Stop. With nothing open it is wall-clock seconds
accumulated since the shader started, advancing on every tick unless the transport is
Paused. A generative shader gets a clock either way. There is no frame counter anywhere in
the contract, so `time` is the only clock there is.

`resolution` is the size of the viewport panel in pixels, updated on every window resize.
It is the size the picture is shown at rather than the size of the surface your shader
fills: the frame is rendered into a texture at the content resolution, which is the video's
own dimensions when one is open and the generative output resolution chosen in the
Transport panel when none is. Use `resolution` for aspect ratio and for the scale of
screen-space features, and do not read `1.0 / resolution.y` as an exact device pixel. Under
`shaderfx` there is no window, and `resolution` is the `--size` you asked for, which is also
the render size.

`videoResolution` is the source video's dimensions. With no video open the renderer mirrors
the generative output resolution into it, so a generative shader that divides by it never
divides by zero.

`padding1` and `padding2` hold the layout together and are not yours to read. The renderer
passes the video blend amount through `padding1` and the blend mode through `padding2.x`
for the compositor pass, so they are not reliably zero.

`custom[8]` is the parameter block: 32 floats, filled from your ISF block. Never index it
by hand. The compiler generates a named alias per parameter, and hand-written indices go
stale the moment you add a parameter above them. See
[the ISF block](/reference/isf-block-and-parameters/).

## Entry point and output

The entry point is `main`, the input struct carries `SV_POSITION` and one `TEXCOORD0`, and
the return semantic is `SV_TARGET`. `input.uv` runs 0 to 1 with (0, 0) at the top-left, and
`input.pos.xy` is the pixel centre in the render target, which is what
[`spIGN` and `spDither`](/reference/shadercommon-helpers/) want.

Shaders are compiled with `D3DCOMPILE_OPTIMIZATION_LEVEL3`. `tools/validate_shaders.py`
passes `/O3` to `fxc` to match, so a shader that validates offline compiles in the app.

The returned alpha is not decoration. When a Video Blend mode is active the compositor
computes:

```hlsl
float3 out_rgb = lerp(v.rgb, r, blendAmount * g.a);
```

`v` is the video, `g` is what your shader wrote, and `r` is the two combined under whichever
mode is selected. Your alpha scales the whole blend, so `alpha < 1` lets the video through in
those pixels under every mode, and `alpha = 1` behaves as though the channel were not there.
With blending Off the shader draws straight to the display texture and alpha does nothing.
This generative shader is fully transparent outside its disc:

```hlsl
/*{
    "SHADER_TYPE": "generative",
    "DESCRIPTION": "A soft disc that leaves the rest of the frame transparent.",
    "INPUTS": [
        {"NAME": "Radius", "LABEL": "Radius", "TYPE": "float",
         "MIN": 0.05, "MAX": 1.0, "STEP": 0.01, "DEFAULT": 0.35},
        {"NAME": "DiscColour", "LABEL": "Colour", "TYPE": "color",
         "DEFAULT": [1.0, 0.85, 0.4, 1.0]}
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
    // spAspectUV puts (0,0) at the centre with y spanning [-1,1].
    float2 p = spAspectUV(input.uv, resolution);

    // Breathe the radius so the shader has something to do with `time`.
    float r = Radius * (0.9 + 0.1 * sin(time * 1.5));

    // Signed distance to the disc edge, band-limited to one pixel by spAAStep.
    float disc = 1.0 - spAAStep(0.0, length(p) - r);

    return float4(DiscColour.rgb, disc * DiscColour.a);
}
```

## What is prepended to your source

The compiler never sees your file verbatim. `ShaderManager::BuildDefinesPreamble` assembles,
in order: the whole of [`ShaderCommon.hlsli`](/reference/shadercommon-helpers/); the
`AudioConstants` block and the `spectrumTexture` declaration if any parameter is an audio
band; one `#define` per parameter; and finally `#line 1 "<preset name>"`, which resets the
line counter so `fxc` error messages point at lines in your file rather than at
preamble-inflated positions.

Print the exact text the compiler sees with:

```
python tools/validate_shaders.py --dump default_shaders/my_shader.hlsl
```

Running `fxc` on a shader file directly instead is worthless: without the preamble every
parameter name is an undeclared identifier and every helper is missing.
