/*{
  "SHADER_TYPE": "video",
  "INPUTS": [
    { "NAME": "PalettePreset",   "TYPE": "long",  "MIN": 0,   "MAX": 3,   "DEFAULT": 0,  "LABEL": "Palette (0=Inferno 1=Ironbow 2=Rainbow 3=Greyscale)" },
    { "NAME": "LevelLow",        "TYPE": "float", "MIN": 0.0, "MAX": 0.9, "DEFAULT": 0.0,"LABEL": "Level Low" },
    { "NAME": "LevelHigh",       "TYPE": "float", "MIN": 0.1, "MAX": 1.0, "DEFAULT": 1.0,"LABEL": "Level High" },
    { "NAME": "ContrastBoost",   "TYPE": "float", "MIN": 0.5, "MAX": 3.0, "DEFAULT": 1.0,"LABEL": "Contrast Boost" },
    { "NAME": "ShowContours",    "TYPE": "bool",              "DEFAULT": false,            "LABEL": "Show Isothermal Contours" },
    { "NAME": "ContourInterval", "TYPE": "float", "MIN": 0.02,"MAX": 0.5, "DEFAULT": 0.1,"LABEL": "Contour Interval" }
  ]
}*/

// Converts video luminance to a false-colour temperature map.
// Palette presets: inferno, ironbow, rainbow, medical greyscale.
// Configurable level windowing (LevelLow–LevelHigh maps to full palette range).
// Optional isothermal contour overlay.

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

// All palettes: return RGB for a normalised temperature t ∈ [0,1].

float3 paletteInferno(float t) {
    // Perceptually uniform: black → deep purple → orange → yellow-white
    float3 c0 = float3(0.0,    0.0,    0.015);
    float3 c1 = float3(0.21,   0.015,  0.47);
    float3 c2 = float3(0.61,   0.09,   0.37);
    float3 c3 = float3(0.90,   0.39,   0.08);
    float3 c4 = float3(0.99,   0.83,   0.20);
    float  s  = saturate(t) * 4.0;
    int    i  = (int)s;
    float  f  = frac(s);
    float3 cols[5] = { c0, c1, c2, c3, c4 };
    return lerp(cols[clamp(i, 0, 3)], cols[clamp(i + 1, 0, 4)], f);
}

float3 paletteIronbow(float t) {
    // Ironbow: black → dark blue → purple → red → orange → yellow → white
    float3 c0 = float3(0.0,   0.0,   0.0);
    float3 c1 = float3(0.07,  0.0,   0.25);
    float3 c2 = float3(0.55,  0.0,   0.45);
    float3 c3 = float3(0.9,   0.2,   0.0);
    float3 c4 = float3(1.0,   0.85,  0.0);
    float3 c5 = float3(1.0,   1.0,   1.0);
    float  s  = saturate(t) * 5.0;
    int    i  = (int)s;
    float  f  = frac(s);
    float3 cols[6] = { c0, c1, c2, c3, c4, c5 };
    return lerp(cols[clamp(i, 0, 4)], cols[clamp(i + 1, 0, 5)], f);
}

float3 paletteRainbow(float t) {
    // Classic ROYGBIV wrapped HSV
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(float3(t, t, t) + K.xyz) * 6.0 - K.www);
    return saturate(p - K.xxx);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float4 vid = videoTexture.Sample(videoSampler, uv);

    // Luminance.
    float luma = dot(vid.rgb, float3(0.299, 0.587, 0.114));

    // Level windowing: remap [LevelLow, LevelHigh] to [0, 1].
    float lvl  = saturate((luma - LevelLow) / max(0.001, LevelHigh - LevelLow));

    // Contrast boost (gamma).
    lvl = pow(lvl, 1.0 / max(0.01, ContrastBoost));

    // Apply palette.
    float3 palCol;
    if (PalettePreset == 1)      palCol = paletteIronbow(lvl);
    else if (PalettePreset == 2) palCol = paletteRainbow(lvl);
    else if (PalettePreset == 3) palCol = float3(lvl, lvl, lvl);
    else                         palCol = paletteInferno(lvl);

    // Isothermal contour overlay.
    if (ShowContours) {
        float contourPhase = frac(lvl / ContourInterval);
        float contourLine  = 1.0 - smoothstep(0.0, 0.08, abs(contourPhase - 0.5) - 0.42);
        palCol = lerp(palCol, float3(1.0, 1.0, 1.0), contourLine * 0.7);
    }

    return float4(palCol, 1.0);
}
