/*{
    "INPUTS": [
        {"NAME": "Strength", "LABEL": "Strength", "TYPE": "float",
         "MIN": 0.0, "MAX": 0.1, "DEFAULT": 0.015},
        {"NAME": "Animate",  "LABEL": "Animate",  "TYPE": "bool",
         "DEFAULT": false}
    ]
}*/

// Chromatic Aberration
// Simulates lens dispersion by offsetting colour channels radially.
// Strength: aberration magnitude (0.015 default gives ~15px at corner on 1080p).
// Animate: oscillates strength over time.

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
    float2 center = uv - 0.5;

    float s = Animate ? Strength * (0.5 + 0.5 * sin(time * 2.0)) : Strength;
    float dist = length(center);
    float2 dir = (dist > 0.0001) ? normalize(center) : float2(0, 0);

    // R shifts outward, B shifts inward; shift scales with distance from centre (lens model).
    float2 offset = dir * dist * s;
    float r = videoTexture.Sample(videoSampler, uv + offset).r;
    float g = videoTexture.Sample(videoSampler, uv).g;
    float b = videoTexture.Sample(videoSampler, uv - offset).b;

    return float4(r, g, b, 1.0);
}
