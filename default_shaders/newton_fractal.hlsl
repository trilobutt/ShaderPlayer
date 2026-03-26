/*{
    "DESCRIPTION": "Newton's method fractal for f(z) = z^n - 1, coloured by convergence basin and speed",
    "SHADER_TYPE": "generative",
    "INPUTS": [
        { "NAME": "Degree",      "LABEL": "Polynomial Degree", "TYPE": "long",
          "VALUES": [2,3,4,5,6,7], "LABELS": ["2","3","4","5","6","7"], "DEFAULT": 3       },
        { "NAME": "ZoomN",       "LABEL": "Zoom",              "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.1,"MAX": 5.0, "STEP": 0.05 },
        { "NAME": "ColourOffset","LABEL": "Colour Offset",     "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0,"MAX": 1.0, "STEP": 0.01 },
        { "NAME": "Damping",     "LABEL": "Relaxation",        "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.1,"MAX": 2.0, "STEP": 0.05 },
        { "NAME": "MaxIterN",    "LABEL": "Max Iterations",    "TYPE": "long",
          "VALUES": [8,16,32,48,64,128], "LABELS": ["8","16","32","48","64","128"], "DEFAULT": 48 },
        { "NAME": "AnimSpeedN",  "LABEL": "Animate Speed",     "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0,"MAX": 0.5, "STEP": 0.01 },
        { "NAME": "PaletteTint", "LABEL": "Palette Tint",      "TYPE": "color", "DEFAULT": [1.0,1.0,1.0,1.0] }
    ]
}*/

// ISF packing:
// Degree       offset 0 → int(custom[0].x)
// ZoomN        offset 1 → custom[0].y
// ColourOffset offset 2 → custom[0].z
// Damping      offset 3 → custom[0].w
// MaxIterN     offset 4 → int(custom[1].x)
// AnimSpeedN   offset 5 → custom[1].y
// (hole at 6,7)
// PaletteTint  offset 8 → custom[2] (rgba)

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
    float3 p = abs(frac(float3(h, h, h) + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return v * lerp(float3(1, 1, 1), saturate(p - 1.0), s);
}

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
    int   degreeVal   = int(custom[0].x);
    float zoomVal     = custom[0].y;
    float colOffset   = custom[0].z;
    float dampVal     = custom[0].w;
    int   maxIterVal  = int(custom[1].x);
    float animSpd     = custom[1].y;

    static const float TWO_PI = 6.28318530718;

    // Map UV to complex plane
    float2 z0 = (input.uv - 0.5) * float2(resolution.x / resolution.y, 1.0) * zoomVal * 3.0;

    // Rotate slowly when AnimSpeedN > 0
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
    for (int k = 0; k < 8; ++k) {
        if (k >= degreeVal) break;
        float rootAngle = TWO_PI * float(k) / float(degreeVal);
        float2 root = float2(cos(rootAngle), sin(rootAngle));
        float d = dot(zvar - root, zvar - root);
        if (d < minRootDist) {
            minRootDist = d;
            closestRoot = k;
        }
    }

    float hue  = frac(float(closestRoot) / float(degreeVal) + colOffset + time * animSpd * 0.02);
    float bri  = pow(1.0 - float(ni) / float(maxIterVal), 0.4);
    float sat  = saturate(0.9 - sqrt(minRootDist) * 2.0);

    float3 col = hsv2rgb(hue, sat, bri) * custom[2].rgb;
    return float4(col, 1.0);
}
