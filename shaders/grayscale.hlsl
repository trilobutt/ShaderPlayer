/*{
    "INPUTS": [
        {"NAME": "Blend",  "LABEL": "Blend",  "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 1.0},
        {"NAME": "Tint",   "LABEL": "Tint",   "TYPE": "color",
         "DEFAULT": [1.0, 1.0, 1.0, 1.0]}
    ]
}*/

// Grayscale (Rec. 709)
// Converts to grayscale using proper luminance coefficients
// Blend: 0 = full colour, 1 = full grayscale
// Tint: colour tint applied to the grayscale result

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

    // Rec. 709 luminance
    float luma = dot(col.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 grey = float3(luma, luma, luma) * Tint.rgb;

    col.rgb = lerp(col.rgb, grey, Blend);
    return col;
}
