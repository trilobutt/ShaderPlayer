// False Colour (Exposure Analysis)
// Shows exposure levels with colour coding:
// - Purple: Clipped blacks
// - Blue: Underexposed
// - Green: Midtones
// - Yellow: Highlights
// - Red: Clipped whites

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
    // Thresholds (adjust for your workflow)
    const float clipBlack = 0.01;
    const float underexposed = 0.15;
    const float shadowLow = 0.25;
    const float midLow = 0.35;
    const float midHigh = 0.65;
    const float highlightLow = 0.75;
    const float overexposed = 0.95;
    const float clipWhite = 0.99;
    
    // Colours
    const float3 clippedBlackCol = float3(0.5, 0.0, 0.5);  // Purple
    const float3 underexposedCol = float3(0.0, 0.0, 1.0);  // Blue
    const float3 shadowCol = float3(0.0, 0.5, 0.5);        // Cyan
    const float3 midCol = float3(0.3, 0.3, 0.3);           // Gray (passthrough-ish)
    const float3 highlightCol = float3(1.0, 1.0, 0.0);     // Yellow
    const float3 overexposedCol = float3(1.0, 0.5, 0.0);   // Orange
    const float3 clippedWhiteCol = float3(1.0, 0.0, 0.0);  // Red
    
    if (luma < clipBlack) return clippedBlackCol;
    if (luma < underexposed) return underexposedCol;
    if (luma < shadowLow) return shadowCol;
    if (luma < midHigh) return midCol;
    if (luma < highlightLow) return highlightCol;
    if (luma < overexposed) return overexposedCol;
    return clippedWhiteCol;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = videoTexture.Sample(videoSampler, input.uv);
    
    // Calculate luminance (Rec. 709)
    float luma = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    
    // Apply false colour
    float3 falseCol = FalseColour(luma);
    
    return float4(falseCol, 1.0);
}
