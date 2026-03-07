/*{
    "INPUTS": [
        {"NAME": "NumSamples", "LABEL": "Sample Rows",  "TYPE": "float",
         "MIN": 32.0, "MAX": 256.0, "DEFAULT": 128.0, "STEP": 1.0},
        {"NAME": "Brightness", "LABEL": "Brightness",   "TYPE": "float",
         "MIN": 1.0, "MAX": 20.0, "DEFAULT": 6.0},
        {"NAME": "BlendVideo", "LABEL": "Video Blend",  "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.0},
        {"NAME": "ShowRGB",    "LABEL": "Show RGB",     "TYPE": "bool",
         "DEFAULT": false}
    ]
}*/

// Luma Waveform Monitor
// X-axis: horizontal position in frame.  Y-axis: luminance 0–100 IRE (top = 100).
// Samples NumSamples evenly-spaced rows per output column to reconstruct waveform density.
// ShowRGB: overlays red, green, blue channel waveforms; luma waveform is shown in white
//          when ShowRGB is off.
// Graticule lines at every 10 IRE; 0 and 100 IRE lines are slightly brighter.
// BlendVideo: 0 = pure waveform on black, 1 = original video.

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

// Gaussian half-width in luma units — gives a ~10-pixel-wide trace at 1080p.
static const float kSigma    = 0.009;
static const float kInvS2    = 1.0 / (2.0 * kSigma * kSigma);

float4 main(PS_INPUT input) : SV_TARGET {
    float  u          = input.uv.x;
    float  targetLuma = 1.0 - input.uv.y;   // top of scope = 1.0 (100 IRE)
    int    nSamples   = int(NumSamples);

    float lumaAcc = 0.0;
    float rAcc    = 0.0;
    float gAcc    = 0.0;
    float bAcc    = 0.0;

    [loop]
    for (int i = 0; i < nSamples; i++) {
        float  v    = (float(i) + 0.5) / float(nSamples);
        float3 rgb  = videoTexture.Sample(videoSampler, float2(u, v)).rgb;
        float  luma = dot(rgb, float3(0.2126, 0.7152, 0.0722));

        float dl = luma - targetLuma;
        lumaAcc += exp(-dl * dl * kInvS2);

        if (ShowRGB) {
            float dr = rgb.r - targetLuma;
            float dg = rgb.g - targetLuma;
            float db = rgb.b - targetLuma;
            rAcc += exp(-dr * dr * kInvS2);
            gAcc += exp(-dg * dg * kInvS2);
            bAcc += exp(-db * db * kInvS2);
        }
    }

    float k = Brightness / float(nSamples);

    float3 waveCol;
    if (ShowRGB) {
        waveCol = float3(rAcc, gAcc, bAcc) * k;
    } else {
        waveCol = float3(lumaAcc, lumaAcc, lumaAcc) * k;
    }

    // --- Graticule: lines at every 10 IRE ---
    // gridX wraps 0..1 ten times per 0-1 luma range.
    float gridX     = frac(targetLuma * 10.0 + 0.0001);
    float threshold = 5.0 / resolution.y;   // ~half-pixel tolerance

    bool onGrid = (gridX < threshold || gridX > 1.0 - threshold);
    if (onGrid) {
        float brightness = 0.12;
        // 0 and 100 IRE are reference black/white — draw them brighter
        if (targetLuma < 2.0 / resolution.y || targetLuma > 1.0 - 2.0 / resolution.y)
            brightness = 0.35;
        waveCol += brightness;
    }

    waveCol = saturate(waveCol);

    // Blend with original video
    float4 videoCol = videoTexture.Sample(videoSampler, input.uv);
    return float4(lerp(waveCol, videoCol.rgb, BlendVideo), 1.0);
}
