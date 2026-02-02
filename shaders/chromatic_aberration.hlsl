// Chromatic Aberration
// Simulates lens dispersion by offsetting colour channels

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
    float2 uv = input.uv;
    float2 center = uv - 0.5;
    
    // Aberration strength (adjust as needed)
    float strength = 0.003;
    
    // Distance from center affects aberration
    float dist = length(center);
    float2 dir = normalize(center);
    
    // Sample each channel with offset
    float r = videoTexture.Sample(videoSampler, uv + dir * dist * strength).r;
    float g = videoTexture.Sample(videoSampler, uv).g;
    float b = videoTexture.Sample(videoSampler, uv - dir * dist * strength).b;
    
    return float4(r, g, b, 1.0);
}
