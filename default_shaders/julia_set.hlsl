/*{
    "DESCRIPTION": "Julia set fractal with smooth colouring and optional animation of the C parameter",
    "SHADER_TYPE": "generative",
    "INPUTS": [
        { "NAME": "CX",          "LABEL": "C Real",            "TYPE": "float", "DEFAULT": -0.7269, "MIN": -1.0, "MAX": 0.5,   "STEP": 0.001 },
        { "NAME": "CY",          "LABEL": "C Imaginary",       "TYPE": "float", "DEFAULT":  0.1889, "MIN": -0.9, "MAX": 0.9,   "STEP": 0.001 },
        { "NAME": "Zoom",        "LABEL": "Zoom",              "TYPE": "float", "DEFAULT": 1.0,     "MIN": 1.0,  "MAX": 400.0, "STEP": 0.05  },
        { "NAME": "MaxIter",     "LABEL": "Max Iterations",    "TYPE": "long",
          "VALUES": [16,32,64,128,256,512], "LABELS": ["16","32","64","128","256","512"], "DEFAULT": 128 },
        { "NAME": "ColourCycle", "LABEL": "Colour Cycle Speed","TYPE": "float", "DEFAULT": 0.1,     "MIN": 0.0,  "MAX": 2.0,   "STEP": 0.01  },
        { "NAME": "AnimateC",    "LABEL": "Animate C",         "TYPE": "bool",  "DEFAULT": 0                                                 },
        { "NAME": "AudioAmount", "LABEL": "Audio Amount",      "TYPE": "float", "DEFAULT": 0.5,     "MIN": 0.0,  "MAX": 1.0,   "STEP": 0.01  },
        { "NAME": "InnerColour", "LABEL": "Inner Colour",      "TYPE": "color", "DEFAULT": [0.05, 0.05, 0.35, 1.0]                           },
        { "NAME": "OuterColour", "LABEL": "Outer Colour",      "TYPE": "color", "DEFAULT": [1.0,  0.75, 0.1,  1.0]                           },
        { "NAME": "Centre",      "LABEL": "Zoom Centre",       "TYPE": "point2d","DEFAULT": [0.0, 0.0], "MIN": -1.5, "MAX": 1.5             },
        { "NAME": "BassIn",      "LABEL": "Bass",              "TYPE": "audio", "BAND": "bass" },
        { "NAME": "HighIn",      "LABEL": "Treble",            "TYPE": "audio", "BAND": "high" },
        { "NAME": "BeatIn",      "LABEL": "Beat",              "TYPE": "audio", "BAND": "beat" }
    ]
}*/

// Parameter ranges are chosen so the fractal is always the subject of the frame:
//
// C Real / C Imaginary are restricted to the neighbourhood of the Mandelbrot set
// (roughly |c| < 1). Outside that region the Julia set degenerates into Fatou dust —
// a sparse scatter with no visible structure — so those values are excluded.
//
// Zoom is a magnification factor: 1.0 frames the whole set (|z| <= 2 bound) with a
// small margin and no more. Values below 1 are excluded because they pull the camera
// back far enough to show the circular escape-time banding that surrounds the set,
// which is what put a visible "outer circle" in frame. Zoom Centre picks the point
// magnification converges on, so deep zooms land on structure rather than the origin.

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
    // Bass nudges C along the cardioid boundary (the set morphs on the beat), treble
    // drives the colour cycle, beats lift the exposure. AudioAmount 0 = static fractal.
    float aBass = BassIn * AudioAmount;
    float aHigh = HighIn * AudioAmount;
    float aBeat = BeatIn * AudioAmount;

    float cx = CX;
    float cy = CY;

    // Optional animated C parameter — Douady rabbit neighbourhood orbit
    if (AnimateC) {
        cx = cos(time * 0.13) * 0.7885;
        cy = sin(time * 0.17) * 0.7885;
    }

    // Bass perturbs C perpendicular to the origin ray, which morphs the filaments
    // without leaving the connected region.
    float cLen = max(length(float2(cx, cy)), 1e-4);
    float2 perp = float2(-cy, cx) / cLen;
    cx += perp.x * aBass * 0.12;
    cy += perp.y * aBass * 0.12;

    // Aspect-correct UV mapped to the complex plane.
    // halfHeight 1.35 at Zoom 1 frames the whole |z| <= 2 attractor with a small margin.
    float halfHeight = 1.35 / max(Zoom, 1.0);
    float2 p = (input.uv - 0.5) * float2(resolution.x / resolution.y, 1.0)
             * (halfHeight * 2.0) + Centre;

    float2 z = p;
    int iterN = 0;
    bool escaped = false;

    // Deep zooms need more iterations to resolve the boundary; scale the budget with
    // magnification so the detail the zoom exposes actually gets computed.
    int iterBudget = min(int(MaxIter * (1.0 + log2(max(Zoom, 1.0)) * 0.35)), 1024);

    [loop]
    for (int i = 0; i < iterBudget; ++i) {
        // z = z^2 + c
        float zx2 = z.x * z.x;
        float zy2 = z.y * z.y;
        if (zx2 + zy2 > 65536.0) {
            iterN = i;
            escaped = true;
            break;
        }
        float newX = zx2 - zy2 + cx;
        float newY = 2.0 * z.x * z.y + cy;
        z = float2(newX, newY);
        iterN = i + 1;
    }

    if (!escaped) {
        return float4(InnerColour.rgb * (0.15 + aBeat * 0.5), 1.0);
    }

    // Smooth escape colouring (Hubbard-Douady potential normalisation)
    float logZn = log(dot(z, z)) * 0.5;
    float nu = log(logZn / 0.6931472) / 0.6931472;
    float smoothed = float(iterN) + 1.0 - nu;

    float tVal = frac(smoothed * 0.04 + time * ColourCycle * 0.05 + aHigh * 0.5);
    float val  = pow(frac(smoothed * 0.04), 0.4);

    float3 col = lerp(InnerColour.rgb, OuterColour.rgb, tVal) * val * (1.0 + aBeat * 0.7);
    return float4(saturate(col), 1.0);
}
