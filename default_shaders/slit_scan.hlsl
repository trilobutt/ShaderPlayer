/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "sliceWidth",         "LABEL": "Slice Width",    "TYPE": "float", "MIN": 0.001, "MAX": 0.1,  "DEFAULT": 0.01},
        {"NAME": "slicePos",           "LABEL": "Slice Position", "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.5},
        {"NAME": "scrollAxis",         "LABEL": "Scroll Axis",    "TYPE": "long",
         "VALUES": [0, 1], "LABELS": ["Vertical", "Horizontal"], "DEFAULT": 0},
        {"NAME": "temporalSpread",     "LABEL": "Temporal Spread","TYPE": "float", "MIN": 0.0,  "MAX": 2.0,  "DEFAULT": 0.8},
        {"NAME": "blendWeight",        "LABEL": "Blend",          "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 1.0},
        {"NAME": "colourPalette",      "LABEL": "Colour Map",     "TYPE": "long",
         "VALUES": [0, 1, 2], "LABELS": ["Original", "Heat", "Monochrome"], "DEFAULT": 0},
        {"NAME": "PaletteShift",       "LABEL": "Palette Shift",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.0}
    ]
}*/

// Slit-scan temporal splice approximation.
// Without frame history, each column/row encodes a different time phase via UV
// displacement, replicating the streak-photo effect spatially. The axis
// perpendicular to the scan direction represents time; the scan axis is spatial.
//
// The phase ramp is a sinusoid, not frac(). A sawtooth wraps, and the wrap put a
// hard tear straight across the frame that moved with the clock: the one feature
// in the image that was unmistakably a bug rather than an artefact.
//
// Slice Width now sets the width of the clean band. It was parsed, assigned to an
// unused local and ignored, so the only thing controlling the falloff was a fixed
// exponential.

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

    // Phase encodes temporal offset: pixels far from the scan position have
    // accumulated more "time", so sample the video at a shifted UV to fake history.
    float perp = (scrollAxis == 0) ? uv.y : uv.x;    // temporal axis
    float dist = perp - slicePos;

    float scanOff = sin(SP_TAU * (dist * temporalSpread + time * 0.05)) * 0.5;

    float2 scanUV = uv;
    if (scrollAxis == 0)
        scanUV.y = uv.y + scanOff * 0.5;
    else
        scanUV.x = uv.x + scanOff * 0.5;

    float3 clean = videoTexture.Sample(videoSampler, uv).rgb;
    float3 scan  = videoTexture.Sample(videoSampler, clamp(scanUV, 0.0, 1.0)).rgb;

    // Clean band of half-width sliceWidth, fading into the streak over a quarter
    // of the frame.
    float fadeW = 1.0 - smoothstep(sliceWidth * 0.5, sliceWidth * 0.5 + 0.25, abs(dist));

    // Crossfade in linear light. Two images dissolved on encoded values lose
    // brightness through the middle of the transition, which here is most of the
    // frame.
    float3 lin = lerp(spSrgbToLinear(scan), spSrgbToLinear(clean), fadeW);

    if (colourPalette == 1) {
        // Inigo Quilez cosine palette (his published warm set), phase-shiftable.
        // The piecewise smoothstep ramp this replaces had a flat green plateau
        // through the mid-tones and two visible kinks either side of it.
        float t = saturate(spLuma(lin));
        lin = spSrgbToLinear(saturate(spPalette(t,
                        float3(0.5, 0.5, 0.5),
                        float3(0.5, 0.5, 0.5),
                        float3(1.0, 1.0, 1.0),
                        float3(0.0, 0.10, 0.20) + PaletteShift)));
    } else if (colourPalette == 2) {
        lin = spLuma(lin).xxx;
    }

    float3 orig = spSrgbToLinear(clean);
    lin = lerp(orig, lin, blendWeight);

    float3 col = spLinearToSrgb(lin);

    // The streak is a low-gradient smear over most of the frame, and the palette
    // mode turns luminance into a full-range gradient: both band on 8 bits.
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
