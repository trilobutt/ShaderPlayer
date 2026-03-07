/*{
    "INPUTS": [
        { "NAME": "PixelSize",  "LABEL": "Pixel Size",      "TYPE": "float", "DEFAULT": 16.0, "MIN": 2.0, "MAX": 64.0, "STEP": 1.0 },
        { "NAME": "BarMargin",  "LABEL": "Bar Margin",      "TYPE": "float", "DEFAULT": 0.05, "MIN": 0.0, "MAX": 0.3  },
        { "NAME": "BgColor",    "LABEL": "Background",      "TYPE": "color", "DEFAULT": [0.70, 0.74, 0.73, 1.0] }
    ]
}*/

Texture2D videoTexture : register(t0);
Texture2D noiseTexture : register(t1);
SamplerState videoSampler : register(s0);
SamplerState noiseSampler : register(s1);

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
    float2 uv  : TEXCOORD0;
};

// ISF offsets: PixelSize→custom[0].x, BarMargin→custom[0].y, BgColor→custom[1]

float4 main(PS_INPUT input) : SV_TARGET {
    float2 normPxSize = float2(PixelSize, PixelSize) / resolution;
    float2 uvPixel    = normPxSize * floor(input.uv / normPxSize);

    float4 col  = videoTexture.Sample(videoSampler, uvPixel);
    float  luma = dot(float3(0.2126, 0.7152, 0.0722), col.rgb);

    float2 cellUv   = frac(input.uv / normPxSize);
    float  barWidth = saturate(1.0 - luma);  // darker pixel → wider bar

    bool inBar = (cellUv.y > BarMargin)
              && (cellUv.y < (1.0 - BarMargin))
              && (cellUv.x < barWidth);

    return inBar ? float4(0.0, 0.0, 0.0, 1.0) : BgColor;
}
