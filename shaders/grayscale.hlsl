// Grayscale (Rec. 709)
// Converts to grayscale using proper luminance coefficients

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
    float4 color = videoTexture.Sample(videoSampler, input.uv);
    
    // Rec. 709 luminance coefficients
    float3 luminanceCoeff = float3(0.2126, 0.7152, 0.0722);
    float luma = dot(color.rgb, luminanceCoeff);
    
    return float4(luma, luma, luma, color.a);
}
