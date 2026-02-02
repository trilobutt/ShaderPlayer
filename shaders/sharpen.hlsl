// Sharpen (Unsharp Mask)
// Enhances edge detail

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
    float2 texelSize = 1.0 / videoResolution;
    float2 uv = input.uv;
    
    // Sharpening strength
    float strength = 1.0;
    
    // 3x3 sharpen kernel
    float4 center = videoTexture.Sample(videoSampler, uv) * (1.0 + 4.0 * strength);
    float4 top = videoTexture.Sample(videoSampler, uv + float2(0, -texelSize.y)) * -strength;
    float4 bottom = videoTexture.Sample(videoSampler, uv + float2(0, texelSize.y)) * -strength;
    float4 left = videoTexture.Sample(videoSampler, uv + float2(-texelSize.x, 0)) * -strength;
    float4 right = videoTexture.Sample(videoSampler, uv + float2(texelSize.x, 0)) * -strength;
    
    float4 result = center + top + bottom + left + right;
    result.rgb = saturate(result.rgb);
    result.a = 1.0;
    
    return result;
}
