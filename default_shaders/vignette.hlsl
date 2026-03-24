/*{
    "INPUTS": [
        {"NAME": "Intensity",      "LABEL": "Intensity",   "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.5},
        {"NAME": "Softness",       "LABEL": "Softness",    "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.4},
        {"NAME": "VignetteColor",  "LABEL": "Colour",      "TYPE": "color",
         "DEFAULT": [0.0, 0.0, 0.0, 1.0]}
    ]
}*/

// Vignette Effect
// Darkens (or tints) the edges of the frame
// Intensity: edge darkness strength
// Softness: how gradual the falloff is
// Colour: vignette tint colour (default black)

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
    float4 col = videoTexture.Sample(videoSampler, input.uv);

    float2 centered = input.uv - 0.5;
    float dist = length(centered);
    float vignette = smoothstep(Softness, Softness + 0.3, dist) * Intensity;

    col.rgb = lerp(col.rgb, VignetteColor.rgb, vignette);
    return col;
}
