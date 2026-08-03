/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "blockSz",       "LABEL": "Block Size",    "TYPE": "float", "MIN": 4.0,  "MAX": 64.0, "DEFAULT": 16.0},
        {"NAME": "vectorScale",   "LABEL": "Vector Scale",  "TYPE": "float", "MIN": 0.0,  "MAX": 2.0,  "DEFAULT": 0.5},
        {"NAME": "refreshRate",   "LABEL": "Refresh Rate",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.05},
        {"NAME": "blendWeight",   "LABEL": "Blend",         "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.85},
        {"NAME": "errorDiffusion","LABEL": "Error Diffusion","TYPE": "float","MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.3},
        {"NAME": "smearAmt",      "LABEL": "Smear",         "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.5},
        {"NAME": "glitchAmt",     "LABEL": "Glitch Amount", "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.2}
    ]
}*/

// Datamosh block drift.
// Uses Perlin noise (noiseTexture R) as a proxy for P-frame motion vectors.
// Blocks accumulate UV displacement over time; periodic noise spikes simulate
// I-frame resets. errorDiffusion spreads artefacts to neighbouring blocks, and
// Smear trails each block along its own motion vector, which is what the codec's
// repeated prediction from a stale reference actually looks like.
//
// The block edges stay hard, deliberately. They are the artefact: a compression
// block boundary is a hard boundary, and filtering it would turn the effect into
// a generic warp.
//
// Every video fetch is SampleLevel. The displaced coordinate jumps at every block
// edge, so its screen-space derivative there is meaningless; the pipeline's
// textures are single-mip and the sampler is bilinear, so nothing consumes that
// derivative anyway, and asking for level 0 explicitly states what happens rather
// than making the hardware compute a number it will discard.

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

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;
    float2 px = 1.0 / resolution;

    // Snap to block grid
    float2 blockCount = resolution / max(blockSz, 1.0);
    float2 blockCoord = floor(uv * blockCount);
    float2 blockUV    = (blockCoord + 0.5) / blockCount;   // block centre UV

    // Per-block random seed. The sin-based hashes this replaces lose all
    // precision once the block index passes a few hundred, so the top-right of a
    // 4K frame drew from a visibly smaller set of values than the bottom-left.
    float  blockSeed  = spHash12(blockCoord);
    float2 blockRand  = spHash22(blockCoord);

    // "I-frame" reset: a block resets when a noise pulse passes through it
    // refreshRate controls how frequently resets happen
    float resetPhase  = frac(blockSeed + time * refreshRate);
    float isRefreshed = step(0.95, resetPhase);   // 5% of time = fresh

    // Motion vector: sample Perlin noise at the block UV, accumulate over time
    float noiseAmp  = noiseTexture.SampleLevel(noiseSampler, blockUV + float2(time * 0.03, 0.0), 0).r;
    float2 motionVec = (spHash22(blockCoord + floor(time * 0.5)) * 2.0 - 1.0) * vectorScale * noiseAmp;

    // Error diffusion: bleed motion to neighbouring blocks
    float2 neighbourOff = (spHash22(blockCoord + 0.7) * 2.0 - 1.0);
    float2 neighbourCoord = blockCoord + round(neighbourOff);
    float2 nbMV = (spHash22(neighbourCoord + floor(time * 0.5)) * 2.0 - 1.0) * vectorScale * noiseAmp;
    motionVec = lerp(motionVec, nbMV, errorDiffusion * 0.4);

    // Accumulate drift over time (integrate velocity)
    float2 drift  = motionVec * frac(time * 0.2 + blockSeed);
    float2 driftUV = clamp(uv + drift * px * blockSz, 0.0, 1.0);

    // Smear: the codec keeps predicting from the same stale reference, so the
    // block reads as a streak along its vector rather than a clean displacement.
    // Analytic multi-tap along the path: there is no previous frame to feed back.
    float3 driftSample = videoTexture.SampleLevel(videoSampler, driftUV, 0).rgb;
    if (smearAmt > 1e-3) {
        float3 trail = 0.0;
        float  wsum  = 0.0;
        [unroll]
        for (int t = 1; t <= 4; ++t) {
            float f = float(t) / 4.0;
            float w = 1.0 - f * 0.75;
            float2 tapUV = clamp(driftUV - drift * px * blockSz * f * 0.8, 0.0, 1.0);
            trail += videoTexture.SampleLevel(videoSampler, tapUV, 0).rgb * w;
            wsum  += w;
        }
        driftSample = lerp(driftSample, trail / wsum, smearAmt);
    }

    float3 cleanSample = videoTexture.SampleLevel(videoSampler, uv, 0).rgb;

    // On I-frame reset, show clean video; otherwise drifted, then blend the whole
    // thing back against the untouched frame.
    float3 col = lerp(driftSample, cleanSample, isRefreshed);
    col = lerp(cleanSample, col, blendWeight);

    // Glitch: occasional colour channel split on a block. Branchless, because the
    // trigger varies per block and a quad straddling a block edge would take both
    // sides of a real branch.
    float glitchTrigger = step(1.0 - glitchAmt, spHash12(blockCoord + floor(time * 3.0)));
    glitchTrigger *= step(0.01, glitchAmt);
    float2 gOff = float2(blockRand.x - 0.5, 0.0) * vectorScale * px * blockSz * 4.0;
    float  rCh  = videoTexture.SampleLevel(videoSampler, clamp(driftUV + gOff,       0.0, 1.0), 0).r;
    float  bCh  = videoTexture.SampleLevel(videoSampler, clamp(driftUV - gOff * 0.5, 0.0, 1.0), 0).b;
    col.r = lerp(col.r, rCh, glitchTrigger);
    col.b = lerp(col.b, bCh, glitchTrigger);

    return float4(col, 1.0);
}
