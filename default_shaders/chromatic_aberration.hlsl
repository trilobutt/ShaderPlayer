/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "Lateral chromatic dispersion: the image is resampled at a slightly different scale per wavelength, so colour fringing is nil at the optical centre and grows toward the corners. Quality sets how many wavelengths are integrated, from a hard three-way R/G/B split with three visible ghost edges to a continuous spectral fringe.",
    "INPUTS": [
        {"NAME": "Strength", "LABEL": "Strength", "TYPE": "float",
         "MIN": 0.0, "MAX": 0.1, "DEFAULT": 0.015},
        {"NAME": "Animate",  "LABEL": "Animate",  "TYPE": "bool",
         "DEFAULT": false},
        {"NAME": "Falloff",  "LABEL": "Falloff",  "TYPE": "float",
         "MIN": 1.0, "MAX": 4.0, "DEFAULT": 2.0},
        {"NAME": "Quality",  "LABEL": "Quality",  "TYPE": "long",
         "VALUES": [3, 9, 17], "LABELS": ["Fast (3 tap)", "Smooth (9 tap)", "Fine (17 tap)"],
         "DEFAULT": 9}
    ]
}*/

// Chromatic Aberration
// Lateral dispersion: the image scale differs per wavelength, so the error grows
// with distance from the optical centre and is zero at the middle of the frame.
//
// Strength: offset at the frame corner. 0.015 is about 20px on a 1080p frame.
// Falloff: radial exponent. A real lens is superlinear (2 is a good match); 1 is
//          the flat ramp a three-tap RGB split implies.
// Quality: taps across the spectrum. 3 is the classic hard R/G/B split and shows
//          three distinct ghost edges at high strength; 9 and up integrate the
//          spectrum properly and give a continuous fringe.

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
    float2 uv : TEXCOORD0;
};

// RGB response of a notional sensor to a wavelength parameter t (0 = blue end,
// 1 = red end). Overlapping gaussians, so adjacent taps share energy and the
// fringe reads as a spectrum rather than as three stacked copies.
float3 spectrumWeight(float t) {
    float3 d = (t - float3(0.85, 0.5, 0.15)) / 0.3;
    return exp(-d * d);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv     = input.uv;
    float2 centre = uv - 0.5;

    float s = Animate ? Strength * (0.5 + 0.5 * sin(time * 2.0)) : Strength;

    float dist = length(centre);
    float2 dir = (dist > 1e-4) ? centre / dist : float2(0, 0);

    // Normalised on the corner distance so Falloff redistributes the aberration
    // across the frame without changing how much of it there is at the corner.
    const float kCorner = 0.70710678;
    float radial = pow(saturate(dist / kCorner), Falloff) * kCorner;
    float2 span  = dir * radial * s;

    int taps = max(Quality, 2);

    float3 acc  = 0.0;
    float3 wsum = 0.0;

    // Accumulate in linear light. Averaging gamma-encoded samples across a
    // high-contrast edge lands well below the true mean radiance, which is what
    // makes naive fringes look grey and dirty instead of coloured.
    [loop]
    for (int i = 0; i < taps; ++i) {
        float t = (float(i) + 0.5) / float(taps);
        float3 w = spectrumWeight(t);
        // Red refracts least and lands outside, blue lands inside.
        float2 off = span * (t * 2.0 - 1.0);
        acc  += spSrgbToLinear(videoTexture.Sample(videoSampler, uv + off).rgb) * w;
        wsum += w;
    }

    float3 col = spLinearToSrgb(acc / max(wsum, 1e-5));

    // The fringe is a low-amplitude gradient over many pixels near the centre of
    // the frame, where the offset is a fraction of a texel.
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
