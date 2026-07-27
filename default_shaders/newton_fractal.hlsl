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
        { "NAME": "BassIn",      "LABEL": "Bass",              "TYPE": "audio", "BAND": "bass" },
        { "NAME": "MidIn",       "LABEL": "Mid",               "TYPE": "audio", "BAND": "mid"  },
        { "NAME": "BeatIn",      "LABEL": "Beat",              "TYPE": "audio", "BAND": "beat" }
    ]
}*/

// Each convergence basin gets its own colour rather than a single tint over a
// generated hue ramp. Six explicit root colours are the most the 32-float uniform
// block holds alongside the numeric controls, so the polynomial degree tops out at 6.
// The old Colour Offset control is gone — it shifted a generated hue wheel that no
// longer exists.

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

    int ni = 0;

    [loop]
    for (ni = 0; ni < maxIterVal; ++ni) {
        float2 zn   = cpow_int(zvar, degreeVal);
        float2 znm1 = cpow_int(zvar, degreeVal - 1);
        float2 denom = float(degreeVal) * znm1;
        float2 step_val = cdiv(zn - float2(1.0, 0.0), denom);
        zvar -= dampVal * step_val;
        // Check convergence to any root — all roots have |z^n - 1| → 0
        float2 residual = cpow_int(zvar, degreeVal) - float2(1.0, 0.0);
        if (dot(residual, residual) < 1e-8) break;
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
    float3 basin = rootCols[closestRoot].rgb;

    // Convergence speed shades the basin; distance to the root fades the boundary
    // filaments toward black so the fractal structure stays legible.
    float shade = pow(1.0 - float(ni) / float(maxIterVal), 0.4);
    float grip  = saturate(0.9 - sqrt(minRootDist) * 2.0);

    float3 col = basin * shade * (0.3 + 0.7 * grip) * (1.0 + aBeat * 0.8);
    return float4(saturate(col), 1.0);
}
