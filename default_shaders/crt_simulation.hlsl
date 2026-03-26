/*{
  "SHADER_TYPE": "video",
  "INPUTS": [
    { "NAME": "BarrelStrength", "TYPE": "float", "MIN": 0.0,  "MAX": 0.5,  "DEFAULT": 0.18,  "LABEL": "Barrel Distortion" },
    { "NAME": "MaskType",       "TYPE": "long",  "VALUES": [0,1,2], "LABELS": ["Shadow","Slot","Trinitron"], "DEFAULT": 0, "LABEL": "Mask Type" },
    { "NAME": "ScanlineStr",    "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.35,  "LABEL": "Scanline Strength" },
    { "NAME": "BloomAmount",    "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.25,  "LABEL": "Bloom" },
    { "NAME": "BleedStrength",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.3,   "LABEL": "Colour Bleed" },
    { "NAME": "MaskScale",      "TYPE": "float", "MIN": 0.5,  "MAX": 3.0,  "DEFAULT": 1.0,   "LABEL": "Mask Scale" }
  ]
}*/

// Authentic CRT emulation: barrel distortion, configurable phosphor mask
// (shadow/slot/Trinitron aperture), horizontal scanline darkening, bloom on
// bright regions, and sub-pixel RGB colour bleed.

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

// Barrel / pincushion distortion.
float2 barrelDistort(float2 uv, float k1) {
    float2 c = uv - 0.5;
    float r2 = dot(c, c);
    return 0.5 + c * (1.0 + k1 * r2);
}

// Three phosphor mask patterns, all tiling at sub-pixel scale.
float3 applyMask(float3 col, float2 uv, int mtype, float mscale) {
    float2 pixUV = uv * resolution;
    float px     = fmod(pixUV.x / mscale, 3.0);
    float py     = fmod(pixUV.y / mscale, 2.0);

    float3 mask = float3(1.0, 1.0, 1.0);

    if (mtype == 0) {
        // Shadow mask: circular phosphor dots
        float3 r3 = float3(px - 0.5, px - 1.5, px - 2.5);
        float  py2 = (py - 0.5);
        float dR = r3.x * r3.x + py2 * py2;
        float dG = r3.y * r3.y + py2 * py2;
        float dB = r3.z * r3.z + py2 * py2;
        mask.r = 1.0 - smoothstep(0.18, 0.38, dR);
        mask.g = 1.0 - smoothstep(0.18, 0.38, dG);
        mask.b = 1.0 - smoothstep(0.18, 0.38, dB);
    } else if (mtype == 1) {
        // Slot mask: alternating vertical stripes, offset on alternate rows
        float offset = (floor(py) > 0.5) ? 0.5 : 0.0;
        float pxOff  = fmod(px + offset, 3.0);
        mask = (pxOff < 0.9)  ? float3(1, 0.1, 0.1) :
               (pxOff < 1.8)  ? float3(0.1, 1, 0.1) :
                                  float3(0.1, 0.1, 1);
        mask = lerp(float3(0.3, 0.3, 0.3), mask, 0.8);
    } else {
        // Trinitron aperture grille: vertical stripes, no horizontal breaks
        mask = (px < 0.85)  ? float3(1, 0.08, 0.08) :
               (px < 1.7)   ? float3(0.08, 1, 0.08) :
                               float3(0.08, 0.08, 1);
        mask = lerp(float3(0.2, 0.2, 0.2), mask, 0.85);
    }

    return col * mask;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Barrel distortion.
    float2 warpUV = barrelDistort(uv, BarrelStrength * 0.5);

    // Vignette: pixels outside the distorted frame go black.
    float2 edgeDist = abs(warpUV - 0.5) * 2.0;
    float  vignette = (edgeDist.x < 1.0 && edgeDist.y < 1.0) ? 1.0 : 0.0;
    // Soft vignette at corners.
    float  vigSmooth = smoothstep(1.0, 0.7, max(edgeDist.x, edgeDist.y));

    // Sample with sub-pixel colour bleed: R/B channels sampled at slight offsets.
    float bleedAmt = BleedStrength * 0.003;
    float r = videoTexture.Sample(videoSampler, warpUV + float2(-bleedAmt, 0)).r;
    float g = videoTexture.Sample(videoSampler, warpUV).g;
    float b = videoTexture.Sample(videoSampler, warpUV + float2( bleedAmt, 0)).b;
    float3 col = float3(r, g, b);

    // Bloom: blur bright regions by sampling neighbours.
    float bloomRadius = BloomAmount * 0.012;
    float3 bloomCol = float3(0, 0, 0);
    [unroll]
    for (int bx = -2; bx <= 2; ++bx) {
        for (int by = -1; by <= 1; ++by) {
            float2 bOff = float2(bx, by) * bloomRadius;
            bloomCol += videoTexture.Sample(videoSampler, warpUV + bOff).rgb;
        }
    }
    bloomCol /= 15.0;
    float  luma       = dot(col, float3(0.299, 0.587, 0.114));
    float  bloomMask  = smoothstep(0.5, 0.9, luma);
    col               = lerp(col, col + bloomCol * bloomMask * BloomAmount, BloomAmount);

    // Scanline darkening: dim every other pixel row.
    float scanline = 1.0 - ScanlineStr * (0.5 + 0.5 * sin(uv.y * resolution.y * 3.14159));
    col *= scanline;

    // Phosphor mask.
    col = applyMask(col, warpUV, MaskType, MaskScale);

    // Vignette.
    col *= vigSmooth * vignette;

    return float4(saturate(col), 1.0);
}
