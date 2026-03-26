/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "PulseBass",     "TYPE": "audio", "BAND": "bass",  "LABEL": "Bass" },
    { "NAME": "PulseBeat",     "TYPE": "audio", "BAND": "beat",  "LABEL": "Beat" },
    { "NAME": "PulseStrength", "TYPE": "float", "DEFAULT": 0.06, "MIN": 0.0, "MAX": 0.3,  "LABEL": "Pulse Strength" },
    { "NAME": "ChromaAmt",     "TYPE": "float", "DEFAULT": 0.018,"MIN": 0.0, "MAX": 0.12, "LABEL": "Chroma Shift" },
    { "NAME": "BeatFlash",     "TYPE": "float", "DEFAULT": 0.5,  "MIN": 0.0, "MAX": 1.0,  "LABEL": "Beat Flash" },
    { "NAME": "FlashColor",    "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0],          "LABEL": "Flash Colour" }
  ]
}*/

// ISF packing:
// PulseStrength offset 0
// ChromaAmt     offset 1
// BeatFlash     offset 2
// (hole 3)
// FlashColor    offset 4 (color, aligned)

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
    float2 uv    = input.uv;
    float2 centre = float2(0.5, 0.5);
    float2 dir   = uv - centre;

    // Radial expansion on bass hits.
    float expand = PulseBass * PulseStrength;
    float2 uvWarp = uv + dir * expand;

    // Chromatic aberration scaled by bass.
    float shift = ChromaAmt * (1.0 + PulseBass * 8.0);
    float2 uvR = uvWarp + dir * shift;
    float2 uvB = uvWarp - dir * shift;

    float r = videoTexture.Sample(videoSampler, uvR).r;
    float g = videoTexture.Sample(videoSampler, uvWarp).g;
    float b = videoTexture.Sample(videoSampler, uvB).b;

    float4 col = float4(r, g, b, 1.0);

    // Beat flash with user colour.
    col.rgb = lerp(col.rgb, FlashColor.rgb, PulseBeat * BeatFlash);

    return col;
}
