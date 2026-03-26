/*{
  "SHADER_TYPE": "generative",
  "INPUTS": [
    { "NAME": "CellCount",   "TYPE": "long",  "VALUES": [2,3,4,5,6,8,10,12,16,20], "LABELS": ["2","3","4","5","6","8","10","12","16","20"], "DEFAULT": 8, "LABEL": "Cell Count" },
    { "NAME": "BorderWidth", "TYPE": "float", "MIN": 0.0,  "MAX": 0.04, "DEFAULT": 0.006,         "LABEL": "Border Width" },
    { "NAME": "MotionSpeed", "TYPE": "float", "MIN": 0.0,  "MAX": 2.0,  "DEFAULT": 0.4,           "LABEL": "Motion Speed" },
    { "NAME": "RenderMode",  "TYPE": "long",  "VALUES": [0,1,2], "LABELS": ["Fill","Border","Both"], "DEFAULT": 0, "LABEL": "Render Mode" },
    { "NAME": "BorderColour","TYPE": "color",                            "DEFAULT": [0,0,0,1],     "LABEL": "Border Colour" }
  ]
}*/

// Animated Voronoi diagram. Seeds follow individual sinusoidal orbits.
// Three render modes: cell fill coloured by seed index, border lines only,
// or filled cells with borders composited on top.

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

float3 hsv2rgb(float h, float s, float v) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(h + K.xyz) * 6.0 - K.www);
    return v * lerp(K.xxx, saturate(p - K.xxx), s);
}

// Pseudo-random float in [0,1] from an integer seed.
float hashf(int n) {
    uint u = (uint)n;
    u = u ^ (u >> 16u);
    u = u * 0x45d9f3bu;
    u = u ^ (u >> 16u);
    return float(u) / 4294967295.0;
}

// Animated seed position for seed index i.
float2 seedPos(int i, float spd) {
    float fi    = float(i);
    // Each seed has its own orbit radius, speed, and phase derived from its index.
    float orbitR = 0.18 + hashf(i * 7 + 1) * 0.28;
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

    float d1   = 1e9, d2 = 1e9;
    int   near = 0;

    // Euclidean Voronoi: find nearest and second-nearest seeds.
    [loop]
    for (int i = 0; i < CellCount; ++i) {
        float2 sp  = seedPos(i, MotionSpeed);
        sp.x      *= resolution.x / resolution.y;
        float2 dv  = uvA - sp;
        float  d   = dot(dv, dv);  // squared distance — avoids sqrt for comparison
        if (d < d1) { d2 = d1; d1 = d; near = i; }
        else if (d < d2) { d2 = d; }
    }

    d1 = sqrt(d1);
    d2 = sqrt(d2);

    // Border mask: pixels close to the boundary between two cells.
    float borderMask = 1.0 - smoothstep(0.0, BorderWidth, d2 - d1);

    // Per-cell colour derived from seed index.
    float hue    = hashf(near * 13 + 5);
    float sat    = 0.6 + hashf(near * 7 + 2) * 0.4;
    float3 cellC = hsv2rgb(hue, sat, 0.75 + d1 * 0.5);

    float3 col;
    if (RenderMode == 1) {
        // Border only: dark background, border in BorderColour.
        col = lerp(float3(0.05, 0.05, 0.08), BorderColour.rgb, borderMask);
    } else {
        // Fill with optional border overlay.
        col = cellC;
        if (RenderMode == 2) {
            col = lerp(col, BorderColour.rgb, borderMask);
        }
    }

    return float4(col, 1.0);
}
