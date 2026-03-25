/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "smoothRadius",  "LABEL": "Radius",       "TYPE": "float", "MIN": 1.0, "MAX": 16.0, "DEFAULT": 6.0},
        {"NAME": "orientSigma",   "LABEL": "Orient Sigma", "TYPE": "float", "MIN": 0.1, "MAX": 4.0,  "DEFAULT": 1.5},
        {"NAME": "sectorCount",   "LABEL": "Sectors",      "TYPE": "long",
         "VALUES": [4, 8], "LABELS": ["4 (fast)", "8 (quality)"], "DEFAULT": 8},
        {"NAME": "sharpStrength", "LABEL": "Sharpness",    "TYPE": "float", "MIN": 0.0, "MAX": 2.0,  "DEFAULT": 0.5},
        {"NAME": "satBoost",      "LABEL": "Saturation +", "TYPE": "float", "MIN": 0.5, "MAX": 2.5,  "DEFAULT": 1.2},
        {"NAME": "iterCount",     "LABEL": "Iterations",   "TYPE": "long",
         "VALUES": [1, 2, 3], "LABELS": ["1", "2", "3"], "DEFAULT": 1}
    ]
}*/

// Generalised Kuwahara / structure-tensor oil-paint filter.
// Each pixel outputs the mean of the neighbourhood quadrant with minimum variance.
// The 8-sector variant gives anisotropic strokes aligned to local edge direction.

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

// Compute mean and variance of samples in one Kuwahara sector.
// sampleCount must be a compile-time constant for static array sizing.
void sectorStats(float3 s[9], out float3 outMean, out float outVar) {
    float3 sumC = 0;
    [unroll] for (int i = 0; i < 9; i++) sumC += s[i];
    outMean = sumC / 9.0;
    float3 d = 0;
    [unroll] for (int j = 0; j < 9; j++) { float3 e = s[j] - outMean; d += e * e; }
    outVar = dot(d / 9.0, float3(0.299, 0.587, 0.114));
}

float4 kuwahara(Texture2D tex, SamplerState smp, float2 uv, float2 px, float r, int sectors) {
    float2 pixelStep = px * r;

    // 4-sector Kuwahara: sample 3x3 in each quadrant corner region
    // Quadrant offsets: TL, TR, BL, BR
    float2 origins[4] = { float2(-1,-1), float2(0,-1), float2(-1,0), float2(0,0) };

    float3 bestMean = 0;
    float  bestVar  = 1e9;

    [unroll] for (int q = 0; q < 4; q++) {
        float3 s[9];
        int k = 0;
        [unroll] for (int dy = 0; dy <= 2; dy++) {
            [unroll] for (int dx = 0; dx <= 2; dx++) {
                float2 off = (origins[q] + float2(dx, dy)) * pixelStep;
                s[k++] = tex.Sample(smp, uv + off).rgb;
            }
        }
        float3 mn; float vr;
        sectorStats(s, mn, vr);
        if (vr < bestVar) { bestVar = vr; bestMean = mn; }
    }

    // 8-sector: add diagonal sectors for smoother strokes
    if (sectors == 8) {
        float2 origins8[4] = {
            float2(-0.5,-1), float2(0,-0.5),
            float2(-1,-0.5), float2(-0.5,0)
        };
        [unroll] for (int q8 = 0; q8 < 4; q8++) {
            float3 s8[9];
            int k8 = 0;
            [unroll] for (int dy8 = 0; dy8 <= 2; dy8++) {
                [unroll] for (int dx8 = 0; dx8 <= 2; dx8++) {
                    float2 off = (origins8[q8] + float2(dx8, dy8)) * pixelStep;
                    s8[k8++] = tex.Sample(smp, uv + off).rgb;
                }
            }
            float3 mn8; float vr8;
            sectorStats(s8, mn8, vr8);
            if (vr8 < bestVar) { bestVar = vr8; bestMean = mn8; }
        }
    }
    return float4(bestMean, 1.0);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 px = 1.0 / resolution;

    // Kuwahara pass (iterCount controls how many times we apply it)
    float4 col = videoTexture.Sample(videoSampler, input.uv);
    col = kuwahara(videoTexture, videoSampler, input.uv, px, smoothRadius, sectorCount);
    if (iterCount >= 2) col = kuwahara(videoTexture, videoSampler, input.uv, px, smoothRadius * 0.6, sectorCount);
    if (iterCount >= 3) col = kuwahara(videoTexture, videoSampler, input.uv, px, smoothRadius * 0.3, sectorCount);

    // Sharpening: unsharp mask against original
    float3 orig  = videoTexture.Sample(videoSampler, input.uv).rgb;
    col.rgb = col.rgb + (col.rgb - orig) * (-sharpStrength); // negative = sharpen painted version

    // Saturation boost typical of oil paint
    float lum = dot(col.rgb, float3(0.2126, 0.7152, 0.0722));
    col.rgb = lerp(float3(lum, lum, lum), col.rgb, satBoost);

    col.rgb = saturate(col.rgb);
    col.a   = 1.0;
    return col;
}
