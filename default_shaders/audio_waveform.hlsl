/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "WaveRms",       "TYPE": "audio", "BAND": "rms",  "LABEL": "RMS" },
    { "NAME": "WaveBeat",      "TYPE": "audio", "BAND": "beat", "LABEL": "Beat" },
    { "NAME": "WaveHeight",    "TYPE": "float", "DEFAULT": 0.30, "MIN": 0.01, "MAX": 1.5,  "LABEL": "Wave Height" },
    { "NAME": "WaveThickness", "TYPE": "float", "DEFAULT": 0.008,"MIN": 0.001,"MAX": 0.05, "LABEL": "Thickness" },
    { "NAME": "WaveColor",     "TYPE": "color", "DEFAULT": [0.2, 0.9, 1.0, 1.0], "LABEL": "Wave Colour" },
    { "NAME": "ShowVideo",     "TYPE": "bool",  "DEFAULT": 1.0,  "LABEL": "Show Video" },
    { "NAME": "GlowIntensity", "TYPE": "float", "DEFAULT": 1.5,  "MIN": 0.5, "MAX": 6.0, "LABEL": "Glow Intensity" }
  ]
}*/

// ISF packing:
// WaveHeight    offset 0
// WaveThickness offset 1
// (holes 2,3)
// WaveColor     offset 4 (color, aligned to mult-of-4)
// ShowVideo     offset 8
// GlowIntensity offset 9

Texture2D videoTexture  : register(t0);
SamplerState videoSampler : register(s0);
Texture2D noiseTexture  : register(t1);
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

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    float4 vid = ShowVideo ? videoTexture.Sample(videoSampler, uv) : float4(0,0,0,1);

    // Per-column amplitude from the spectrum texture.
    float mag = spectrumTexture.Sample(videoSampler, float2(uv.x, 0.5)).r;

    // Wave displaced around y=0.5 by spectrum amplitude.
    float waveY = 0.5 + (mag - 0.5) * WaveHeight * 2.0;

    float dist = abs(uv.y - waveY);

    // Wide glow + sharp core line.
    float glow     = exp(-dist / max(WaveThickness * 6.0, 0.001)) * GlowIntensity;
    float coreLine = (dist < WaveThickness) ? 1.5 : 0.0;
    float lineAmt  = saturate(coreLine + glow * 0.8);

    // Beat flash and RMS brightness.
    float4 wColor = lerp(WaveColor, float4(1,1,1,1), WaveBeat * 0.75);
    float brightness = 0.25 + WaveRms * 1.5;

    return lerp(vid, wColor * brightness, lineAmt * wColor.a);
}
