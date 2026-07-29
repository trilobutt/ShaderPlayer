/*{
    "DESCRIPTION": "Newton's method fractal for f(z) = z^n - 1, with a selectable colour per convergence basin",
    "SHADER_TYPE": "generative",
    "INPUTS": [
        { "NAME": "RootColour1", "LABEL": "Root 1 Colour",     "TYPE": "color", "DEFAULT": [1.00,0.28,0.24,1.0] },
        { "NAME": "RootColour2", "LABEL": "Root 2 Colour",     "TYPE": "color", "DEFAULT": [0.24,0.86,0.42,1.0] },
        { "NAME": "RootColour3", "LABEL": "Root 3 Colour",     "TYPE": "color", "DEFAULT": [0.26,0.52,1.00,1.0] },
        { "NAME": "RootColour4", "LABEL": "Root 4 Colour",     "TYPE": "color", "DEFAULT": [1.00,0.82,0.20,1.0] },
        { "NAME": "RootColour5", "LABEL": "Root 5 Colour",     "TYPE": "color", "DEFAULT": [0.82,0.32,0.95,1.0] },
        { "NAME": "RootColour6", "LABEL": "Root 6 Colour",     "TYPE": "color", "DEFAULT": [0.20,0.90,0.90,1.0] },
        { "NAME": "Degree",      "LABEL": "Polynomial Degree", "TYPE": "long",
          "VALUES": [2,3,4,5,6], "LABELS": ["2","3","4","5","6"], "DEFAULT": 3 },
        { "NAME": "ZoomN",       "LABEL": "Zoom",              "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.1,"MAX": 5.0, "STEP": 0.05 },
        { "NAME": "Damping",     "LABEL": "Relaxation",        "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.1,"MAX": 2.0, "STEP": 0.05 },
        { "NAME": "MaxIterN",    "LABEL": "Max Iterations",    "TYPE": "long",
          "VALUES": [8,16,32,48,64,128], "LABELS": ["8","16","32","48","64","128"], "DEFAULT": 48 },
        { "NAME": "AnimSpeedN",  "LABEL": "Animate Speed",     "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0,"MAX": 0.5, "STEP": 0.01 },
        { "NAME": "AudioAmount", "LABEL": "Audio Amount",      "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0,"MAX": 1.0, "STEP": 0.01 },
        { "NAME": "Exposure",    "LABEL": "Exposure",          "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.1,"MAX": 4.0, "STEP": 0.01 },
        { "NAME": "VignetteAmt", "LABEL": "Vignette",          "TYPE": "float", "DEFAULT": 0.25,"MIN": 0.0,"MAX": 1.0, "STEP": 0.01 },
        { "NAME": "BassIn",      "LABEL": "Bass",              "TYPE": "audio", "BAND": "bass" },
        { "NAME": "MidIn",       "LABEL": "Mid",               "TYPE": "audio", "BAND": "mid"  },
        { "NAME": "BeatIn",      "LABEL": "Beat",              "TYPE": "audio", "BAND": "beat" }
    ]
}*/

// Each convergence basin gets its own colour rather than a single tint over a
// generated hue ramp. Six explicit root colours plus the numeric controls fill the
// 32-float uniform block exactly, so the polynomial degree tops out at 6 and there
// is no room for a separate glow control — Exposure drives the whole image.
//
// Basin boundaries are not anti-aliased and deliberately so: the boundary between
// any two basins contains points of every basin at every scale (it is the Julia set
// of the Newton map), so there is no footprint over which a filtered edge is
// meaningful. What removes the aliasing there instead is the orbit-trap glow, which
// is a smooth field over exactly that region.

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

float2 cmul(float2 a, float2 b) {
    return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float2 cdiv(float2 a, float2 b) {
    float d = dot(b, b);
    return float2(dot(a, b), a.y * b.x - a.x * b.y) / d;
}

float2 cpow_int(float2 z, int n) {
    float2 r = float2(1.0, 0.0);
    [loop]
    for (int i = 0; i < n; ++i) {
        r = cmul(r, z);
    }
    return r;
}

float4 main(PS_INPUT input) : SV_TARGET {
    // --- Audio modulation ---
    // Bass drives the relaxation factor, which makes the basin boundaries churn and
    // fold; mid spins the plane; beats lift the exposure. AudioAmount 0 = static.
    float aBass = BassIn * AudioAmount;
    float aMid  = MidIn  * AudioAmount;
    float aBeat = BeatIn * AudioAmount;

    int   degreeVal  = clamp(Degree, 2, 6);
    float zoomVal    = ZoomN;
    float dampVal    = clamp(Damping + aBass * 0.7, 0.1, 2.5);
    int   maxIterVal = MaxIterN;
    float animSpd    = AnimSpeedN + aMid * 0.35;

    static const float TWO_PI = 6.28318530718;

    // Map UV to complex plane
    float2 z0 = (input.uv - 0.5) * float2(resolution.x / resolution.y, 1.0) * zoomVal * 3.0;

    // Rotate slowly when Animate Speed > 0 (or when mid energy is driving it)
    float ca = cos(time * animSpd);
    float sa = sin(time * animSpd);
    float2 zvar = float2(z0.x * ca - z0.y * sa, z0.x * sa + z0.y * ca);

    int   ni   = 0;
    float trap = 1e9;   // closest approach of the orbit to the origin

    [loop]
    for (ni = 0; ni < maxIterVal; ++ni) {
        // z^(n-1) once, then z^n by one more multiply. The old form evaluated three
        // separate cpow_int calls per iteration (z^n, z^(n-1) and a residual), which
        // was roughly three times the complex multiplies for the same step.
        float2 znm1 = cpow_int(zvar, degreeVal - 1);
        float2 zn   = cmul(znm1, zvar);
        float2 step_val = cdiv(zn - float2(1.0, 0.0), float(degreeVal) * znm1);
        zvar -= dampVal * step_val;

        // The orbit of a boundary pixel swings past the pole at the origin before it
        // settles; the interior of a basin never does. Trapping that distance gives a
        // smooth field concentrated exactly on the boundary filigree.
        trap = min(trap, length(zvar));

        // Converged once the correction stops moving. Cheaper than re-evaluating the
        // residual, and the right test under relaxation, where the residual can stall
        // while the iterate is still walking.
        if (dot(step_val, step_val) < 1e-12) { ++ni; break; }
    }

    // Find closest root: roots at exp(2πi k/n)
    int closestRoot = 0;
    float minRootDist = 1e10;

    [loop]
    for (int k = 0; k < 6; ++k) {
        if (k >= degreeVal) break;
        float rootAngle = TWO_PI * float(k) / float(degreeVal);
        float2 root = float2(cos(rootAngle), sin(rootAngle));
        float d = dot(zvar - root, zvar - root);
        if (d < minRootDist) {
            minRootDist = d;
            closestRoot = k;
        }
    }

    float4 rootCols[6] = { RootColour1, RootColour2, RootColour3,
                           RootColour4, RootColour5, RootColour6 };
    // Root colours are picked in sRGB; shade and blend in linear light.
    float3 basin = spSrgbToLinear(rootCols[closestRoot].rgb);

    // Smooth (fractional) iteration count. Newton converges quadratically at
    // Relaxation 1.0, so the error satisfies log|e_n| ~ 2^n·log|e_0| and the
    // fractional part falls out as a log2 of the log ratio. Away from 1.0 the
    // convergence is linear instead and the fraction is only approximate — it still
    // varies continuously with the pixel, which is all the shading needs, and it is
    // what removes the integer contour steps the raw count produced.
    float dRoot   = max(sqrt(minRootDist), 1e-30);
    float ratio   = max(log(dRoot) / log(1e-6), 1.0);
    float smoothN = float(ni) - log(ratio) / 0.6931472;

    // Convergence speed shades the basin; distance to the root fades the boundary
    // filaments toward black so the fractal structure stays legible.
    float shade = pow(saturate(1.0 - smoothN / float(maxIterVal)), 0.4);
    float grip  = saturate(0.9 - dRoot * 2.0);

    // Fine contour rings on the fractional iteration count add a second scale of
    // detail across the otherwise flat basin interiors. Band-limited, so where the
    // count is discontinuous (across a basin boundary) the rings fade to flat
    // instead of aliasing into a moire.
    float ringPhase = SP_TAU * smoothN * 0.5;
    float ring = 0.5 + 0.5 * spBandLimitedCos(ringPhase, fwidth(ringPhase));

    float3 col = basin * shade * (0.3 + 0.7 * grip) * (0.82 + 0.18 * ring);

    // Inverse-distance glow on the orbit trap. Unbounded near the boundary, where
    // the orbit passes closest to the pole, so the filigree genuinely blooms.
    col += basin * 0.05 / max(trap, 0.02);

    col *= Exposure * (1.0 + aBeat * 0.8);
    col *= spVignette(input.uv, VignetteAmt, 0.8);

    // tanh rather than ACES: the trap term is unbounded and tanh keeps each basin's
    // hue through its hot core instead of desaturating the boundaries to white.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
