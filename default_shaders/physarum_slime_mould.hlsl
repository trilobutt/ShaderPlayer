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
        {"NAME": "TrailColour",    "LABEL": "Trail Colour",   "TYPE": "color", "DEFAULT": [0.7,1.0,0.4,1.0]}
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

    // Voronoi frequency: more attractors → denser cells
    float vFreq = sqrt(float(attractorCount)) * 1.5;

    // Noise perturbation controlled by sensor angle (wider angle = more branching distortion)
    float noisePerturb = sensorAngleDeg / 90.0 * 0.08;
    float2 noiseOff = (noiseTexture.SampleLevel(noiseSampler, uv * 3.0 + float2(time * 0.02, 0), 0).rg * 2.0 - 1.0)
                      * noisePerturb;

    float F1, F2;
    float2 nearSeed;
    voronoi(p + noiseOff, vFreq, F1, F2, nearSeed);

    // Trail: SDF along the Voronoi edge (F2 - F1 ridge)
    float edgeDist  = F2 - F1;
    float trailW    = diffRadius * 0.12 * vFreq;   // trail width scales with cell size
    float trailSDF  = smoothstep(trailW, 0.0, edgeDist);

    // Turn-angle affects trail texture via a secondary noise modulation
    float turnNoise = noiseTexture.SampleLevel(noiseSampler, nearSeed + float2(0.3, 0.7), 0).r;
    trailSDF *= 0.7 + turnNoise * 0.3 * (rotAngle / 90.0);

    // Pulsing chemoattractant wave: travels outward from nearest attractor at wave speed
    float distToSeed    = length(p - nearSeed);
    float waveSpeed     = 0.4;
    float waveFreq      = 6.0;
    float wavePhase     = frac(distToSeed * waveFreq - time * waveSpeed);
    float wavePulse     = exp(-wavePhase * 5.0) * 0.6 + 0.4;

    // Decay: dimmer strands far from last wave front
    float decay = 1.0 - decayRate * (1.0 - wavePulse);

    // Node glow at Voronoi seeds
    float2 seedUV  = nearSeed / float2(ar, 1.0);
    float  nodeDist = length(uv - seedUV);
    float  nodeGlow = exp(-nodeDist * vFreq * 12.0) * (0.6 + 0.4 * sin(time * 3.0 + h21(nearSeed) * 6.28));

    // Combine: bright trail + node glow
    float brightness = trailSDF * wavePulse * decay + nodeGlow;

    // Colour: trail colour on dark background
    float3 trailCol = lerp(float3(0.0, 0.05, 0.02),
                           TrailColour.rgb,
                           saturate(brightness));
    trailCol = lerp(trailCol, float3(1.0, 0.9, 0.5), nodeGlow * 0.8);

    return float4(saturate(trailCol), 1.0);
}
