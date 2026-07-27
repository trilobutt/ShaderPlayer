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
        {"NAME": "bassIn",        "LABEL": "Bass",           "TYPE": "audio", "BAND": "bass"},
        {"NAME": "highIn",        "LABEL": "Treble",         "TYPE": "audio", "BAND": "high"},
        {"NAME": "beatIn",        "LABEL": "Beat",           "TYPE": "audio", "BAND": "beat"},
        {"NAME": "audioAmount",   "LABEL": "Audio Amount",   "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,   "DEFAULT": 0.6}
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
    // (This parameter previously fed a variable that was never read, so it did nothing.)
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
        float  noiseVal = noiseTexture.SampleLevel(noiseSampler, frac(fp * 0.2 + 0.5), 0).r;
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

    // Threshold to make fractal sparse, like actual DLA. The transition band widens
    // with edgeSoftness and never falls below the pixel footprint of `cluster`.
    float threshold = 0.35 * stickingProb * (1.0 - aBeat * 0.35);
    float maskAA    = max(fwidth(cluster), 1e-4) + edgeSoftness * 0.15 + 0.01;
    float mask      = smoothstep(threshold - maskAA, threshold + maskAA, cluster);

    if (mask < 0.002) return float4(0.0, 0.0, 0.0, 1.0);

    // Colouring
    float3 col;
    if (colourByRadius) {
        // Hue encodes radius: centre = warm (red/orange), tips = cool (blue/violet)
        float hue = frac(rad * 0.6 + 0.05);
        col = hsv2rgb(float3(hue, 0.75, mask));
    } else {
        // White/cyan dendritic structure
        col = lerp(float3(0.3, 0.6, 1.0), float3(1.0, 1.0, 1.0), mask) * mask;
    }

    return float4(saturate(col), 1.0);
}
