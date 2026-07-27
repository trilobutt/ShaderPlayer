/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        { "NAME": "ColourA",     "LABEL": "Colour A",     "TYPE": "color", "DEFAULT": [1.0, 0.25, 0.35, 1.0] },
        { "NAME": "ColourB",     "LABEL": "Colour B",     "TYPE": "color", "DEFAULT": [0.2, 0.85, 1.0,  1.0] },
        { "NAME": "ColourC",     "LABEL": "Colour C",     "TYPE": "color", "DEFAULT": [1.0, 0.9,  0.3,  1.0] },
        { "NAME": "Tint",        "LABEL": "Tint",         "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0] },
        { "NAME": "Speed",       "LABEL": "Speed",        "TYPE": "float", "DEFAULT": 1.0,  "MIN": 0.0, "MAX": 4.0,  "STEP": 0.05 },
        { "NAME": "Scale",       "LABEL": "Scale",        "TYPE": "float", "DEFAULT": 3.0,  "MIN": 0.5, "MAX": 10.0, "STEP": 0.1  },
        { "NAME": "Contrast",    "LABEL": "Contrast",     "TYPE": "float", "DEFAULT": 1.0,  "MIN": 0.5, "MAX": 3.0,  "STEP": 0.05 },
        { "NAME": "AudioAmount", "LABEL": "Audio Amount", "TYPE": "float", "DEFAULT": 0.6,  "MIN": 0.0, "MAX": 1.0,  "STEP": 0.01 },
        { "NAME": "BassIn",      "LABEL": "Bass",         "TYPE": "audio", "BAND": "bass" },
        { "NAME": "MidIn",       "LABEL": "Mid",          "TYPE": "audio", "BAND": "mid"  },
        { "NAME": "BeatIn",      "LABEL": "Beat",         "TYPE": "audio", "BAND": "beat" }
    ]
}*/

// Plasma — pure generative shader; does not require video input.
// videoTexture is available but unused.
//
// The palette is a cyclic three-stop ramp (A → B → C → A) indexed by the plasma
// field, with Tint applied over the result as an overall multiplier.

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
    float4 custom[8];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    // --- Audio modulation ---
    // Mid drives the wave speed, bass swells the spatial scale, beats punch the
    // contrast and exposure. AudioAmount 0 = the original static plasma.
    float aBass = BassIn * AudioAmount;
    float aMid  = MidIn  * AudioAmount;
    float aBeat = BeatIn * AudioAmount;

    float t = time * (Speed + aMid * 2.5);

    // Aspect-correct UV centred on (0,0)
    float2 aspect = float2(resolution.x / resolution.y, 1.0);
    float2 p = (input.uv - 0.5) * aspect * (Scale * (1.0 + aBass * 0.8));

    // Classic plasma: sum of several sinusoidal waves
    float v = sin(p.x + t);
    v += sin(p.y + t * 0.7);
    v += sin((p.x + p.y) * 0.7 + t * 0.5);
    float r = length(p);
    v += sin(r * 2.0 - t * 0.9);

    // Map -4..4 → 0..1, apply contrast
    float s = (v / 4.0 + 0.5);
    s = saturate((s - 0.5) * (Contrast + aBeat * 1.5) + 0.5);

    // Cyclic three-stop palette: A → B → C → A
    float seg  = frac(s) * 3.0;
    int   idx  = int(seg);
    float f    = frac(seg);
    float3 col = (idx == 0) ? lerp(ColourA.rgb, ColourB.rgb, f)
               : (idx == 1) ? lerp(ColourB.rgb, ColourC.rgb, f)
                            : lerp(ColourC.rgb, ColourA.rgb, f);

    col *= Tint.rgb * (1.0 + aBeat * 0.6);

    return float4(saturate(col), 1.0);
}
