/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        {"NAME": "noiseFreq",      "LABEL": "Noise Scale",   "TYPE": "float", "MIN": 0.5,  "MAX": 12.0, "DEFAULT": 3.0},
        {"NAME": "octaveCount",    "LABEL": "Octaves",       "TYPE": "long",
         "VALUES": [1,2,3,4,5,6], "LABELS": ["1","2","3","4","5","6"], "DEFAULT": 4},
        {"NAME": "persistence",    "LABEL": "Persistence",   "TYPE": "float", "MIN": 0.1,  "MAX": 0.9,  "DEFAULT": 0.5},
        {"NAME": "lacunarity",     "LABEL": "Lacunarity",    "TYPE": "float", "MIN": 1.2,  "MAX": 4.0,  "DEFAULT": 2.0},
        {"NAME": "trailFade",      "LABEL": "Trail Length",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.6},
        {"NAME": "stepSz",         "LABEL": "Step Size",     "TYPE": "float", "MIN": 0.5,  "MAX": 8.0,  "DEFAULT": 2.0},
        {"NAME": "colourByAngle",  "LABEL": "Colour By Angle","TYPE": "bool", "DEFAULT": true},
        {"NAME": "FlowTint",       "LABEL": "Flow Tint",      "TYPE": "color","DEFAULT": [0.6,0.9,1.0,1.0]}
    ]
}*/

// Perlin curl flow field visualisation (LIC approximation).
// At each pixel the velocity field angle is read from FBM-summed noise; the
// shader then advects the sample point backward for trailSteps iterations,
// accumulating the noise texture value along the path.  The resulting integral
// produces the characteristic fibrous streamline texture of Line-Integral
// Convolution.  colourByAngle tints each streamline by local flow direction.

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

float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// FBM velocity angle from Perlin noise texture (R channel)
float flowAngle(float2 p, float freq, int octs, float pers, float lac) {
    float angle  = 0.0;
    float amp    = 1.0;
    float totAmp = 0.0;
    float2 fp    = p * freq;
    // Slide the noise slowly over time so the field evolves
    float2 drift = float2(time * 0.04, time * 0.027);
    [loop] for (int i = 0; i < 6; i++) {
        if (i >= octs) break;
        float n  = noiseTexture.SampleLevel(noiseSampler, fp + drift * pow(lac, float(i)), 0).r * 2.0 - 1.0;
        angle   += n * amp;
        totAmp  += amp;
        fp      *= lac;
        amp     *= pers;
    }
    // Map to full circle; 3.0 multiplier gives tighter curl patterns
    return (angle / max(totAmp, 0.001)) * 3.14159265 * 3.0;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float2 px  = 1.0 / resolution;
    float  ar  = resolution.x / resolution.y;

    int   trailSteps = max(4, int(trailFade * 18.0 + 4.0));
    float pxStep     = stepSz;  // step length in pixels

    // --- LIC integration (backward trace) ---
    float licVal = 0.0;
    float2 p     = uv;
    float2 pAR   = float2(ar, 1.0);   // aspect-ratio corrected coords

    [loop] for (int s = 0; s < 24; s++) {
        if (s >= trailSteps) break;
        float ang = flowAngle(p * pAR, noiseFreq, octaveCount, persistence, lacunarity);
        float2 vel = float2(cos(ang), -sin(ang));                // screen-space direction
        p         -= vel * pxStep * px;                          // step backward
        p          = frac(p);                                    // wrap

        // High-frequency checkerboard as LIC base signal → averages to fibrous texture
        float2 hiFreq  = p * pAR * 80.0;
        float  stripes = frac(hiFreq.x + hiFreq.y) > 0.5 ? 1.0 : 0.0;
        licVal += stripes;
    }
    licVal /= float(max(trailSteps, 1));

    // Brightness: squash into a nice range, apply slight gamma lift
    float brightness = pow(saturate(licVal * 1.4 - 0.1), 0.7);

    // Colour by local flow angle
    float ang0 = flowAngle(uv * float2(ar, 1.0), noiseFreq, octaveCount, persistence, lacunarity);
    float3 col;
    if (colourByAngle) {
        float hue = frac(ang0 / (3.14159265 * 2.0) + 0.5);
        col = hsv2rgb(float3(hue, 0.75, brightness)) * FlowTint.rgb;
    } else {
        col = lerp(float3(0.0, 0.0, 0.0), FlowTint.rgb, brightness);
    }

    return float4(saturate(col), 1.0);
}
