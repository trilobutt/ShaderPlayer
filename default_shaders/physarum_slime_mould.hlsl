/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        {"NAME": "sensorAngleDeg", "LABEL": "Sensor Angle",  "TYPE": "float", "MIN": 5.0,  "MAX": 90.0, "DEFAULT": 30.0},
        {"NAME": "sensorDist",     "LABEL": "Sensor Dist",   "TYPE": "float", "MIN": 0.01, "MAX": 0.2,  "DEFAULT": 0.06},
        {"NAME": "rotAngle",       "LABEL": "Turn Angle",    "TYPE": "float", "MIN": 5.0,  "MAX": 90.0, "DEFAULT": 25.0},
        {"NAME": "decayRate",      "LABEL": "Decay Rate",    "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.4},
        {"NAME": "diffRadius",     "LABEL": "Diffuse Radius","TYPE": "float", "MIN": 0.001,"MAX": 0.05, "DEFAULT": 0.01},
        {"NAME": "attractorCount", "LABEL": "Attractors",    "TYPE": "long",
         "VALUES": [4,8,12,16,24,32], "LABELS": ["4","8","12","16","24","32"], "DEFAULT": 12},
        {"NAME": "TrailColour",    "LABEL": "Trail Colour",  "TYPE": "color", "DEFAULT": [0.7,1.0,0.4,1.0]},
        {"NAME": "audioAmount",    "LABEL": "Audio Amount",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.6},
        {"NAME": "GlowAmount",     "LABEL": "Glow",          "TYPE": "float", "MIN": 0.0,  "MAX": 3.0,  "DEFAULT": 0.7},
        {"NAME": "Granularity",    "LABEL": "Granularity",   "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.45},
        {"NAME": "Exposure",       "LABEL": "Exposure",      "TYPE": "float", "MIN": 0.1,  "MAX": 4.0,  "DEFAULT": 1.0},
        {"NAME": "VignetteAmt",    "LABEL": "Vignette",      "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.3},
        {"NAME": "bassIn",         "LABEL": "Bass",          "TYPE": "audio", "BAND": "bass"},
        {"NAME": "highIn",         "LABEL": "Treble",        "TYPE": "audio", "BAND": "high"},
        {"NAME": "beatIn",         "LABEL": "Beat",          "TYPE": "audio", "BAND": "beat"}
    ]
}*/

// Physarum polycephalum transport network (procedural approximation).
// True Physarum requires per-agent state buffers; this shader reproduces the
// characteristic web morphology via Voronoi-ridge SDF: trail tubes are drawn
// along the boundaries between Voronoi cells seeded by attractorCount food
// sources.  sensorAngle and rotAngle modulate the trail width and branching
// curvature via a noise-perturbed Voronoi metric.  Pulsing brightness waves
// travel outward from attractor nodes, matching the observed chemoattractant
// pulse behaviour from Tero et al. (2010).
//
// The tube is a filtered band about the F2-F1 ridge rather than a linear ramp off
// it, so it has an actual edge and that edge is never narrower than a pixel.
// F1 and F2 are both continuous in screen space (only the seed *identity* jumps,
// not the distances), so fwidth is a valid footprint here.
//
// Nodes and tube cores accumulate energy as 1/distance in linear light. That is
// what gives the network a hot core with a real falloff around it; the previous
// exp() envelope produced a soft blob whose brightest point was still bounded by
// the trail colour, so no amount of tuning made a node read as a light source.

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

float2 h22(float2 p) {
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

float h21(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }

// Voronoi: returns F1 (nearest) and F2 (second nearest) distances, and nearest seed
void voronoi(float2 p, float freq, out float F1, out float F2, out float2 nearestSeed) {
    float2 fp   = p * freq;
    int2   cell = int2(floor(fp));
    F1 = 1e9; F2 = 1e9;
    nearestSeed = float2(0, 0);

    [unroll] for (int dy = -2; dy <= 2; dy++) {
        [unroll] for (int dx = -2; dx <= 2; dx++) {
            int2  nb   = cell + int2(dx, dy);
            float2 seed = (float2(nb) + h22(float2(nb))) / freq;
            float  d    = length(p - seed);
            if (d < F1) { F2 = F1; F1 = d; nearestSeed = seed; }
            else if (d < F2) { F2 = d; }
        }
    }
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float  ar  = resolution.x / resolution.y;
    float2 p   = uv * float2(ar, 1.0);

    // --- Audio modulation ---
    // Bass thickens the trails, treble perturbs the network geometry, beats fire a
    // chemoattractant pulse from every node. audioAmount 0 = the static network.
    float aBass = bassIn * audioAmount;
    float aHigh = highIn * audioAmount;
    float aBeat = beatIn * audioAmount;

    // Voronoi frequency: more attractors → denser cells
    float vFreq = sqrt(float(attractorCount)) * 1.5;

    // Noise perturbation controlled by sensor angle (wider angle = more branching distortion)
    float noisePerturb = sensorAngleDeg / 90.0 * 0.08 * (1.0 + aHigh * 2.5);
    float2 noiseOff = (noiseTexture.SampleLevel(noiseSampler, uv * 3.0 + float2(time * 0.02, 0), 0).rg * 2.0 - 1.0)
                      * noisePerturb;

    float F1, F2;
    float2 nearSeed;
    voronoi(p + noiseOff, vFreq, F1, F2, nearSeed);

    // Trail: SDF along the Voronoi edge (F2 - F1 ridge)
    float edgeDist = F2 - F1;
    float trailW   = diffRadius * 0.12 * vFreq * (1.0 + aBass * 2.0);   // width scales with cell size

    // Filter width from the ridge's own screen footprint, and never let the tube be
    // thinner than that, since a sub-pixel tube would otherwise break into dashes.
    float fw    = max(fwidth(edgeDist), 1e-6) * 0.5;
    float tubeW = max(trailW, fw);
    float trail = 1.0 - smoothstep(tubeW - fw, tubeW + fw, edgeDist);

    // Turn-angle affects trail texture via a secondary noise modulation
    float turnNoise = noiseTexture.SampleLevel(noiseSampler, nearSeed + float2(0.3, 0.7), 0).r;
    trail *= 0.7 + turnNoise * 0.3 * (rotAngle / 90.0);

    // Protoplasmic granularity: a fine cellular octave flowing along the network.
    // Without it the tubes are uniform ribbons, which is the one thing real Physarum
    // never looks like, and the flat interior is where the image falls apart on a
    // large display.
    float grain = noiseTexture.SampleLevel(noiseSampler,
                      p * 22.0 + float2(time * 0.05, -time * 0.035), 0).g;
    trail *= lerp(1.0, 0.45 + grain * 1.1, Granularity);

    // Pulsing chemoattractant wave: travels outward from nearest attractor at wave
    // speed. Sensor distance sets the probing scale, hence the wavelength.
    float distToSeed = length(p - nearSeed);
    float waveSpeed  = 0.4 * (1.0 + aBass * 2.0);
    float waveFreq   = 0.4 / max(sensorDist, 0.01);
    float wavePhase  = frac(distToSeed * waveFreq - time * waveSpeed);
    float wavePulse  = exp(-wavePhase * 5.0) * 0.6 + 0.4;

    // Decay: dimmer strands far from last wave front
    float decay = 1.0 - decayRate * (1.0 - wavePulse);

    float3 trailLin = spSrgbToLinear(TrailColour.rgb);

    // Background: a dark medium with a slow large-scale mottle, so the field the
    // network sits in is not a flat plate.
    float bgN = noiseTexture.SampleLevel(noiseSampler, p * 1.3 + float2(time * 0.008, 0), 0).r;
    float3 col = float3(0.002, 0.010, 0.006) * (0.6 + bgN * 1.4);

    // Tube body plus an inverse-distance core along the ridge itself.
    col += trailLin * trail * wavePulse * decay;
    col += trailLin * GlowAmount * 0.35 * tubeW / max(edgeDist, tubeW * 0.35) * decay;

    // Node glow at Voronoi seeds: inverse-distance, twinkling on the beat.
    float2 seedUV   = nearSeed / float2(ar, 1.0);
    float  nodeDist = length(uv - seedUV) * vFreq;
    float  nodeAmp  = 0.6 + 0.4 * sin(time * 3.0 + h21(nearSeed) * 6.28) + aBeat * 0.9;
    float  nodeGlow = nodeAmp * (0.06 + aBeat * 0.05) / max(nodeDist, 0.02);
    col += lerp(trailLin, float3(1.0, 0.82, 0.42), 0.55) * nodeGlow * GlowAmount;

    col *= Exposure;
    col *= spVignette(uv, VignetteAmt, 0.85);

    // tanh: the core and node terms are unbounded, and it keeps the trail hue through
    // the hot centres instead of blowing them to white.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
