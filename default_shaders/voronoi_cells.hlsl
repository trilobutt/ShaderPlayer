/*{
  "SHADER_TYPE": "generative",
  "INPUTS": [
    { "NAME": "CellCount",   "TYPE": "long",  "VALUES": [2,3,4,5,6,8,10,12,16,20], "LABELS": ["2","3","4","5","6","8","10","12","16","20"], "DEFAULT": 8, "LABEL": "Cell Count" },
    { "NAME": "BorderWidth", "TYPE": "float", "MIN": 0.0,  "MAX": 0.04, "DEFAULT": 0.006,         "LABEL": "Border Width" },
    { "NAME": "MotionSpeed", "TYPE": "float", "MIN": 0.0,  "MAX": 2.0,  "DEFAULT": 0.4,           "LABEL": "Motion Speed" },
    { "NAME": "RenderMode",  "TYPE": "long",  "VALUES": [0,1,2], "LABELS": ["Fill","Border","Both"], "DEFAULT": 0, "LABEL": "Render Mode" },
    { "NAME": "BorderColour","TYPE": "color",                            "DEFAULT": [0,0,0,1],     "LABEL": "Border Colour" },
    { "NAME": "CellSoftness","TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.25,          "LABEL": "Cell Softness" },
    { "NAME": "PaletteShift","TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.0,           "LABEL": "Palette Shift" },
    { "NAME": "SeedGlow",    "TYPE": "float", "MIN": 0.0,  "MAX": 3.0,  "DEFAULT": 0.6,           "LABEL": "Seed Glow" },
    { "NAME": "Exposure",    "TYPE": "float", "MIN": 0.1,  "MAX": 4.0,  "DEFAULT": 1.0,           "LABEL": "Exposure" },
    { "NAME": "VignetteAmt", "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.25,          "LABEL": "Vignette" },
    { "NAME": "AudioAmount", "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.6,           "LABEL": "Audio Amount" },
    { "NAME": "BassIn",      "TYPE": "audio", "BAND": "bass", "LABEL": "Bass" },
    { "NAME": "HighIn",      "TYPE": "audio", "BAND": "high", "LABEL": "Treble" },
    { "NAME": "BeatIn",      "TYPE": "audio", "BAND": "beat", "LABEL": "Beat" }
  ]
}*/

// Animated Voronoi diagram. Seeds follow individual sinusoidal orbits.
// Three render modes: cell fill coloured by seed index, border lines only,
// or filled cells with borders composited on top.
//
// Colour comes from an IQ cosine palette indexed by a per-seed hash, so the set
// of cell colours is a coherent ramp rather than the arbitrary rainbow a random
// hue produces. Everything below is linear light until the final encode.

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

// Pseudo-random float in [0,1] from an integer seed.
float hashf(int n) {
    uint u = (uint)n;
    u = u ^ (u >> 16u);
    u = u * 0x45d9f3bu;
    u = u ^ (u >> 16u);
    return float(u) / 4294967295.0;
}

// Per-cell colour from seed index, in linear light. Shared by the nearest and
// second-nearest lookups so the boundary blend interpolates between two real
// cell colours.
float3 cellColour(int i, float d) {
    float t = hashf(i * 13 + 5) + PaletteShift;
    float3 base = spPalette(t, float3(0.48, 0.44, 0.46),
                               float3(0.45, 0.42, 0.44),
                               float3(1.0,  1.0,  1.0),
                               float3(0.0,  0.28, 0.58));
    // Distance shading: cells fall off toward their own boundary, which is what
    // stops a flat fill from reading as a paper cut-out.
    return max(base, 0.0) * (0.7 + d * 0.6);
}

// Animated seed position for seed index i.
float2 seedPos(int i, float spd, float orbitScale) {
    float fi    = float(i);
    // Each seed has its own orbit radius, speed, and phase derived from its index.
    float orbitR = (0.18 + hashf(i * 7 + 1) * 0.28) * orbitScale;
    float phase  = hashf(i * 3 + 2) * 6.2832;
    float ospd   = spd * (0.4 + hashf(i * 5 + 3) * 0.8);
    float ox     = 0.5 + orbitR * cos(time * ospd + phase);
    float oy     = 0.5 + orbitR * sin(time * ospd * 0.73 + phase * 1.3);
    return float2(ox, oy);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;
    // Correct aspect so cells are spatially isotropic.
    float2 uvA = uv;
    uvA.x     *= resolution.x / resolution.y;

    // --- Audio modulation ---
    // Bass swings the seeds out along wider orbits, treble thickens the borders,
    // beats lift the cell brightness. AudioAmount 0 = the unmodulated diagram.
    float aBass = BassIn * AudioAmount;
    float aHigh = HighIn * AudioAmount;
    float aBeat = BeatIn * AudioAmount;

    float d1   = 1e9, d2 = 1e9;
    int   near = 0, near2 = 0;

    float orbitScale = 1.0 + aBass * 0.8;
    float speed      = MotionSpeed * (1.0 + aBass * 1.5);

    // Euclidean Voronoi: find nearest and second-nearest seeds.
    [loop]
    for (int i = 0; i < CellCount; ++i) {
        float2 sp  = seedPos(i, speed, orbitScale);
        sp.x      *= resolution.x / resolution.y;
        float2 dv  = uvA - sp;
        float  d   = dot(dv, dv);  // squared distance, avoids sqrt for comparison
        if (d < d1) { d2 = d1; d1 = d; near2 = near; near = i; }
        else if (d < d2) { d2 = d; near2 = i; }
    }

    d1 = sqrt(d1);
    d2 = sqrt(d2);
    float gap = d2 - d1;   // 0 exactly on the bisector between two cells

    // Border mask, filtered over the screen footprint of `gap` so the line keeps
    // its apparent width at any resolution instead of aliasing to a dotted edge.
    float bw = BorderWidth * (1.0 + aHigh * 2.0);
    float borderMask = spAALine(gap, bw);

    // Cell fill. Picking the nearest seed's colour outright makes the cell boundary a
    // hard index switch with no transition, which is what made borderless cells look
    // stair-stepped. Blending toward the second-nearest cell across a band that is at
    // least one pixel wide resolves the boundary smoothly at any resolution.
    float blendW = max(fwidth(gap) * 1.5, 1e-6) + CellSoftness * 0.06;
    float w      = saturate(gap / blendW) * 0.5 + 0.5;   // 0.5 on the bisector
    float3 cellC = lerp(cellColour(near2, d2), cellColour(near, d1), w);
    cellC *= 1.0 + aBeat * 0.7;

    float3 col;
    if (RenderMode == 1) {
        // Border only: dark background, border in BorderColour.
        col = lerp(float3(0.003, 0.003, 0.006), spSrgbToLinear(BorderColour.rgb), borderMask);
    } else {
        // Fill with optional border overlay.
        col = cellC;
        if (RenderMode == 2) {
            col = lerp(col, spSrgbToLinear(BorderColour.rgb), borderMask);
        }
    }

    // Inverse-distance glow at each seed. Accumulating 1/d in linear light and
    // letting the tonemap roll it off gives a real falloff with a hot core; a
    // pow() ramp on a saturated colour cannot produce the same core at all.
    float glow = SeedGlow * 0.012 / max(d1, 0.005);
    col += cellColour(near, 1.0) * glow;

    col *= Exposure;
    col *= spVignette(uv, VignetteAmt, 0.8);

    // tanh rather than ACES: the glow term is unbounded, and tanh holds colour
    // through the very bright cores where ACES desaturates them to white.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
