---
title: Audio Bands and the Spectrum
nav_title: Audio and Spectrum
---

# Audio Bands and the Spectrum

Six scalars and one texture, bound before every draw, zeroed when nothing is playing. That
is the whole audio interface, and the only thing standing between a shader and it is one
`audio` parameter in the ISF block, which makes the renderer inject the `AudioConstants`
cbuffer at `b1` and the spectrum texture at `t3`.

The analyser runs a 2048-point real FFT with a Hann window over the decoded audio of
whatever is open, advancing by half a window each time so a bass transient cannot fall
between two analysis frames.

## The six bands

Declare each band you want as an `audio` parameter with a `BAND` key. The alias points
straight at the `AudioConstants` field, consumes none of the 32-float `custom[]` budget, and
shows in the Shader Parameters panel as a read-only meter.

| `BAND` | Uniform | What it measures |
|---|---|---|
| `rms` | `audioRms` | RMS of the windowed 2048-sample block: overall loudness |
| `bass` | `audioBass` | RMS of the magnitude bins from 20 to 250 Hz |
| `mid` | `audioMid` | RMS of the magnitude bins from 250 to 4000 Hz |
| `high` | `audioHigh` | RMS of the magnitude bins from 4000 to 20000 Hz |
| `beat` | `audioBeat` | A pulse set to 1.0 on an onset, decaying every frame after |
| `centroid` | `audioSpectralCentroid` | Magnitude-weighted mean bin, normalised against Nyquist: brightness |

Every value is clamped to the 0 to 1 range, then smoothed by an exponential moving average
whose coefficient is the Smoothing control in the Audio Monitor panel (0.3 by default, where
0 is unsmoothed and 1 is frozen). A beat is declared when the bass energy exceeds the rolling
average of the last 43 analysis frames times the Beat Sensitivity (1.5 by default), and the
pulse then decays by a factor of 0.92 per frame, so it reads as a spike with a tail of about
a fifth of a second rather than a single-frame flash.

**The values are much smaller than they look.** For ordinary music, bands sit between 0.01
and 0.3, and 0.3 is loud. A modulation written as `1.0 + bass` moves nothing a viewer will
notice; multipliers of three to five times what intuition suggests are the working range.
Every shipped generative shader that reads audio wraps its modulation in an Audio Amount
control for exactly this reason, and setting that to 0 gives back the unmodulated pattern.

One `audio` parameter injects the whole cbuffer, so all six uniform names are reachable in
the body afterwards whether or not you declared each as a parameter. Declaring them is what
gives the user a meter to look at, which is worth the two lines.

## The spectrum texture

`spectrumTexture` at `t3` is 256 by 1, `R32_FLOAT`, rewritten every frame. Sample it at
`float2(x, 0.5)` and read `.r`; `x` is the frequency axis, running 0 at DC to 1 at Nyquist.

```hlsl
float mag = spectrumTexture.SampleLevel(videoSampler, float2(x, 0.5), 0).r;
```

Use `videoSampler` for it. There is no third sampler, and the clamp addressing on `s0` is
what you want at the ends of the axis anyway.

The 1024 usable FFT bins are max-pooled down to the 256 texels, then smoothed by the same
EMA as the bands. **The axis is linear in frequency, and that is the one thing to design
around.** With a 48 kHz source each texel covers about 94 Hz, so the entire bass range from
20 to 250 Hz lands inside the first three texels: a bar graph drawn on a linear axis is a
wall of activity in its leftmost 2% and near-silence across the rest. Warp the axis with a
power curve or a logarithm before you sample, which is what the `Tilt` control does below.

## A worked audio shader

```hlsl
/*{
    "SHADER_TYPE": "audio",
    "DESCRIPTION": "Spectrum bars with a beat flash, as a worked audio example.",
    "INPUTS": [
        {"NAME": "BarCount", "LABEL": "Bars", "TYPE": "float",
         "MIN": 8.0, "MAX": 128.0, "STEP": 1.0, "DEFAULT": 48.0},

        {"NAME": "Tilt", "LABEL": "Frequency Tilt", "TYPE": "float",
         "MIN": 1.0, "MAX": 4.0, "STEP": 0.05, "DEFAULT": 2.5},

        {"NAME": "BarColour", "LABEL": "Bar Colour", "TYPE": "color",
         "DEFAULT": [0.25, 0.85, 1.0, 1.0]},

        {"NAME": "RmsIn", "LABEL": "Level", "TYPE": "audio", "BAND": "rms"},
        {"NAME": "BeatIn", "LABEL": "Beat", "TYPE": "audio", "BAND": "beat"}
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
    float bars    = max(BarCount, 1.0);
    float barIdx  = floor(input.uv.x * bars);
    float xInBar  = frac(input.uv.x * bars);

    // Warp the frequency axis before sampling: pow(t, 2.5) pulls the first few
    // hundred Hz out across a third of the width instead of three texels.
    float t = pow(saturate((barIdx + 0.5) / bars), Tilt);

    // Loop-free, but the coordinate has been through floor() and pow(), so the
    // fetch is SampleLevel rather than Sample. See the sampling page.
    float mag = spectrumTexture.SampleLevel(videoSampler, float2(t, 0.5), 0).r;

    // Bands and bins are both small for music. Scale, then clamp.
    float height = saturate(mag * 3.0 + RmsIn * 0.5);

    // uv.y runs downward, so the bar grows from the bottom of the frame.
    float column = smoothstep(0.0, 0.08, xInBar) * (1.0 - smoothstep(0.92, 1.0, xInBar));
    float bar    = step(1.0 - height, input.uv.y) * column;

    float3 col = BarColour.rgb * bar;

    // The beat pulse is already a decaying envelope; no smoothing needed here.
    col += BarColour.rgb * BeatIn * 0.15;

    return float4(col, BarColour.a);
}
```

With no audio open every band reads 0 and every texel of the spectrum is 0, so this draws
black rather than misbehaving. That is the correct idle state for an audio shader: silence
should look like silence, and a shader that needs to prove it is running can lean on `time`
instead.
