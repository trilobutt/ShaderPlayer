/*{
  "SHADER_TYPE": "video",
  "INPUTS": [
    { "NAME": "SortAxis",     "TYPE": "long",  "MIN": 0,   "MAX": 1,   "DEFAULT": 0,  "LABEL": "Axis (0=Horizontal 1=Vertical)" },
    { "NAME": "ThresholdLow", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.25,"LABEL": "Threshold Low" },
    { "NAME": "ThresholdHigh","TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.85,"LABEL": "Threshold High" },
    { "NAME": "MaxRunLen",    "TYPE": "float", "MIN": 0.01,"MAX": 0.8, "DEFAULT": 0.25,"LABEL": "Max Run Length" },
    { "NAME": "SortDirection","TYPE": "long",  "MIN": 0,   "MAX": 1,   "DEFAULT": 0,  "LABEL": "Direction (0=Fwd 1=Back)" }
  ]
}*/

// Threshold-based pixel sort approximation.
// Pixels whose luminance falls within [ThresholdLow, ThresholdHigh] are
// replaced by a sample taken from further along the sort axis, creating
// the characteristic smeared / streaked glitch aesthetic.
// A noise-modulated run length avoids uniform banding.
// This is a single-pass approximation — not true per-run re-ordering.

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

float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv   = input.uv;
    float4 orig = videoTexture.Sample(videoSampler, uv);
    float  lOrig = luma(orig.rgb);

    // Only pixels within the threshold band are candidates for sorting.
    bool inBand = (lOrig >= ThresholdLow && lOrig <= ThresholdHigh);

    if (!inBand) return orig;

    // Noise-modulated run length: each "run" has a locally variable length.
    // This avoids the rigid banding that a fixed MaxRunLen would produce.
    float noiseUV_x = (SortAxis == 0) ? uv.y : uv.x;
    float runNoise  = noiseTexture.Sample(noiseSampler, float2(noiseUV_x * 2.7, time * 0.1)).r;
    float runLen    = MaxRunLen * (0.3 + runNoise * 0.7);

    // Walk along the sort axis and accumulate the sorted pixel.
    // We approximate sorting by sampling the brightest pixel within the run
    // window (sort key = luminance, ascending).  A single-pass forward scan
    // can't produce a true sorted ordering, so we use a small sample set.
    float2 axisDir = (SortAxis == 0) ? float2(1.0 / resolution.x, 0.0)
                                      : float2(0.0, 1.0 / resolution.y);
    float  axisSign = (SortDirection == 0) ? 1.0 : -1.0;
    axisDir        *= axisSign;

    // Sample N evenly spaced pixels within the run and pick the brightest.
    const int SAMPLES = 8;
    float4 bestPx    = orig;
    float  bestLuma  = -1.0;

    [unroll]
    for (int s = 0; s < SAMPLES; ++s) {
        float  frac2    = (float(s) + 0.5) / float(SAMPLES);
        float2 sampleUV = uv + axisDir * runLen * frac2;
        sampleUV        = saturate(sampleUV);
        float4 spx      = videoTexture.Sample(videoSampler, sampleUV);
        float  sl       = luma(spx.rgb);
        // Only consider pixels also in the threshold band.
        if (sl >= ThresholdLow && sl <= ThresholdHigh && sl > bestLuma) {
            bestLuma = sl;
            bestPx   = spx;
        }
    }

    // If no in-band pixel was found along the run, output original.
    if (bestLuma < 0.0) return orig;

    return bestPx;
}
