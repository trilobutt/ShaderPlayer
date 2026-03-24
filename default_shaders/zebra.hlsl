/*{
    "INPUTS": [
        {"NAME": "UpperThreshold", "LABEL": "Highlights Threshold", "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.9},
        {"NAME": "LowerThreshold", "LABEL": "Shadows Threshold",    "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.05},
        {"NAME": "StripeWidth",    "LABEL": "Stripe Width",         "TYPE": "float",
         "MIN": 0.005, "MAX": 0.1, "DEFAULT": 0.025},
        {"NAME": "ShowShadows",    "LABEL": "Show Shadow Zebra",    "TYPE": "bool",
         "DEFAULT": false}
    ]
}*/

// Zebra Exposure Checker
// Overlays diagonal stripes on highlights and optionally shadows.
//   Yellow stripes — luminance >= UpperThreshold  (overexposure risk)
//   Blue stripes   — luminance <= LowerThreshold  (only when ShowShadows enabled)
// StripeWidth is relative to frame height so stripe scale is resolution-independent.

Texture2D    videoTexture : register(t0);
SamplerState videoSampler : register(s0);

cbuffer Constants : register(b0) {
    float  time;
    float  padding1;
    float2 resolution;
    float2 videoResolution;
    float2 padding2;
    float4 custom[4];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float4 col  = videoTexture.Sample(videoSampler, input.uv);
    float  luma = dot(col.rgb, float3(0.2126, 0.7152, 0.0722));

    // Diagonal stripe: period scaled so StripeWidth = fraction of frame height.
    float2 px     = input.uv * resolution;
    float  period = resolution.y * StripeWidth;
    bool   bright = frac((px.x + px.y) / period) > 0.5;

    if (luma >= UpperThreshold) {
        // Yellow / black — highlight overexposure warning
        col.rgb = bright ? float3(1.0, 1.0, 0.0) : float3(0.0, 0.0, 0.0);
    } else if (ShowShadows && luma <= LowerThreshold) {
        // Blue / black — shadow underexposure warning
        col.rgb = bright ? float3(0.2, 0.5, 1.0) : float3(0.0, 0.0, 0.0);
    }

    return col;
}
