/*{
    "DESCRIPTION": "Julia set fractal with smooth colouring and optional animation of the C parameter",
    "SHADER_TYPE": "generative",
    "INPUTS": [
        { "NAME": "CX",          "LABEL": "C Real",            "TYPE": "float", "DEFAULT": -0.7269, "MIN": -2.0, "MAX": 2.0,  "STEP": 0.001 },
        { "NAME": "CY",          "LABEL": "C Imaginary",       "TYPE": "float", "DEFAULT":  0.1889, "MIN": -2.0, "MAX": 2.0,  "STEP": 0.001 },
        { "NAME": "Zoom",        "LABEL": "Zoom",              "TYPE": "float", "DEFAULT": 1.3,     "MIN": 0.1,  "MAX": 10.0, "STEP": 0.05  },
        { "NAME": "MaxIter",     "LABEL": "Max Iterations",    "TYPE": "long",  "DEFAULT": 128,     "MIN": 16,   "MAX": 256               },
        { "NAME": "ColourCycle", "LABEL": "Colour Cycle Speed","TYPE": "float", "DEFAULT": 0.1,     "MIN": 0.0,  "MAX": 2.0,  "STEP": 0.01  },
        { "NAME": "AnimateC",    "LABEL": "Animate C",         "TYPE": "bool",  "DEFAULT": 0                                             }
    ]
}*/

// ISF packing:
// CX          offset 0 → custom[0].x
// CY          offset 1 → custom[0].y
// Zoom        offset 2 → custom[0].z
// MaxIter     offset 3 → int(custom[0].w)
// ColourCycle offset 4 → custom[1].x
// AnimateC    offset 5 → (custom[1].y > 0.5)

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

float4 main(PS_INPUT input) : SV_TARGET {
    float cx          = custom[0].x;
    float cy          = custom[0].y;
    float zoomVal     = custom[0].z;
    int   maxIterVal  = int(custom[0].w);
    float colourSpeed = custom[1].x;
    bool  animC       = (custom[1].y > 0.5);

    // Optional animated C parameter — Douady rabbit neighbourhood orbit
    if (animC) {
        cx = cos(time * 0.13) * 0.7885;
        cy = sin(time * 0.17) * 0.7885;
    }

    // Aspect-correct UV mapped to complex plane
    float2 p = (input.uv - 0.5) * float2(resolution.x / resolution.y, 1.0) * zoomVal * 2.5;

    float2 z = p;
    int iterN = 0;
    bool escaped = false;

    [loop]
    for (int i = 0; i < maxIterVal; ++i) {
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
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    // Smooth escape colouring (Hubbard-Douady potential normalisation)
    float logZn = log(dot(z, z)) * 0.5;
    float nu = log(logZn / 0.6931472) / 0.6931472;  // log2(log2(|z|))
    float smoothed = float(iterN) + 1.0 - nu;

    float tVal = frac(smoothed * 0.04 + time * colourSpeed * 0.05);
    float val  = pow(frac(smoothed * 0.04), 0.4);

    float3 col = hsv2rgb(tVal, 0.85, val);
    return float4(col, 1.0);
}
