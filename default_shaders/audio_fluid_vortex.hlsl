/*{
    "SHADER_TYPE": "audio",
    "INPUTS": [
        {"NAME": "bassLevel",          "LABEL": "Bass",            "TYPE": "audio", "BAND": "bass"},
        {"NAME": "trebleLevel",        "LABEL": "Treble",          "TYPE": "audio", "BAND": "high"},
        {"NAME": "viscosity",          "LABEL": "Viscosity",       "TYPE": "float", "MIN": 0.1,  "MAX": 2.0,  "DEFAULT": 0.6},
        {"NAME": "dyeDiffusion",       "LABEL": "Dye Diffusion",   "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.4},
        {"NAME": "bassInfluenceScale", "LABEL": "Bass Scale",      "TYPE": "float", "MIN": 0.0,  "MAX": 5.0,  "DEFAULT": 2.0},
        {"NAME": "trebleScale",        "LABEL": "Treble Scale",    "TYPE": "float", "MIN": 0.0,  "MAX": 5.0,  "DEFAULT": 1.5},
        {"NAME": "injectionPoints",    "LABEL": "Vortex Points",   "TYPE": "long",
         "VALUES": [1,2,3,4], "LABELS": ["1","2","3","4"], "DEFAULT": 2},
        {"NAME": "colourByVorticity",  "LABEL": "Colour Vorticity","TYPE": "bool",  "DEFAULT": true},
        {"NAME": "spectrumSpread",     "LABEL": "Spectrum Spread", "TYPE": "float", "MIN": 1.0,  "MAX": 6.0,  "DEFAULT": 3.0},
        {"NAME": "dyeFill",            "LABEL": "Dye Fill",        "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.55},
        {"NAME": "brightness",         "LABEL": "Brightness",      "TYPE": "float", "MIN": 0.1,  "MAX": 3.0,  "DEFAULT": 1.2},
        {"NAME": "FluidTint",          "LABEL": "Fluid Tint",      "TYPE": "color", "DEFAULT": [1.0,1.0,1.0,1.0]}
    ]
}*/

// Audio-driven incompressible fluid vortex.
// A divergence-free velocity field is derived from the curl of the Perlin noise
// texture: Vx = dN/dy, Vy = -dN/dx.  Bass energy injects large-scale vortices
// (coarse noise) while treble drives fine viscous streaks (high-frequency noise).
// UV coordinates are advected backward through the combined field to fetch the
// dye colour from the spectrum visualisation.  colourByVorticity tints regions
// by local rotation magnitude.

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

float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Curl of the noise field at uv, sampled at given frequency scale
float2 curlNoise(float2 uv, float freq, float timeOff) {
    float2 fp  = uv * freq + float2(timeOff * 0.03, timeOff * 0.021);
    float eps  = 1.5 / max(resolution.x, resolution.y);
    float nx   = noiseTexture.SampleLevel(noiseSampler, fp + float2(eps, 0), 0).r
               - noiseTexture.SampleLevel(noiseSampler, fp - float2(eps, 0), 0).r;
    float ny   = noiseTexture.SampleLevel(noiseSampler, fp + float2(0, eps), 0).r
               - noiseTexture.SampleLevel(noiseSampler, fp - float2(0, eps), 0).r;
    return float2(ny, -nx) / (2.0 * eps);  // curl: (dN/dy, -dN/dx)
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;
    float  ar = resolution.x / resolution.y;

    // Combine coarse (bass) and fine (treble) curl fields
    float coarseFreq = 1.5 + viscosity * 0.5;
    float fineFreq   = coarseFreq * 4.0;

    float bassAmp   = bassLevel   * bassInfluenceScale;
    float trebleAmp = trebleLevel * trebleScale;

    // Injection: additional vortex sources driven by bass peaks.
    // Sites sit well out toward the corners with a wide falloff so the extra rotation
    // reaches the frame edges rather than pooling in the centre.
    float injectionBoost = 0.0;
    if (injectionPoints >= 2) {
        float2 p2 = float2(uv.x - 0.5, uv.y - 0.5) * float2(ar, 1.0);
        injectionBoost += exp(-length(p2 - float2( 0.32,  0.30)) * 3.5) * bassAmp * 0.5;
        if (injectionPoints >= 3) injectionBoost += exp(-length(p2 - float2(-0.34, -0.26)) * 3.5) * bassAmp * 0.4;
        if (injectionPoints >= 4) injectionBoost += exp(-length(p2 - float2( 0.28, -0.32)) * 3.5) * bassAmp * 0.4;
    }

    // --- Backward advection through the velocity field ---
    float2 p = uv;
    float2 px = 1.0 / resolution * viscosity;

    int advSteps = 16;
    [loop] for (int s = 0; s < 16; s++) {
        float2 vCoarse = curlNoise(p * float2(ar, 1.0), coarseFreq, time) * (bassAmp + injectionBoost);
        float2 vFine   = curlNoise(p * float2(ar, 1.0), fineFreq,   time) * trebleAmp * 0.4;
        float2 vel     = (vCoarse + vFine) * px * 0.8;
        p             -= vel;
        p              = frac(p);
    }

    // --- Dye field ---
    // Sampling the spectrum linearly in x confines every visible bin to the left edge:
    // music puts almost no energy above bin ~40 of 256, so the frame only lit up at the
    // margins. The power curve redistributes those populated low bins across the full
    // width, and folding p.y into the lookup stops the result being vertically uniform.
    float2 q     = frac(p);
    float  specX = pow(saturate(q.x * 0.72 + q.y * 0.28), spectrumSpread);
    float  specE = spectrumTexture.SampleLevel(videoSampler, float2(specX, 0.5), 0).r;

    // Advected body dye. Carries the flow structure into regions the spectrum leaves
    // quiet, so the vortices read across the whole viewport instead of only where the
    // energy happens to land.
    float  bodyDye = noiseTexture.SampleLevel(noiseSampler, q * 2.5 + time * 0.01, 0).r;
    float  specVal = saturate(specE * 1.6 +
                              dyeFill * bodyDye * (0.25 + bassLevel * 2.5 + trebleLevel * 1.5));

    // Compute local vorticity for colouring
    float eps2 = 2.0 / max(resolution.x, resolution.y);
    float2 vRight = curlNoise(float2((uv.x + eps2) * ar, uv.y), coarseFreq, time);
    float2 vLeft  = curlNoise(float2((uv.x - eps2) * ar, uv.y), coarseFreq, time);
    float2 vUp    = curlNoise(float2(uv.x * ar, uv.y + eps2), coarseFreq, time);
    float2 vDown  = curlNoise(float2(uv.x * ar, uv.y - eps2), coarseFreq, time);
    float vortMag = abs((vRight.y - vLeft.y - vUp.x + vDown.x) / (2.0 * eps2)) * 0.02;

    // Build colour from dye + vorticity
    float3 col;
    if (colourByVorticity) {
        float hue = frac(vortMag * 0.8 + q.x * 0.3 + q.y * 0.15 + time * 0.03);
        float sat = 0.6 + vortMag * 0.4;
        col = hsv2rgb(float3(hue, sat, saturate(specVal * brightness)));
    } else {
        // Spectrum rainbow without vorticity tint
        float hue = frac(specVal * 0.8 + q.x * 0.5 + q.y * 0.2);
        col = hsv2rgb(float3(hue, 0.8, saturate(specVal * brightness * 1.15)));
    }

    // Diffusion: blend with a blurred neighbour sample
    float3 blurCol = noiseTexture.Sample(noiseSampler, frac(p + float2(0.001, 0))).rrr;
    col = lerp(col, blurCol * 0.2, dyeDiffusion * 0.15);
    col *= FluidTint.rgb;

    return float4(saturate(col), 1.0);
}
