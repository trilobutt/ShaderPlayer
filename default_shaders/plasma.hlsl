/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        { "NAME": "Speed",     "LABEL": "Speed",      "TYPE": "float", "DEFAULT": 1.0,  "MIN": 0.0, "MAX": 4.0,  "STEP": 0.05 },
        { "NAME": "Scale",     "LABEL": "Scale",      "TYPE": "float", "DEFAULT": 3.0,  "MIN": 0.5, "MAX": 10.0, "STEP": 0.1  },
        { "NAME": "Tint",      "LABEL": "Tint",       "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0] },
        { "NAME": "Contrast",  "LABEL": "Contrast",   "TYPE": "float", "DEFAULT": 1.0,  "MIN": 0.5, "MAX": 3.0,  "STEP": 0.05 }
    ]
}*/

// Plasma — pure generative shader; does not require video input.
// videoTexture is available but unused.

Texture2D videoTexture : register(t0);
SamplerState videoSampler : register(s0);

Texture2D noiseTexture : register(t1);
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
    float t = time * Speed;

    // Aspect-correct UV centred on (0,0)
    float2 aspect = float2(resolution.x / resolution.y, 1.0);
    float2 p = (input.uv - 0.5) * aspect * Scale;

    // Classic plasma: sum of several sinusoidal waves
    float v = sin(p.x + t);
    v += sin(p.y + t * 0.7);
    v += sin((p.x + p.y) * 0.7 + t * 0.5);
    float r = length(p);
    v += sin(r * 2.0 - t * 0.9);

    // Map -4..4 → 0..1, apply contrast
    float s = (v / 4.0 + 0.5);
    s = saturate((s - 0.5) * Contrast + 0.5);

    // Colour via shifted hue channels
    float3 col = float3(
        sin(s * 3.14159 * 2.0),
        sin(s * 3.14159 * 2.0 + 2.094),
        sin(s * 3.14159 * 2.0 + 4.189)
    );
    col = col * 0.5 + 0.5;
    col *= Tint.rgb;

    return float4(col, 1.0);
}
