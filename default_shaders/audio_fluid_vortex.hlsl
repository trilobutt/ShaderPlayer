/*{
    "SHADER_TYPE": "audio",
    "DESCRIPTION": "Incompressible fluid advection driven by the spectrum. A divergence-free velocity field is taken as the curl of the noise texture and the sampling coordinate traced backward through it, so dye swirls in closed vortices instead of smearing. Bass injects the large coarse vortices, treble the fine viscous streaks, and Colour Vorticity switches the palette between local rotation and dye density.",
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
        {"NAME": "brightness",         "LABEL": "Dye Density",     "TYPE": "float", "MIN": 0.1,  "MAX": 3.0,  "DEFAULT": 1.2},
        {"NAME": "TrailAmt",           "LABEL": "Flow Trails",     "TYPE": "float", "MIN": 0.0,  "MAX": 2.0,  "DEFAULT": 0.7},
        {"NAME": "PaletteShift",       "LABEL": "Palette Shift",   "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.0},
        {"NAME": "Exposure",           "LABEL": "Exposure",        "TYPE": "float", "MIN": 0.1,  "MAX": 4.0,  "DEFAULT": 1.0},
        {"NAME": "VignetteAmt",        "LABEL": "Vignette",        "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.25},
        {"NAME": "FluidTint",          "LABEL": "Fluid Tint",      "TYPE": "color", "DEFAULT": [1.0,1.0,1.0,1.0]}
    ]
}*/

// ISF packing: the fifteen scalars occupy offsets 0..14, FluidTint takes 16..19 on its
// 4-float boundary (offset 15 is a padding hole). 20/32 floats used.

// Audio-driven incompressible fluid vortex.
// A divergence-free velocity field is derived from the curl of the Perlin noise
// texture: Vx = dN/dy, Vy = -dN/dx.  Bass energy injects large-scale vortices
// (coarse noise) while treble drives fine viscous streaks (high-frequency noise).
// UV coordinates are advected backward through the combined field to fetch the
// dye colour from the spectrum visualisation.  colourByVorticity selects whether
// local rotation or dye density drives the palette.

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

// Dye concentration at an advected position.
// Sampling the spectrum linearly in x confines every visible bin to the left edge:
// music puts almost no energy above bin ~40 of 256, so the frame only lit up at the
// margins. The power curve redistributes those populated low bins across the full
// width, and folding q.y into the lookup stops the result being vertically uniform.
// The body dye carries flow structure into regions the spectrum leaves quiet; it
// takes two octaves so the field still has something to show under a zoom, where a
// single 2.5x octave went flat.
// Every fetch is SampleLevel: q is loop-carried and wrapped through frac(), so an
// implicit-LOD Sample would ask the hardware for a derivative that is undefined
// there. No texture in the pipeline is mip-mapped, so level 0 is also exactly what
// such a fetch would have resolved to.
float dyeAt(float2 q) {
    float specX = pow(saturate(q.x * 0.72 + q.y * 0.28), spectrumSpread);
    float specE = spectrumTexture.SampleLevel(videoSampler, float2(specX, 0.5), 0).r;

    float bodyDye = noiseTexture.SampleLevel(noiseSampler, q * 2.5 + time * 0.010, 0).r * 0.65
                  + noiseTexture.SampleLevel(noiseSampler, q * 7.3 - time * 0.024, 0).r * 0.35;

    return specE * 1.6 +
           dyeFill * bodyDye * (0.25 + bassLevel * 2.5 + trebleLevel * 1.5);
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
    float2 p   = uv;
    float2 px  = 1.0 / resolution * viscosity;
    float2 vel = 0.0.xx;

    [loop] for (int s = 0; s < 16; s++) {
        float2 vCoarse = curlNoise(p * float2(ar, 1.0), coarseFreq, time) * (bassAmp + injectionBoost);
        float2 vFine   = curlNoise(p * float2(ar, 1.0), fineFreq,   time) * trebleAmp * 0.4;
        vel = (vCoarse + vFine) * px * 0.8;
        p  -= vel;
        p   = frac(p);
    }

    float2 q = frac(p);

    // --- Dye field, with an analytic trail along the streamline ---
    // There is no previous-frame texture to accumulate into, so the trail is built by
    // continuing the march past the advected point and summing the dye found there,
    // weighted by 1/(1+ks). That is the same integral a feedback trail approximates,
    // evaluated in one pass. It is added rather than blended, so dense flow lines
    // accumulate real energy for the tonemap to roll off.
    float dye   = dyeAt(q);
    float trail = 0.0;
    if (TrailAmt > 0.001) {
        float wsum = 0.0;
        [unroll] for (int i = 1; i <= 6; i++) {
            float w = 1.0 / (1.0 + float(i) * 1.4);
            trail += dyeAt(frac(q - vel * float(i) * 6.0)) * w;
            wsum  += w;
        }
        trail /= max(wsum, 1e-4);
    }
    float specVal = dye + TrailAmt * trail * 0.6;

    // Compute local vorticity for colouring
    float eps2 = 2.0 / max(resolution.x, resolution.y);
    float2 vRight = curlNoise(float2((uv.x + eps2) * ar, uv.y), coarseFreq, time);
    float2 vLeft  = curlNoise(float2((uv.x - eps2) * ar, uv.y), coarseFreq, time);
    float2 vUp    = curlNoise(float2(uv.x * ar, uv.y + eps2), coarseFreq, time);
    float2 vDown  = curlNoise(float2(uv.x * ar, uv.y - eps2), coarseFreq, time);
    float vortMag = abs((vRight.y - vLeft.y - vUp.x + vDown.x) / (2.0 * eps2)) * 0.02;

    // Palette. The previous HSV rainbow put a full hue sweep across every gradient,
    // which is the classic "shader rainbow" read: every region equally saturated and
    // nothing to look at. The IQ cosine palette stays continuous, keeps a coherent
    // warm/cool axis, and PaletteShift exposes the phase rather than a raw hue.
    float pt = colourByVorticity
             ? vortMag * 0.8 + q.x * 0.3 + q.y * 0.15 + time * 0.03
             : specVal * 0.5 + q.x * 0.4 + q.y * 0.15;
    pt += PaletteShift;

    float3 col = spPalette(pt,
                           float3(0.50, 0.42, 0.48),
                           float3(0.48, 0.46, 0.50),
                           float3(1.00, 1.00, 1.00),
                           float3(0.00, 0.15, 0.35));

    // Dye density drives brightness. Left unbounded on purpose, so the tonemap decides
    // where the highlights roll rather than a saturate().
    col *= specVal * brightness;

    // Vorticity picks out the shear filaments as extra energy rather than as a hue
    // change, so the rotating structure reads even where the dye is thin.
    col += spPalette(pt + 0.12,
                     float3(0.50, 0.42, 0.48),
                     float3(0.48, 0.46, 0.50),
                     float3(1.00, 1.00, 1.00),
                     float3(0.00, 0.15, 0.35))
         * vortMag * (0.35 + trebleLevel * 2.5);

    // Diffusion: bleed toward a neighbouring dye sample.
    float diffuse = dyeAt(frac(q + float2(0.012, 0.008)));
    col = lerp(col, col * 0.35 + diffuse * 0.25, dyeDiffusion * 0.5);

    col *= spSrgbToLinear(FluidTint.rgb);
    col *= spVignette(uv, VignetteAmt, 0.8);
    col *= Exposure;

    // No procedural step edges here (every boundary is a smooth field), so there is
    // nothing for spAAStep to band-limit; the aliasing risk is all in the texture
    // fetches, which the SampleLevel note above covers.

    // tanh: the trail and vorticity terms both accumulate without bound.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
