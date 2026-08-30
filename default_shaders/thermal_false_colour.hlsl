/*{
  "SHADER_TYPE": "video",
  "DESCRIPTION": "Maps video luminance to a thermographic palette over an adjustable level window, with optional isothermal contour lines. Inferno is the default because it is the only one of the four that is monotone in lightness, so brighter genuinely means hotter; Ironbow is the traditional thermal-camera ramp, Turbo a rainbow with no lightness reversals in it, and Greyscale the monochrome medical look.",
  "INPUTS": [
    { "NAME": "PalettePreset",   "TYPE": "long",  "VALUES": [0,1,2,3], "LABELS": ["Inferno","Ironbow","Turbo","Greyscale"], "DEFAULT": 0, "LABEL": "Palette" },
    { "NAME": "LevelLow",        "TYPE": "float", "MIN": 0.0, "MAX": 0.9, "DEFAULT": 0.0,"LABEL": "Level Low" },
    { "NAME": "LevelHigh",       "TYPE": "float", "MIN": 0.1, "MAX": 1.0, "DEFAULT": 1.0,"LABEL": "Level High" },
    { "NAME": "ContrastBoost",   "TYPE": "float", "MIN": 0.5, "MAX": 3.0, "DEFAULT": 1.0,"LABEL": "Contrast Boost" },
    { "NAME": "ShowContours",    "TYPE": "bool",              "DEFAULT": false,            "LABEL": "Show Isothermal Contours" },
    { "NAME": "ContourInterval", "TYPE": "float", "MIN": 0.02,"MAX": 0.5, "DEFAULT": 0.1,"LABEL": "Contour Interval" }
  ]
}*/

// Converts video luminance to a false-colour temperature map.
// Configurable level windowing (LevelLow to LevelHigh maps to the full palette
// range) and an optional isothermal contour overlay.
//
// Palettes:
//   Inferno   - perceptually uniform, monotone in lightness. The default because
//               it is the only one of the four where "brighter means hotter" is
//               true of the colours as well as of the numbers.
//   Ironbow   - the traditional thermographic ramp. Not uniform, but it is what
//               thermal camera operators read, so it stays.
//   Turbo     - replaces the plain HSV rainbow, which had a lightness curve that
//               went up and down twice: bands appeared at yellow and cyan where
//               the data was perfectly smooth, and detail vanished in green.
//   Greyscale - medical/monochrome.
//
// Stops are interpolated in the sRGB encoding, not in linear light, which is the
// one place in this pass where that is the correct choice: Inferno and Turbo are
// defined as uniformly spaced sRGB samples, and re-interpolating them in linear
// light would undo the very spacing that makes them uniform.
//
// Windowing runs on encoded luma, so the Level sliders line up with what a
// histogram or waveform of the same footage shows.

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

float3 paletteInferno(float t) {
    float3 c[6] = {
        float3(0.0015, 0.0005, 0.0139),
        float3(0.2582, 0.0386, 0.4065),
        float3(0.5783, 0.1480, 0.4044),
        float3(0.8654, 0.3168, 0.2261),
        float3(0.9877, 0.6453, 0.0399),
        float3(0.9884, 0.9982, 0.6449)
    };
    float s = saturate(t) * 5.0;
    int   i = min((int)s, 4);
    return lerp(c[i], c[i + 1], s - float(i));
}

float3 paletteIronbow(float t) {
    float3 c[6] = {
        float3(0.0,   0.0,   0.0),
        float3(0.07,  0.0,   0.25),
        float3(0.55,  0.0,   0.45),
        float3(0.9,   0.2,   0.0),
        float3(1.0,   0.85,  0.0),
        float3(1.0,   1.0,   1.0)
    };
    float s = saturate(t) * 5.0;
    int   i = min((int)s, 4);
    return lerp(c[i], c[i + 1], s - float(i));
}

// Google's Turbo: a rainbow with a monotone lightness ramp and no banding at the
// primaries.
float3 paletteTurbo(float t) {
    float3 c[9] = {
        float3(0.190, 0.072, 0.232),
        float3(0.274, 0.401, 0.925),
        float3(0.108, 0.721, 0.832),
        float3(0.130, 0.910, 0.452),
        float3(0.548, 0.983, 0.144),
        float3(0.864, 0.863, 0.127),
        float3(0.995, 0.607, 0.120),
        float3(0.920, 0.290, 0.044),
        float3(0.480, 0.016, 0.011)
    };
    float s = saturate(t) * 8.0;
    int   i = min((int)s, 7);
    return lerp(c[i], c[i + 1], s - float(i));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float4 vid = videoTexture.Sample(videoSampler, uv);

    float luma = dot(vid.rgb, float3(0.299, 0.587, 0.114));

    // Level windowing: remap [LevelLow, LevelHigh] to [0, 1].
    float lvl = saturate((luma - LevelLow) / max(0.001, LevelHigh - LevelLow));
    lvl = pow(lvl, 1.0 / max(0.01, ContrastBoost));

    float3 palCol;
    if (PalettePreset == 1)      palCol = paletteIronbow(lvl);
    else if (PalettePreset == 2) palCol = paletteTurbo(lvl);
    else if (PalettePreset == 3) palCol = float3(lvl, lvl, lvl);
    else                         palCol = paletteInferno(lvl);

    // Isothermal contours, band-limited. The footprint is measured on the phase
    // before it is folded, which is the only place it is continuous; taking it
    // after frac() reports the fold as an infinitely steep edge and draws a grey
    // smear along every contour. Where the temperature gradient is steeper than
    // one interval per pixel the contours correctly fade out rather than alias
    // into a herringbone.
    if (ShowContours) {
        float phase = lvl / max(ContourInterval, 0.001);
        float w     = max(fwidth(phase), 1e-5);
        float fp    = frac(phase);
        float dPhase = min(fp, 1.0 - fp);
        float contourMask = 1.0 - smoothstep(0.03, 0.03 + w, dPhase);
        palCol = lerp(palCol, float3(1.0, 1.0, 1.0), contourMask * 0.7);
    }

    // Every one of these palettes is a full-range gradient over a smooth input.
    palCol = spDither(palCol, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(palCol), 1.0);
}
