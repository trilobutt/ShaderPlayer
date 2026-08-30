/*{
    "DESCRIPTION": "Escape-time Julia set for z squared plus c, smoothly coloured and framed so the fractal is always the subject. The iteration carries the orbit derivative alongside z, which gives a Koebe distance estimate to the set and drives a filament glow that stays a genuine pixel wide at any zoom. C Real and C Imaginary pick which set is drawn; Zoom Centre picks what a deep zoom converges on.",
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
        { "NAME": "GlowAmount",  "LABEL": "Filament Glow",     "TYPE": "float", "DEFAULT": 0.6,     "MIN": 0.0,  "MAX": 3.0,   "STEP": 0.01  },
        { "NAME": "Exposure",    "LABEL": "Exposure",          "TYPE": "float", "DEFAULT": 1.0,     "MIN": 0.1,  "MAX": 4.0,   "STEP": 0.01  },
        { "NAME": "VignetteAmt", "LABEL": "Vignette",          "TYPE": "float", "DEFAULT": 0.25,    "MIN": 0.0,  "MAX": 1.0,   "STEP": 0.01  },
        { "NAME": "BassIn",      "LABEL": "Bass",              "TYPE": "audio", "BAND": "bass" },
        { "NAME": "HighIn",      "LABEL": "Treble",            "TYPE": "audio", "BAND": "high" },
        { "NAME": "BeatIn",      "LABEL": "Beat",              "TYPE": "audio", "BAND": "beat" }
    ]
}*/

// Parameter ranges are chosen so the fractal is always the subject of the frame:
//
// C Real / C Imaginary are restricted to the neighbourhood of the Mandelbrot set
// (roughly |c| < 1). Outside that region the Julia set degenerates into Fatou dust
// (a sparse scatter with no visible structure), so those values are excluded.
//
// Zoom is a magnification factor: 1.0 frames the whole set (|z| <= 2 bound) with a
// small margin and no more. Values below 1 are excluded because they pull the camera
// back far enough to show the circular escape-time banding that surrounds the set,
// which is what put a visible "outer circle" in frame. Zoom Centre picks the point
// magnification converges on, so deep zooms land on structure rather than the origin.
//
// The iteration loop carries the orbit derivative dz alongside z. That buys two
// things the escape count alone cannot give: a Koebe distance estimate to the set,
// which drives an inverse-distance filament glow that is genuinely one pixel wide at
// any zoom, and a bailout that fires before the derivative overflows fp32.

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

    // Optional animated C parameter: Douady rabbit neighbourhood orbit
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

    // One output pixel measured in complex-plane units. The filament footprint is
    // expressed in these units, so the glow stays one pixel wide at any resolution
    // and any zoom rather than dissolving as the camera pushes in.
    float px = halfHeight * 2.0 / max(resolution.y, 1.0);

    float2 z  = p;
    float2 dz = float2(1.0, 0.0);   // dz_n / dz_0
    float  trap = 1e9;              // closest approach of the orbit to the origin
    int  iterN = 0;
    bool escaped    = false;
    bool onFilament = false;

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

        trap = min(trap, zx2 + zy2);

        // |dz| roughly doubles every step for a point hugging the boundary. Past
        // this magnitude the pixel is a small fraction of a pixel from the set at
        // any practical zoom, so bail out here rather than let dz overflow to inf
        // and poison the distance estimate with a NaN.
        if (dot(dz, dz) > 1e24) {
            iterN = i;
            onFilament = true;
            break;
        }

        dz = 2.0 * float2(z.x * dz.x - z.y * dz.y, z.x * dz.y + z.y * dz.x);
        z  = float2(zx2 - zy2 + cx, 2.0 * z.x * z.y + cy);
        iterN = i + 1;
    }

    // Both user colours are picked in sRGB; everything below is linear light until
    // the final encode, so the ramp between them is an actual gradient.
    float3 innerLin = spSrgbToLinear(InnerColour.rgb);
    float3 outerLin = spSrgbToLinear(OuterColour.rgb);

    float3 col;
    if (escaped) {
        // Smooth escape colouring (Hubbard-Douady potential normalisation): the
        // fractional iteration count, continuous across the integer boundary.
        float logZn = log(dot(z, z)) * 0.5;
        float nu = log(logZn / 0.6931472) / 0.6931472;
        float smoothed = float(iterN) + 1.0 - nu;

        // Koebe distance estimate |z|·log|z| / |dz|, in complex-plane units.
        float zLen = sqrt(dot(z, z));
        float dist = zLen * log(zLen) / max(length(dz), 1e-20);

        // Continuous colour cycle. The previous frac() wrapped Outer straight back
        // to Inner once per band, so every ring carried a hard seam; closing the
        // loop with a cosine removes the seam without changing the band spacing.
        float phase = smoothed * 0.04 + time * ColourCycle * 0.05 + aHigh * 0.5;
        float tVal  = 0.5 - 0.5 * cos(SP_TAU * phase);

        // Brightness rises toward the set, so the eye lands on the boundary rather
        // than on the outermost band. Replaces pow(frac(...)), which banded hard.
        float speed = saturate(smoothed / max(float(iterBudget) * 0.3, 1.0));
        col = lerp(innerLin, outerLin, tVal) * (0.10 + 0.90 * speed);

        // Inverse-distance filament glow. This is what the derivative is for: near
        // the boundary the pixel accumulates real HDR energy that tanh rolls into a
        // bloom, which a pow() ramp on a clamped colour cannot reproduce.
        col += outerLin * GlowAmount * 0.5 * (px / max(dist, px * 0.15));
    } else if (onFilament) {
        // Derivative bailout: the pixel is on the boundary itself. Hot core.
        col = outerLin * (0.35 + GlowAmount * 2.5);
    } else {
        // Interior, shaded by the orbit trap (closest approach to the origin) so the
        // body of the set carries structure instead of reading as a flat silhouette.
        float t = saturate(1.0 - sqrt(trap));
        col = innerLin * (0.05 + 0.55 * t * t);
    }

    col *= Exposure * (1.0 + aBeat * 0.7);
    col *= spVignette(input.uv, VignetteAmt, 0.8);

    // tanh rather than ACES: the filament term is unbounded and tanh holds the
    // colour of the outer ramp through the hot cores instead of washing to white.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
