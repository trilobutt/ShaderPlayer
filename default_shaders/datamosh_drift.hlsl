/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "blockSz",       "LABEL": "Block Size",    "TYPE": "float", "MIN": 4.0,  "MAX": 64.0, "DEFAULT": 16.0},
        {"NAME": "vectorScale",   "LABEL": "Vector Scale",  "TYPE": "float", "MIN": 0.0,  "MAX": 2.0,  "DEFAULT": 0.5},
        {"NAME": "refreshRate",   "LABEL": "Refresh Rate",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.05},
        {"NAME": "blendWeight",   "LABEL": "Blend",         "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.85},
        {"NAME": "errorDiffusion","LABEL": "Error Diffusion","TYPE": "float","MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.3},
        {"NAME": "glitchAmt",     "LABEL": "Glitch Amount", "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.2}
    ]
}*/

// Datamosh block drift.
// Uses Perlin noise (noiseTexture R) as a proxy for P-frame motion vectors.
// Blocks accumulate UV displacement over time; periodic noise spikes simulate
// I-frame resets.  errorDiffusion spreads artefacts to neighbouring blocks.

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

float h21(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float2 h22(float2 p) {
    return frac(sin(float2(dot(p, float2(127.1,311.7)), dot(p, float2(269.5,183.3)))) * 43758.5453);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;
    float2 px = 1.0 / resolution;

    // Snap to block grid
    float2 blockCount = resolution / max(blockSz, 1.0);
    float2 blockCoord = floor(uv * blockCount);
    float2 blockUV    = (blockCoord + 0.5) / blockCount;   // block centre UV

    // Per-block random seed
    float  blockSeed  = h21(blockCoord);
    float2 blockRand  = h22(blockCoord);

    // "I-frame" reset: a block resets when a noise pulse passes through it
    // refreshRate controls how frequently resets happen
    float resetPhase  = frac(blockSeed + time * refreshRate);
    float isRefreshed = step(0.95, resetPhase);   // 5% of time = fresh

    // Motion vector: sample Perlin noise at the block UV, accumulate over time
    float noiseAmp  = noiseTexture.SampleLevel(noiseSampler, blockUV + float2(time * 0.03, 0.0), 0).r;
    float2 motionVec = (h22(blockCoord + floor(time * 0.5)) * 2.0 - 1.0) * vectorScale * noiseAmp;

    // Error diffusion: bleed motion to neighbouring blocks
    float2 neighbourOff = (h22(blockCoord + 0.7) * 2.0 - 1.0);
    float2 neighbourCoord = blockCoord + round(neighbourOff);
    float2 nbMV = (h22(neighbourCoord + floor(time * 0.5)) * 2.0 - 1.0) * vectorScale * noiseAmp;
    motionVec = lerp(motionVec, nbMV, errorDiffusion * 0.4);

    // Accumulate drift over time (integrate velocity)
    float2 drift = motionVec * frac(time * 0.2 + blockSeed);

    // Displaced UV for the "P-frame" sample
    float2 driftUV = clamp(uv + drift * px * blockSz, 0.0, 1.0);

    float4 driftSample = videoTexture.Sample(videoSampler, driftUV);
    float4 cleanSample = videoTexture.Sample(videoSampler, uv);

    // On I-frame reset, show clean video; otherwise drifted
    float4 col = lerp(driftSample, cleanSample, isRefreshed);

    // Blend drift vs clean
    col = lerp(cleanSample, col, blendWeight);

    // Glitch: occasional colour channel split at high drift blocks
    if (glitchAmt > 0.01) {
        float glitchTrigger = step(1.0 - glitchAmt, h21(blockCoord + floor(time * 3.0)));
        if (glitchTrigger > 0.5) {
            float2 gOff = float2(blockRand.x - 0.5, 0.0) * vectorScale * px * blockSz * 4.0;
            float  rCh  = videoTexture.Sample(videoSampler, clamp(driftUV + gOff,       0.0, 1.0)).r;
            float  bCh  = videoTexture.Sample(videoSampler, clamp(driftUV - gOff * 0.5, 0.0, 1.0)).b;
            col.r = rCh;
            col.b = bCh;
        }
    }

    col.a = 1.0;
    return col;
}
