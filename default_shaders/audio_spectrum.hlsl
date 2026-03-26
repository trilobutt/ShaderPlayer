/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "BarBass",    "TYPE": "audio", "BAND": "bass",  "LABEL": "Bass" },
    { "NAME": "BarBeat",    "TYPE": "audio", "BAND": "beat",  "LABEL": "Beat" },
    { "NAME": "BgColor",    "TYPE": "color", "DEFAULT": [0.0, 0.0, 0.06, 1.0], "LABEL": "Background" },
    { "NAME": "BarColor",   "TYPE": "color", "DEFAULT": [0.1, 0.7, 1.0,  1.0], "LABEL": "Bar Colour" },
    { "NAME": "Mirror",     "TYPE": "bool",  "DEFAULT": 1.0,  "LABEL": "Mirror" },
    { "NAME": "BarScale",   "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.1, "MAX": 3.0, "LABEL": "Bar Scale" },
    { "NAME": "PosY",       "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0, "LABEL": "Vertical Position" }
  ]
}*/

// ISF packing:
// BgColor   offset 0  → custom[0]
// BarColor  offset 4  → custom[1]
// Mirror    offset 8  → (custom[2].x > 0.5)
// BarScale  offset 9  → custom[2].y
// PosY      offset 10 → custom[2].z

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
    float  xCoord = Mirror ? abs(uv.x * 2.0 - 1.0) : uv.x;

    // Sample spectrum and apply height scale.
    float mag = spectrumTexture.Sample(videoSampler, float2(xCoord, 0.5)).r * BarScale;

    // Anchor: PosY=0 → bottom, PosY=1 → top
    float anchor = 1.0 - PosY;
    float barTop = anchor - mag;
    float inBar  = (uv.y >= barTop && uv.y <= anchor) ? 1.0 : 0.0;

    // Gradient: dim at bottom, bright at top of bar.
    float t = (mag > 0.001) ? saturate((uv.y - barTop) / max(mag, 0.001)) : 0.0;

    // Beat flash and colour.
    float4 col = lerp(BarColor * 0.4, BarColor, t);
    col = lerp(col, float4(1, 1, 1, 1), BarBeat * 0.55);

    float4 bg = BgColor;
    // Bass glow on background and subtle bass halo.
    bg.rgb += BarColor.rgb * BarBass * 0.25;

    // Glow above each bar.
    float glowDist = max(0.0, barTop - uv.y);
    float glowAmt  = exp(-glowDist * 30.0) * mag * 0.5;
    bg.rgb += BarColor.rgb * glowAmt;

    return lerp(bg, col, inBar);
}
