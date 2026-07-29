/*{
    "DESCRIPTION": "Fractal domain warping (Inigo Quilez fbm style) applied to video, with optional chromatic aberration",
    "INPUTS": [
        { "NAME": "WarpStrength", "LABEL": "Warp Strength",  "TYPE": "float", "DEFAULT": 0.12, "MIN": 0.0, "MAX": 0.5,  "STEP": 0.005 },
        { "NAME": "WarpLayers",   "LABEL": "Warp Layers",    "TYPE": "long",  "VALUES": [1,2,3], "LABELS": ["1","2","3"], "DEFAULT": 2 },
        { "NAME": "NoiseFreq",    "LABEL": "Noise Frequency","TYPE": "float", "DEFAULT": 2.5, "MIN": 0.5, "MAX": 10.0, "STEP": 0.1  },
        { "NAME": "WarpSpeed",    "LABEL": "Warp Speed",     "TYPE": "float", "DEFAULT": 0.06,"MIN": 0.0, "MAX": 0.5,  "STEP": 0.01 },
        { "NAME": "Detail",       "LABEL": "Fine Detail",    "TYPE": "float", "DEFAULT": 0.3, "MIN": 0.0, "MAX": 1.0,  "STEP": 0.01 },
        { "NAME": "ChromaSplit",  "LABEL": "Chroma Split",   "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 0.04, "STEP": 0.001}
    ]
}*/

// Fractal domain warping over the video: the sampling coordinate is displaced by
// an fbm vector field, optionally by a field that has itself been warped.
//
// Deliberately NOT tonemapped and NOT vignetted: every output value is a resample
// of the source frame, already display-referred and already inside [0,1], so a
// tonemap would only crush footage that was graded correctly to begin with.

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

// 2D vector-valued fbm built from the noise texture R channel.
// Uses two offset samples to approximate a 2D gradient noise output.
// SampleLevel(0) rather than Sample: inside the loop the implicit derivative is
// of a coordinate that has already been scaled by freq, so a mip-mapped fetch
// would silently drop the high octaves exactly where they matter.
float2 fbm2D(float2 p, float tOffset, int octaves, float fineGain) {
    float2 result = float2(0.0, 0.0);
    float amp  = 0.5;
    float freq = 1.0;

    [loop]
    for (int i = 0; i < 6; ++i) {
        if (i >= octaves) break;
        float2 sp = p * freq + float2(tOffset, tOffset * 0.73);
        float nx = noiseTexture.SampleLevel(noiseSampler, sp, 0).r * 2.0 - 1.0;
        float ny = noiseTexture.SampleLevel(noiseSampler, sp + float2(0.43, 0.27), 0).r * 2.0 - 1.0;
        // Octaves past the fourth are the fine detail; Detail fades them in so
        // the warp can be made ropey or smooth without changing its scale.
        float g = (i < 4) ? 1.0 : fineGain;
        result += float2(nx, ny) * amp * g;
        freq *= 2.03;
        amp  *= 0.48;
    }
    return result;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float  tOff = time * WarpSpeed;

    // Two extra octaves whenever Detail is doing anything; skipping them at 0
    // keeps the cheap path cheap.
    int octaves = (Detail > 0.001) ? 6 : 4;

    // Layer 1: single warp
    float2 q = fbm2D(uv * NoiseFreq, tOff, octaves, Detail);
    float2 warpedUV = uv + WarpStrength * q;

    // Layer 2: warp the warp
    if (WarpLayers >= 2) {
        float2 rr = fbm2D(uv * NoiseFreq + q + float2(1.7, 9.2), tOff, octaves, Detail);
        warpedUV = uv + WarpStrength * rr;
    }

    // Layer 3: blend of both warp fields
    if (WarpLayers >= 3) {
        float2 ss = fbm2D(uv * NoiseFreq + q * 0.5 + float2(5.2, 1.3), tOff, octaves, Detail);
        warpedUV = uv + WarpStrength * (q * 0.5 + ss * 0.5);
    }

    // Chromatic aberration along the radius, not along x. Lateral CA in a real
    // lens grows with distance from the optical axis and points away from it; a
    // fixed horizontal split reads as a registration error instead.
    float2 chromaDir = (uv - 0.5) * 2.0 * ChromaSplit;

    float3 col;
    col.r = videoTexture.Sample(videoSampler, warpedUV + chromaDir).r;
    col.g = videoTexture.Sample(videoSampler, warpedUV            ).g;
    col.b = videoTexture.Sample(videoSampler, warpedUV - chromaDir).b;

    // Bilinear resampling of a shallow gradient quantises to fewer levels than
    // the source had; one LSB of triangular noise hides the contours it creates.
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
