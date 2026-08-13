/*{
  "SHADER_TYPE": "video",
  "INPUTS": [
    { "NAME": "SortAxis",     "TYPE": "long",  "VALUES": [0,1], "LABELS": ["Horizontal","Vertical"], "DEFAULT": 0, "LABEL": "Axis" },
    { "NAME": "ThresholdLow", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.25,"LABEL": "Threshold Low" },
    { "NAME": "ThresholdHigh","TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.85,"LABEL": "Threshold High" },
    { "NAME": "Softness",     "TYPE": "float", "MIN": 0.0, "MAX": 0.3, "DEFAULT": 0.04,"LABEL": "Edge Softness" },
    { "NAME": "MaxRunLen",    "TYPE": "float", "MIN": 0.01,"MAX": 0.8, "DEFAULT": 0.25,"LABEL": "Max Run Length" },
    { "NAME": "SortDirection","TYPE": "long",  "VALUES": [0,1], "LABELS": ["Forward","Backward"], "DEFAULT": 0, "LABEL": "Direction" }
  ]
}*/

// Threshold-based pixel sort approximation.
// Pixels whose luminance falls within [ThresholdLow, ThresholdHigh] are replaced
// by the brightest in-band pixel found further along the sort axis, creating the
// characteristic smeared / streaked glitch look. A noise-modulated run length
// avoids uniform banding. This is a single-pass approximation, not true per-run
// re-ordering.
//
// Edge Softness feathers the threshold test. A hard test made the mask boundary
// crawl and flicker frame to frame wherever the source luminance sat near a
// threshold, which on real footage is most of the mid-tones.
//
// The taps are jittered per pixel. Eight evenly spaced taps along the run put a
// visible staircase of repeated colour down every streak, because neighbouring
// pixels sample the same eight positions; an interleaved-gradient offset breaks
// the alignment and the staircase turns into the grain the effect wants anyway.
//
// The sort key is Rec.601 luma on the encoded signal. That is what the threshold
// sliders have always meant, and any saved value would read as a different
// brightness against a linear-light key.
//
// Every fetch is SampleLevel: the run walk and the noise lookup sit after a
// varying test, and implicit-derivative sampling in varying flow control is
// undefined.

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

float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

// Smooth band membership: 1 well inside [lo, hi], 0 well outside.
float bandWeight(float v, float lo, float hi, float soft) {
    float s = max(soft, 1e-4);
    return smoothstep(lo - s, lo + s, v) * (1.0 - smoothstep(hi - s, hi + s, v));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv   = input.uv;
    float3 orig = videoTexture.SampleLevel(videoSampler, uv, 0).rgb;
    float  lOrig = luma(orig);

    float w = bandWeight(lOrig, ThresholdLow, ThresholdHigh, Softness);
    if (w <= 0.001) return float4(orig, 1.0);

    // Noise-modulated run length: each run has a locally variable length, so the
    // streaks do not all stop on the same line.
    float noiseAxis = (SortAxis == 0) ? uv.y : uv.x;
    float runNoise  = noiseTexture.SampleLevel(noiseSampler, float2(noiseAxis * 2.7, time * 0.1), 0).r;
    float runLen    = MaxRunLen * (0.3 + runNoise * 0.7);

    // The run is measured in UV, so the axis is a unit UV direction. Scaling it by
    // one pixel here (1/resolution) capped every tap at a quarter of a pixel and
    // the whole effect resolved to the source image.
    float2 axisDir = (SortAxis == 0) ? float2(1.0, 0.0) : float2(0.0, 1.0);
    axisDir *= (SortDirection == 0) ? 1.0 : -1.0;

    const int SAMPLES = 24;
    float jitter = spIGN(input.pos.xy);

    float3 bestPx   = orig;
    float  bestLuma = -1.0;

    [loop]
    for (int s = 0; s < SAMPLES; ++s) {
        float  f        = (float(s) + jitter) / float(SAMPLES);
        float2 sampleUV = saturate(uv + axisDir * runLen * f);
        float3 spx      = videoTexture.SampleLevel(videoSampler, sampleUV, 0).rgb;
        float  sl       = luma(spx);
        // Only in-band candidates compete, so a run cannot pull a highlight in
        // from outside the band it is supposed to be sorting.
        if (bandWeight(sl, ThresholdLow, ThresholdHigh, Softness) > 0.5 && sl > bestLuma) {
            bestLuma = sl;
            bestPx   = spx;
        }
    }

    // Fade in with band membership rather than switching: the mask edge is where
    // this effect used to show its seams.
    return float4(lerp(orig, bestPx, w), 1.0);
}
