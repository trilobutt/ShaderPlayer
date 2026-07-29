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
        { "NAME": "Detail",      "LABEL": "Detail",       "TYPE": "float", "DEFAULT": 0.35, "MIN": 0.0, "MAX": 1.0,  "STEP": 0.01 },
        { "NAME": "Exposure",    "LABEL": "Exposure",     "TYPE": "float", "DEFAULT": 1.0,  "MIN": 0.1, "MAX": 4.0,  "STEP": 0.05 },
        { "NAME": "VignetteAmt", "LABEL": "Vignette",     "TYPE": "float", "DEFAULT": 0.25, "MIN": 0.0, "MAX": 1.0,  "STEP": 0.01 },
        { "NAME": "AudioAmount", "LABEL": "Audio Amount", "TYPE": "float", "DEFAULT": 0.6,  "MIN": 0.0, "MAX": 1.0,  "STEP": 0.01 },
        { "NAME": "BassIn",      "LABEL": "Bass",         "TYPE": "audio", "BAND": "bass" },
        { "NAME": "MidIn",       "LABEL": "Mid",          "TYPE": "audio", "BAND": "mid"  },
        { "NAME": "BeatIn",      "LABEL": "Beat",         "TYPE": "audio", "BAND": "beat" }
    ]
}*/

// Plasma — pure generative shader; does not require video input.
// videoTexture is available but unused.
//
// The palette is a cyclic three-stop ramp (A -> B -> C -> A) indexed by the plasma
// field, with Tint applied over the result as an overall multiplier. The ramp is
// evaluated in linear light and tonemapped, so the stops stay recognisable while
// the highlights roll off instead of clipping per channel.

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

// Cyclic three-stop ramp. smoothstep on the segment parameter makes the joins
// C1-continuous, so the three stops read as one continuous ramp rather than as
// three straight lines meeting at a kink. Stops must be passed in linear light.
float3 rampCyclic(float s, float3 a, float3 b, float3 c) {
    float seg = frac(s) * 3.0;
    int   idx = int(seg);
    float f   = smoothstep(0.0, 1.0, frac(seg));
    float3 c0 = (idx == 0) ? a : (idx == 1) ? b : c;
    float3 c1 = (idx == 0) ? b : (idx == 1) ? c : a;
    return lerp(c0, c1, f);
}

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

    // Classic plasma: sum of several sinusoidal waves, amplitude 4 total.
    float v = sin(p.x + t);
    v += sin(p.y + t * 0.7);
    v += sin((p.x + p.y) * 0.7 + t * 0.5);
    float r = length(p);
    v += sin(r * 2.0 - t * 0.9);

    // Second octave at ~3x the frequency. Without it the field is four broad
    // lobes and every region between them is flat; the fine wave gives the
    // gradient something to hold at any zoom. Amplitude 3 total, scaled by Detail.
    float v2 = sin(p.x * 2.9 - t * 1.3);
    v2 += sin(p.y * 3.3 + t * 1.1);
    v2 += sin((p.x - p.y) * 2.6 + t * 0.8);
    v += v2 * Detail;

    // Map to 0..1 over the actual amplitude of the sum, then apply contrast.
    float s = v / (2.0 * (4.0 + 3.0 * Detail)) + 0.5;
    s = saturate((s - 0.5) * (Contrast + aBeat * 1.5) + 0.5);

    // Ramp in linear light: an sRGB lerp between saturated stops dips in
    // luminance at the midpoint and reads as a muddy band.
    float3 col = rampCyclic(s,
                            spSrgbToLinear(ColourA.rgb),
                            spSrgbToLinear(ColourB.rgb),
                            spSrgbToLinear(ColourC.rgb));

    col *= spSrgbToLinear(Tint.rgb) * (1.0 + aBeat * 0.6);
    col *= Exposure;

    // Radial falloff to seat the field in the frame rather than letting it run
    // flat to the edges.
    col *= spVignette(input.uv, VignetteAmt, 0.8);

    // Tonemap instead of saturate: hard clipping shifts the hue as brightness
    // rises, because each channel reaches 1.0 at a different field value.
    col = spLinearToSrgb(spTonemapACES(col));

    // The ramp is a smooth gradient across the whole frame — the worst case for
    // 8-bit banding.
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
