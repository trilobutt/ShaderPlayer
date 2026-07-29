/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        {"NAME": "stickingProb",  "LABEL": "Sticking Prob",  "TYPE": "float", "MIN": 0.1,  "MAX": 1.0,   "DEFAULT": 0.7},
        {"NAME": "driftAngle",    "LABEL": "Drift Angle",    "TYPE": "float", "MIN": 0.0,  "MAX": 360.0, "DEFAULT": 270.0},
        {"NAME": "driftStrength", "LABEL": "Drift Strength", "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,   "DEFAULT": 0.0},
        {"NAME": "walkerDensity", "LABEL": "Walker Density", "TYPE": "float", "MIN": 0.1,  "MAX": 1.0,   "DEFAULT": 0.6},
        {"NAME": "colourByRadius","LABEL": "Colour by Radius","TYPE": "bool", "DEFAULT": true},
        {"NAME": "branchDetail",  "LABEL": "Branch Detail",  "TYPE": "float", "MIN": 1.0,  "MAX": 8.0,   "DEFAULT": 4.0},
        {"NAME": "AnimSpeed",     "LABEL": "Anim Speed",     "TYPE": "float", "MIN": 0.0,  "MAX": 2.0,   "DEFAULT": 0.3},
        {"NAME": "edgeSoftness",  "LABEL": "Edge Softness",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,   "DEFAULT": 0.4},
        {"NAME": "audioAmount",   "LABEL": "Audio Amount",   "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,   "DEFAULT": 0.6},
        {"NAME": "PaletteShift",  "LABEL": "Palette Shift",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,   "DEFAULT": 0.0},
        {"NAME": "GlowAmount",    "LABEL": "Filament Glow",  "TYPE": "float", "MIN": 0.0,  "MAX": 3.0,   "DEFAULT": 0.7},
        {"NAME": "Exposure",      "LABEL": "Exposure",       "TYPE": "float", "MIN": 0.1,  "MAX": 4.0,   "DEFAULT": 1.0},
        {"NAME": "VignetteAmt",   "LABEL": "Vignette",       "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,   "DEFAULT": 0.3},
        {"NAME": "bassIn",        "LABEL": "Bass",           "TYPE": "audio", "BAND": "bass"},
        {"NAME": "highIn",        "LABEL": "Treble",         "TYPE": "audio", "BAND": "high"},
        {"NAME": "beatIn",        "LABEL": "Beat",           "TYPE": "audio", "BAND": "beat"}
    ]
}*/

// Diffusion-limited aggregation (procedural approximation).
// True DLA requires incremental per-particle random-walk integration across
// frames; this shader reproduces the Hausdorff ~1.71 fractal morphology via
// multi-scale threshold noise: at each octave the local Perlin variance is
// compared to a radially decaying stickingProb envelope, exactly as the
// probability of irreversible attachment scales with the radial position in
// actual DLA clusters.  driftAngle introduces anisotropic directional bias
// matching the asymmetric DLA variant.  The resulting structure exhibits
// self-similar branching across scales controlled by branchDetail octaves.
//
// The aggregate is emissive rather than masked. Local density above the
// attachment threshold is carried as an unbounded signal, so a dense junction
// overexposes and blooms while a single-walker tip stays a thin line, and the
// tonemap rather than a saturate() decides where white lands.

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

// Warm-core to cool-tip cosine ramp. Replaces an HSV hue sweep, which spent a third
// of its range in greens the aggregate never reads well in and wrapped with a seam.
float3 dlaPalette(float t) {
    return max(spPalette(t + PaletteShift,
                         float3(0.48, 0.36, 0.42),
                         float3(0.48, 0.36, 0.42),
                         float3(1.0,  1.0,  1.0),
                         float3(0.02, 0.20, 0.52)), 0.0);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float  ar  = resolution.x / resolution.y;

    // Centred aspect-corrected coordinates [-1,1]
    float2 p   = (uv - 0.5) * float2(ar, 1.0);
    float  rad = length(p);

    // Drift bias: apply directional displacement, shrinking clusters toward one side
    float  dA   = radians(driftAngle);
    float2 bias = float2(cos(dA), sin(dA)) * driftStrength * 0.4;
    float2 pb   = p + bias;

    // --- Audio modulation ---
    // Bass extends the cluster's radial reach, treble adds fine branch octaves and
    // beats fire the nucleus. audioAmount at 0 gives the original static behaviour.
    float aBass = bassIn * audioAmount;
    float aHigh = highIn * audioAmount;
    float aBeat = beatIn * audioAmount;

    // Radial reach of the aggregate. Bass flattens the falloff so the cluster grows
    // outward on loud passages.
    float reach = max(0.25, 1.0 - aBass * 0.55);

    // Walker density lowers the attachment threshold: more walkers, denser branching.
    float densityBias = 1.35 - walkerDensity * 0.6;

    // Multi-scale fractal noise: DLA-like dendritic branching via iterated threshold
    float cluster = 0.0;
    float freq    = 2.5;
    float amp     = 1.0;
    float totAmp  = 0.0;
    // Treble buys extra octaves of fine branching.
    int   iOcts   = clamp(int(branchDetail + aHigh * 3.0), 1, 8);

    float2 animOffset = float2(sin(time * AnimSpeed * 0.4), cos(time * AnimSpeed * 0.31)) * AnimSpeed * 0.15;

    [loop] for (int i = 0; i < 8; i++) {
        if (i >= iOcts) break;
        float2 fp      = pb * freq + float2(float(i) * 1.73, float(i) * 2.31) + animOffset * (1.0 + float(i) * 0.4);
        // No frac() on the lookup: the noise sampler already wraps, and folding the
        // coordinate by hand put a discontinuity in the field that fwidth reported as
        // an infinitely wide pixel, smearing a grey seam across the aggregate wherever
        // an octave happened to cross a tile boundary.
        float  noiseVal = noiseTexture.SampleLevel(noiseSampler, fp * 0.2 + 0.5, 0).r;
        // Apply threshold at this scale; stickingProb controls density of branches.
        // A hard step() here quantised every branch to a binary in/out decision, which
        // is what made the aggregate look blocky. The transition band is at least one
        // screen-space gradient wide, so branch edges resolve smoothly at any zoom.
        float  thresh  = stickingProb * densityBias * (1.0 - float(i) / float(iOcts) * 0.5);
        float  aa      = max(fwidth(noiseVal), 1e-4) + edgeSoftness * 0.12;
        float  branch  = smoothstep(thresh - aa, thresh + aa, noiseVal)
                       * amp * exp(-rad * (0.5 + float(i) * 0.8) * reach);
        cluster += branch;
        totAmp  += amp;
        freq    *= 2.1;
        amp     *= 0.55;
    }
    cluster /= max(totAmp, 0.001);

    // Seed nucleus: always bright at centre, flaring on beats.
    float nucleus = exp(-rad * (20.0 - aBeat * 12.0));
    cluster = max(cluster, nucleus);

    // Threshold to make the fractal sparse, like actual DLA. The transition band
    // widens with edgeSoftness and never falls below the pixel footprint of `cluster`.
    float threshold = 0.35 * stickingProb * (1.0 - aBeat * 0.35);
    float maskAA    = max(fwidth(cluster), 1e-4) + edgeSoftness * 0.15 + 0.01;

    // Signed density in units of the filter width. Positive inside a branch, negative
    // outside; the zero crossing is the aggregate's boundary.
    float dens = (cluster - threshold) / maskAA;
    float body = saturate(dens * 0.5 + 0.5);

    float3 palCol;
    if (colourByRadius) {
        // Colour encodes radius: warm at the seed, cool at the tips.
        palCol = dlaPalette(rad * 0.6 + 0.05);
    } else {
        // Cool-to-white dendritic structure, tipping toward white with density.
        palCol = lerp(spSrgbToLinear(float3(0.3, 0.6, 1.0)), float3(1.0, 1.0, 1.0),
                      saturate(dens * 0.2));
    }

    // The medium the cluster grows in: a very dark radial gradient rather than the
    // flat black the old early-out returned, which also skipped the dither and left
    // the surrounding field banding on any real display.
    float3 col = float3(0.004, 0.006, 0.014) * (1.0 - rad * 0.35);

    // Filament body, brightening without bound where branches pile up.
    col += palCol * body * (0.30 + 0.85 * saturate(dens * 0.22));

    // Edge-lit rim: peaks exactly on the aggregate boundary and falls off both ways,
    // so every tip carries a halo instead of ending on a hard silhouette.
    col += palCol * GlowAmount * 0.45 / (abs(dens) * 0.6 + 1.0);

    // Nucleus as an actual light source.
    col += dlaPalette(0.05) * (0.03 + aBeat * 0.05) * (1.0 + aBeat * 2.0) / max(rad, 0.015);

    col *= Exposure;
    col *= spVignette(uv, VignetteAmt, 0.85);

    // tanh: the rim and nucleus terms are unbounded, and it keeps the warm core warm
    // where ACES would flatten it to white.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
