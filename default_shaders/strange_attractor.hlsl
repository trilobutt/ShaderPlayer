/*{
  "SHADER_TYPE": "generative",
  "INPUTS": [
    { "NAME": "AttractorType", "TYPE": "long",
      "VALUES": [0,1,2,3], "LABELS": ["Lorenz","Rössler","Thomas","Halvorsen"], "DEFAULT": 0, "LABEL": "Attractor" },
    { "NAME": "IterCount",     "TYPE": "long",
      "VALUES": [60,120,180,240,300,400], "LABELS": ["60","120","180","240","300","400"], "DEFAULT": 180, "LABEL": "Iterations" },
    { "NAME": "ViewScale",     "TYPE": "float", "MIN": 0.005,"MAX": 0.08,"DEFAULT": 0.024,"LABEL": "View Scale" },
    { "NAME": "PointBright",   "TYPE": "float", "MIN": 0.2,  "MAX": 6.0, "DEFAULT": 2.5, "LABEL": "Point Brightness" },
    { "NAME": "PointRadius",   "TYPE": "float", "MIN": 0.5,  "MAX": 24.0,"DEFAULT": 5.0, "LABEL": "Point Radius (px)" },
    { "NAME": "RotationSpeed", "TYPE": "float", "MIN": 0.0,  "MAX": 5.0, "DEFAULT": 0.1, "LABEL": "Rotation Speed" },
    { "NAME": "ColourMode",    "TYPE": "long",
      "VALUES": [0,1,2], "LABELS": ["Velocity","Time","Depth"], "DEFAULT": 0, "LABEL": "Colour Mode" },
    { "NAME": "PaletteShift",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0, "DEFAULT": 0.0, "LABEL": "Palette Shift" },
    { "NAME": "Exposure",      "TYPE": "float", "MIN": 0.1,  "MAX": 4.0, "DEFAULT": 1.0, "LABEL": "Exposure" },
    { "NAME": "VignetteAmt",   "TYPE": "float", "MIN": 0.0,  "MAX": 1.0, "DEFAULT": 0.3, "LABEL": "Vignette" },
    { "NAME": "AudioAmount",   "TYPE": "float", "MIN": 0.0,  "MAX": 1.0, "DEFAULT": 0.6, "LABEL": "Audio Amount" },
    { "NAME": "BassIn",        "TYPE": "audio", "BAND": "bass", "LABEL": "Bass" },
    { "NAME": "MidIn",         "TYPE": "audio", "BAND": "mid",  "LABEL": "Mid" },
    { "NAME": "BeatIn",        "TYPE": "audio", "BAND": "beat", "LABEL": "Beat" }
  ]
}*/

// Chaotic ODE integration rendered as a per-pixel density accumulator.
// Each pixel is seeded at a phase-space position matching its screen coordinate.
// The trajectory is integrated forward; the pixel accumulates the emission of
// every trajectory point that passes near it. Attractor selectable: Lorenz
// (xz butterfly), Rössler, Thomas, Halvorsen.
//
// Emission is inverse-square in pixels, not a Gaussian in UV. Two consequences:
// the point kernel is a fixed size on screen at any output resolution, and the
// accumulated value is unbounded, so a dense fold of the attractor genuinely
// blows past white and blooms once tanh rolls it off, which is the whole look.
// Colour is accumulated per sample rather than averaged at the end, so where a
// fast branch crosses a slow one the two hues add instead of cancelling.

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

#define PI 3.14159265358979

// Cool-to-hot cosine palette. Replaces the raw HSV sweep, which ran through a
// band of near-identical yellows where the velocity range is densest.
float3 attractorPalette(float t) {
    return max(spPalette(t + PaletteShift,
                         float3(0.42, 0.40, 0.48),
                         float3(0.45, 0.40, 0.42),
                         float3(1.0,  1.0,  1.0),
                         float3(0.55, 0.35, 0.10)), 0.0);
}

float3 lorenzDeriv(float3 p) {
    return float3(10.0 * (p.y - p.x),
                  p.x * (28.0 - p.z) - p.y,
                  p.x * p.y - (8.0 / 3.0) * p.z);
}
float3 rosslerDeriv(float3 p) {
    return float3(-p.y - p.z,
                   p.x + 0.2 * p.y,
                   0.2 + p.z * (p.x - 5.7));
}
float3 thomasDeriv(float3 p) {
    return float3(sin(p.y) - 0.208186 * p.x,
                  sin(p.z) - 0.208186 * p.y,
                  sin(p.x) - 0.208186 * p.z);
}
float3 halvorsenDeriv(float3 p) {
    return float3(-1.4 * p.x - 4.0 * p.y - 4.0 * p.z - p.y * p.y,
                  -1.4 * p.y - 4.0 * p.z - 4.0 * p.x - p.z * p.z,
                  -1.4 * p.z - 4.0 * p.x - 4.0 * p.y - p.x * p.x);
}

float3 getDeriv(float3 p) {
    if (AttractorType == 1) return rosslerDeriv(p);
    if (AttractorType == 2) return thomasDeriv(p);
    if (AttractorType == 3) return halvorsenDeriv(p);
    return lorenzDeriv(p);
}

// Attractor-specific coordinate centres and scales for display.
// Returns: float3(xCentre, yCentre_or_zCentre, viewScale_mult)
float3 attractorConfig() {
    // Lorenz: project in xz plane, centred at (0, 25)
    if (AttractorType == 0) return float3(0.0, 25.0, 1.0);
    // Rössler: project in xy plane, centred at (0, 0)
    if (AttractorType == 1) return float3(0.0, 0.0,  0.6);
    // Thomas: symmetric, centre at (0, 0)
    if (AttractorType == 2) return float3(0.0, 0.0,  0.25);
    // Halvorsen: centre at (0, 0)
    return float3(0.0, 0.0, 0.15);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // --- Audio modulation ---
    // Mid spins the camera, bass zooms the phase-space view, beats brighten the
    // trajectory density. AudioAmount 0 = the unmodulated attractor.
    float aBass = BassIn * AudioAmount;
    float aMid  = MidIn  * AudioAmount;
    float aBeat = BeatIn * AudioAmount;

    // Camera rotation angle (slow drift to expose 3D structure).
    float camAngle = time * (RotationSpeed + aMid * 1.6);
    float cosA     = cos(camAngle);
    float sinA     = sin(camAngle);

    float3 cfg = attractorConfig();

    // Map pixel UV to a phase-space starting position.
    // X and Z (or X and Y for Rössler) span the visible range.
    float viewScale = ViewScale * (1.0 + aBass * 0.7);
    float halfRange = 1.0 / max(viewScale, 0.001) * 0.5;
    float ax = (uv.x - 0.5) * halfRange * 2.0;
    float az = (uv.y - 0.5) * halfRange * 2.0 + cfg.y;

    float3 p;
    if (AttractorType == 0) {
        p = float3(ax, 0.0, az);         // Lorenz xz seed (y=0)
    } else if (AttractorType == 1) {
        p = float3(ax, az - cfg.y, 0.5); // Rössler xy seed
    } else if (AttractorType == 2) {
        p = float3(ax, az - cfg.y, 1.0); // Thomas
    } else {
        p = float3(ax, az - cfg.y, 1.0); // Halvorsen
    }

    float dt = 0.01;
    float3 colAcc = float3(0.0, 0.0, 0.0);
    // Kernel radius squared, in pixels. Bounding the core is what keeps a direct
    // hit finite without flattening the tail into a disc of constant brightness.
    float  pr2 = max(PointRadius * PointRadius, 0.25);

    [loop]
    for (int i = 0; i < IterCount; ++i) {
        float3 dp = getDeriv(p);
        p        += dp * dt;

        // Project attractor point onto the 2D screen plane.
        // For Lorenz: rotate in the xz plane; for others: rotate in xy.
        float projA, projB;
        if (AttractorType == 0) {
            float xC   = p.x;
            float zC   = p.z - cfg.y;
            projA = xC * cosA - zC * sinA;
            projB = xC * sinA + zC * cosA;
        } else {
            projA = p.x * cosA - p.y * sinA;
            projB = p.x * sinA + p.y * cosA;
        }

        // Screen UV of this trajectory point, then distance in pixels.
        float2 sUV  = float2(projA, projB) * viewScale + 0.5;
        float2 dPx  = (uv - sUV) * resolution;
        float  dist2 = dot(dPx, dPx);
        float w     = pr2 / (dist2 + pr2);

        float velMag = length(dp);
        float cParam = (ColourMode == 0) ? saturate(velMag / 25.0)
                     : (ColourMode == 1) ? float(i) / float(IterCount)
                     : (AttractorType == 0) ? saturate(p.z / 50.0)
                     :                        saturate((p.z + 5.0) / 10.0);

        colAcc += attractorPalette(cParam * 0.65 + 0.05) * w;
    }

    float3 col = colAcc * PointBright * 0.08 * (1.0 + aBeat * 1.5) * Exposure;
    col *= spVignette(uv, VignetteAmt, 0.85);

    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
