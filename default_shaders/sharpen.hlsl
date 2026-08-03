/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "Strength", "LABEL": "Strength", "TYPE": "float",
         "MIN": 0.0, "MAX": 3.0, "DEFAULT": 1.0},
        {"NAME": "Radius",   "LABEL": "Radius",   "TYPE": "float",
         "MIN": 0.5, "MAX": 4.0, "DEFAULT": 1.0},
        {"NAME": "Threshold","LABEL": "Threshold","TYPE": "float",
         "MIN": 0.0, "MAX": 0.2, "DEFAULT": 0.01}
    ]
}*/

// Sharpen (unsharp mask)
// Strength: amount of the high-pass added back. 0 = passthrough.
// Radius: blur radius in source pixels. Larger radii sharpen coarser structure.
// Threshold: local contrast below this is left alone, so film grain and sensor
//            noise are not amplified along with the detail.
//
// The high-pass is taken in linear light. Differencing gamma-encoded code values
// weights a shadow edge far more heavily than an identical highlight edge, which
// is why naive sharpeners eat the shadows and barely touch the sky.

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
    float2 uv : TEXCOORD0;
};

// Highlight-only shoulder: identity below the knee, asymptotic to 1 above it.
// A sharpened edge overshoots past white by design; hard-clipping the overshoot
// is what turns a crisp edge into a flat white halo with a visible outline.
float3 softShoulder(float3 x, float knee) {
    float3 over = max(x - knee, 0.0);
    return min(x, knee) + (1.0 - knee) * over / (over + (1.0 - knee));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 texel = Radius / max(videoResolution, 1.0);
    float2 uv    = input.uv;

    // 3x3 tent rather than the 4-neighbour cross: the cross is not isotropic, so
    // a diagonal edge gets a different amount of sharpening than a vertical one
    // and staircases along its length.
    float3 c  = spSrgbToLinear(videoTexture.Sample(videoSampler, uv).rgb);
    float3 blur = c * 4.0;
    blur += spSrgbToLinear(videoTexture.Sample(videoSampler, uv + float2(-texel.x, 0)).rgb) * 2.0;
    blur += spSrgbToLinear(videoTexture.Sample(videoSampler, uv + float2( texel.x, 0)).rgb) * 2.0;
    blur += spSrgbToLinear(videoTexture.Sample(videoSampler, uv + float2(0, -texel.y)).rgb) * 2.0;
    blur += spSrgbToLinear(videoTexture.Sample(videoSampler, uv + float2(0,  texel.y)).rgb) * 2.0;
    blur += spSrgbToLinear(videoTexture.Sample(videoSampler, uv + float2(-texel.x, -texel.y)).rgb);
    blur += spSrgbToLinear(videoTexture.Sample(videoSampler, uv + float2( texel.x, -texel.y)).rgb);
    blur += spSrgbToLinear(videoTexture.Sample(videoSampler, uv + float2(-texel.x,  texel.y)).rgb);
    blur += spSrgbToLinear(videoTexture.Sample(videoSampler, uv + float2( texel.x,  texel.y)).rgb);
    blur /= 16.0;

    float3 high = c - blur;

    // Soft gate on the magnitude of the high-pass. A hard threshold quantises the
    // sharpening into visible islands wherever the contrast crosses the cut.
    float mag  = abs(spLuma(high));
    float gate = smoothstep(Threshold, Threshold + 0.02, mag);

    // 2.0 keeps the default slider position near the punch of the previous
    // 5-tap Laplacian, which applied roughly 4x the raw high-pass.
    float3 lin = c + high * (Strength * 2.0 * gate);

    lin = softShoulder(max(lin, 0.0), 0.85);

    float3 col = spLinearToSrgb(lin);
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
