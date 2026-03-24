/*{
    "INPUTS": [
        {"NAME": "Strength", "LABEL": "Strength", "TYPE": "float",
         "MIN": 0.0, "MAX": 5.0, "DEFAULT": 1.0}
    ]
}*/

// Sharpen (Unsharp Mask)
// Enhances edge detail via a 3x3 Laplacian kernel
// Strength: 0 = passthrough, higher = more aggressive

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
    float2 texel = 1.0 / videoResolution;
    float2 uv = input.uv;

    float4 c      = videoTexture.Sample(videoSampler, uv) * (1.0 + 4.0 * Strength);
    float4 top    = videoTexture.Sample(videoSampler, uv + float2(0, -texel.y)) * -Strength;
    float4 bottom = videoTexture.Sample(videoSampler, uv + float2(0,  texel.y)) * -Strength;
    float4 left   = videoTexture.Sample(videoSampler, uv + float2(-texel.x, 0)) * -Strength;
    float4 right  = videoTexture.Sample(videoSampler, uv + float2( texel.x, 0)) * -Strength;

    float4 result = c + top + bottom + left + right;
    result.rgb = saturate(result.rgb);
    result.a = 1.0;
    return result;
}
