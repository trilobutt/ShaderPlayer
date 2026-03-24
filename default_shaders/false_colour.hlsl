/*{
    "INPUTS": [
        {"NAME": "Blend",    "LABEL": "Blend",    "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 1.0},
        {"NAME": "Exposure", "LABEL": "Exposure", "TYPE": "float",
         "MIN": -3.0, "MAX": 3.0, "DEFAULT": 0.0}
    ]
}*/

// False Colour (Exposure Analysis)
// Colour-codes luminance bands for exposure monitoring
// Blend: 0 = original video, 1 = full false colour
// Exposure: stop offset applied before analysis (+/- 3 stops)
//
// Band colours:
//   Purple  — clipped blacks  (< 0.01)
//   Blue    — underexposed    (< 0.15)
//   Cyan    — shadows         (< 0.25)
//   Grey    — midtones        (< 0.65)
//   Yellow  — highlights      (< 0.75)
//   Orange  — overexposed     (< 0.95)
//   Red     — clipped whites  (>= 0.95)

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

float3 FalseColour(float luma) {
    if (luma < 0.01)  return float3(0.5, 0.0, 0.5);   // Purple  — clipped blacks
    if (luma < 0.15)  return float3(0.0, 0.0, 1.0);   // Blue    — underexposed
    if (luma < 0.25)  return float3(0.0, 0.5, 0.5);   // Cyan    — shadows
    if (luma < 0.65)  return float3(0.3, 0.3, 0.3);   // Grey    — midtones
    if (luma < 0.75)  return float3(1.0, 1.0, 0.0);   // Yellow  — highlights
    if (luma < 0.95)  return float3(1.0, 0.5, 0.0);   // Orange  — overexposed
    return                    float3(1.0, 0.0, 0.0);  // Red     — clipped whites
}

float4 main(PS_INPUT input) : SV_TARGET {
    float4 col = videoTexture.Sample(videoSampler, input.uv);

    // Apply exposure offset (stop-based: multiply by 2^stops)
    float3 exposed = col.rgb * pow(2.0, Exposure);

    float luma = dot(saturate(exposed), float3(0.2126, 0.7152, 0.0722));
    float3 fc = FalseColour(luma);

    return float4(lerp(col.rgb, fc, Blend), 1.0);
}
