/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "BarBass",  "TYPE": "audio", "BAND": "bass",  "LABEL": "Bass" },
    { "NAME": "BarBeat",  "TYPE": "audio", "BAND": "beat",  "LABEL": "Beat" },
    { "NAME": "BgColor",  "TYPE": "color", "DEFAULT": [0.0, 0.0, 0.06, 1.0], "LABEL": "Background" },
    { "NAME": "BarColor", "TYPE": "color", "DEFAULT": [0.1, 0.7, 1.0,  1.0], "LABEL": "Bar Colour" },
    { "NAME": "Mirror",   "TYPE": "bool",  "DEFAULT": 1.0,  "LABEL": "Mirror" }
  ]
}*/

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

    // Sample the spectrum texture (t3, injected by preamble).
    float mag = spectrumTexture.Sample(videoSampler, float2(xCoord, 0.5)).r;

    // Bar height: bottom-up.
    float barTop = 1.0 - mag;
    float inBar  = (uv.y >= barTop) ? 1.0 : 0.0;

    // Gradient: dim at bottom, bright at top of bar.
    float t = saturate((uv.y - barTop) / max(mag, 0.001));

    // Beat flash: interpolate bar colour toward white.
    float4 col = lerp(BarColor * 0.5, BarColor, t);
    col = lerp(col, float4(1, 1, 1, 1), BarBeat * 0.4);

    float4 bg = BgColor;
    // Faint bass glow on background.
    bg.rgb += BarColor.rgb * BarBass * 0.15;

    return lerp(bg, col, inBar);
}
