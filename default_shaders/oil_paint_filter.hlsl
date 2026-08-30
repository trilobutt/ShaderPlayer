/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "Generalised Kuwahara filter. Each pixel takes a weighted mean of overlapping neighbourhood sectors, weighted against each sector's own variance, so a pixel near an edge is filled from whichever side of that edge is flatter and the edge itself stays sharp while flat areas turn into brushstrokes. Edge Selectivity is the exponent on that weighting, running from a plain box blur to the classic blocky hard-minimum Kuwahara; Detail Scales blends several radii for a coarse-over-fine paint build.",
    "INPUTS": [
        {"NAME": "smoothRadius",  "LABEL": "Radius",          "TYPE": "float", "MIN": 1.0, "MAX": 16.0, "DEFAULT": 6.0},
        {"NAME": "orientSigma",   "LABEL": "Edge Selectivity","TYPE": "float", "MIN": 0.1, "MAX": 4.0,  "DEFAULT": 1.5},
        {"NAME": "sectorCount",   "LABEL": "Sectors",         "TYPE": "long",
         "VALUES": [4, 8], "LABELS": ["4 (fast)", "8 (quality)"], "DEFAULT": 8},
        {"NAME": "sharpStrength", "LABEL": "Sharpness",       "TYPE": "float", "MIN": 0.0, "MAX": 2.0,  "DEFAULT": 0.5},
        {"NAME": "satBoost",      "LABEL": "Saturation +",    "TYPE": "float", "MIN": 0.5, "MAX": 2.5,  "DEFAULT": 1.2},
        {"NAME": "iterCount",     "LABEL": "Detail Scales",   "TYPE": "long",
         "VALUES": [1, 2, 3], "LABELS": ["1 (broad)", "2", "3 (fine)"], "DEFAULT": 1},
        {"NAME": "bristleAmt",    "LABEL": "Bristle",         "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.25}
    ]
}*/

// Generalised Kuwahara oil-paint filter.
// Each pixel takes a weighted mean of the overlapping neighbourhood sectors,
// weighted against each sector's own variance, so a pixel near an edge is filled
// from whichever side of the edge is flatter and the edge itself stays sharp.
//
// Edge Selectivity is the exponent on that weighting. The classic Kuwahara takes
// a hard minimum, which switches discontinuously as the window slides and tiles
// flat areas with visible rectangular patches; a soft weighting is the standard
// fix and is what this parameter drives. High values approach the hard minimum
// (crisp, blocky), low values approach a plain box blur.
//
// Detail Scales runs the filter at successively smaller radii and blends the
// results, weighted toward the coarsest. It previously ran the extra passes and
// threw the earlier result away, so the control only ever changed the radius.
//
// Sharpness is a true local contrast boost of the painted result against the box
// mean of the same window. It previously subtracted the difference between the
// painted result and the source, which lerps back toward the untouched video:
// the slider undid the effect rather than sharpening it.
//
// All statistics are computed in linear light (gamma 2.0: square in, sqrt out,
// exact when the filter is a no-op). Means of gamma-encoded values across an edge
// land well below the true mean radiance, which is what makes naive painterly
// filters look chalky.

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

// Mean and luminance-weighted variance of one 3x3 sector.
void sectorStats(float3 s[9], out float3 outMean, out float outVar) {
    float3 sumC = 0;
    [unroll] for (int i = 0; i < 9; i++) sumC += s[i];
    outMean = sumC / 9.0;
    float3 d = 0;
    [unroll] for (int j = 0; j < 9; j++) { float3 e = s[j] - outMean; d += e * e; }
    outVar = dot(d / 9.0, float3(0.299, 0.587, 0.114));
}

// tone = variance-weighted sector mean, box = plain mean of all sectors.
void kuwahara(float2 uv, float2 pixelStep, int sectors, float q,
              out float3 tone, out float3 box) {
    float3 means[8];
    float  vars[8];
    [unroll] for (int c = 0; c < 8; c++) { means[c] = 0.0; vars[c] = 1e9; }

    // Four overlapping quadrant windows: TL, TR, BL, BR.
    float2 origins[4] = { float2(-1,-1), float2(0,-1), float2(-1,0), float2(0,0) };

    [unroll] for (int a = 0; a < 4; a++) {
        float3 s[9];
        int k = 0;
        [unroll] for (int dy = 0; dy <= 2; dy++) {
            [unroll] for (int dx = 0; dx <= 2; dx++) {
                float3 t = videoTexture.SampleLevel(videoSampler,
                            uv + (origins[a] + float2(dx, dy)) * pixelStep, 0).rgb;
                s[k++] = t * t;
            }
        }
        sectorStats(s, means[a], vars[a]);
    }

    // Four half-offset windows: diagonal coverage, so an edge at 45 degrees has a
    // sector that lies along it rather than four that straddle it.
    if (sectors == 8) {
        float2 origins8[4] = {
            float2(-0.5,-1), float2(0,-0.5),
            float2(-1,-0.5), float2(-0.5,0)
        };
        [unroll] for (int b = 0; b < 4; b++) {
            float3 s8[9];
            int k8 = 0;
            [unroll] for (int dy8 = 0; dy8 <= 2; dy8++) {
                [unroll] for (int dx8 = 0; dx8 <= 2; dx8++) {
                    float3 t = videoTexture.SampleLevel(videoSampler,
                                uv + (origins8[b] + float2(dx8, dy8)) * pixelStep, 0).rgb;
                    s8[k8++] = t * t;
                }
            }
            sectorStats(s8, means[b + 4], vars[b + 4]);
        }
    }

    // Normalise on the smallest variance before the power, so the exponent cannot
    // overflow on a flat region where every variance is ~1e-6.
    float vmin = 1e9;
    [unroll] for (int m = 0; m < 8; m++) vmin = min(vmin, vars[m]);
    vmin = max(vmin, 1e-6);

    float3 acc = 0.0;
    float  wsum = 0.0;
    box = 0.0;
    float bcount = 0.0;

    [unroll] for (int n = 0; n < 8; n++) {
        float active = (vars[n] < 1e8) ? 1.0 : 0.0;
        float w = pow(max(vars[n], 1e-6) / vmin, -q) * active;
        acc  += means[n] * w;
        wsum += w;
        box  += means[n] * active;
        bcount += active;
    }

    tone = acc / max(wsum, 1e-6);
    box  = box / max(bcount, 1.0);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 px = 1.0 / resolution;

    // Bristle: displace the whole window by a low-frequency noise offset, so the
    // stroke boundaries wander instead of following the pixel grid. This is the
    // difference between a filter and a brush.
    float2 uv = input.uv;
    if (bristleAmt > 1e-3) {
        float2 n = noiseTexture.SampleLevel(noiseSampler, input.uv * 3.0, 0).rg;
        uv += (n - 0.5) * bristleAmt * px * smoothRadius * 2.0;
    }

    float q = orientSigma * 2.0;

    float3 tone, box;
    kuwahara(uv, px * smoothRadius, sectorCount, q, tone, box);

    // Multi-scale: coarse structure first, finer strokes laid over it.
    if (iterCount >= 2) {
        float3 t2, b2;
        kuwahara(uv, px * smoothRadius * 0.55, sectorCount, q, t2, b2);
        tone = lerp(tone, t2, 0.4);
        box  = lerp(box,  b2, 0.4);
    }
    if (iterCount >= 3) {
        float3 t3, b3;
        kuwahara(uv, px * smoothRadius * 0.28, sectorCount, q, t3, b3);
        tone = lerp(tone, t3, 0.3);
        box  = lerp(box,  b3, 0.3);
    }

    // Local contrast against the box mean of the same window: the impasto ridge.
    float3 lin = max(tone + (tone - box) * sharpStrength, 0.0);

    float3 col = sqrt(lin);

    // Saturation toward a grey of the same linear luminance.
    float3 grey = sqrt(spLuma(lin).xxx);
    col = lerp(grey, col, satBoost);

    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
