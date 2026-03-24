/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "WaveRms",       "TYPE": "audio", "BAND": "rms",  "LABEL": "RMS" },
    { "NAME": "WaveBeat",      "TYPE": "audio", "BAND": "beat", "LABEL": "Beat" },
    { "NAME": "WaveHeight",    "TYPE": "float", "DEFAULT": 0.12, "MIN": 0.01, "MAX": 0.5,  "LABEL": "Wave Height" },
    { "NAME": "WaveThickness", "TYPE": "float", "DEFAULT": 0.004,"MIN": 0.001,"MAX": 0.02, "LABEL": "Thickness" },
    { "NAME": "WaveColor",     "TYPE": "color", "DEFAULT": [0.2, 0.9, 1.0, 1.0], "LABEL": "Wave Colour" },
    { "NAME": "ShowVideo",     "TYPE": "bool",  "DEFAULT": 1.0,  "LABEL": "Show Video" }
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

    float4 vid = ShowVideo ? videoTexture.Sample(videoSampler, uv) : float4(0,0,0,1);

    // Per-column amplitude from the spectrum texture.
    float mag = spectrumTexture.Sample(videoSampler, float2(uv.x, 0.5)).r;

    // The wave sits at y=0.5; mag drives vertical displacement.
    float waveY = 0.5 + (mag - 0.5) * WaveHeight * 2.0;

    // Signed distance from the wave line.
    float dist = abs(uv.y - waveY);

    // Glow falloff.
    float glow     = exp(-dist / max(WaveThickness * 4.0, 0.001));
    float coreLine = (dist < WaveThickness) ? 1.0 : 0.0;
    float lineAmt  = saturate(coreLine + glow * 0.5);

    // Beat flash colour.
    float4 wColor = lerp(WaveColor, float4(1,1,1,1), WaveBeat * 0.5);

    // Composite over video; scale lineAmt brightness by RMS.
    float brightness = 0.4 + WaveRms * 0.6;
    return lerp(vid, wColor * brightness, lineAmt * wColor.a);
}
