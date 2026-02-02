// Vignette Effect
// Darkens the edges of the frame

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
    
    // Vignette parameters
    float intensity = 0.5;   // How dark the edges get
    float softness = 0.4;    // How gradual the falloff is
    
    // Calculate vignette
    float2 center = input.uv - 0.5;
    float dist = length(center);
    float vignette = 1.0 - smoothstep(softness, softness + 0.3, dist) * intensity;
    
    color.rgb *= vignette;
    return color;
}
