/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "Monochrome conversion with a choice of weighting. Linear Y linearises first and takes Rec.709 luminance, which is photometrically correct: a saturated blue and the grey of matching brightness land on the same value. Luma Y' uses Rec.601 coefficients on the gamma-encoded signal, wrong as physics but the look broadcast monochrome actually had. Contrast is a gamma about middle grey and Tint multiplies the result in linear light.",
    "INPUTS": [
        {"NAME": "Blend",  "LABEL": "Blend",  "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 1.0},
        {"NAME": "Weighting", "LABEL": "Weighting", "TYPE": "long",
         "VALUES": [0, 1], "LABELS": ["Linear Y (Rec.709)", "Luma Y' (Rec.601)"], "DEFAULT": 0},
        {"NAME": "Contrast", "LABEL": "Contrast", "TYPE": "float",
         "MIN": 0.5, "MAX": 2.0, "DEFAULT": 1.0},
        {"NAME": "Tint",   "LABEL": "Tint",   "TYPE": "color",
         "DEFAULT": [1.0, 1.0, 1.0, 1.0]}
    ]
}*/

// Grayscale
// Blend: 0 = full colour, 1 = full grayscale
// Weighting:
//   Linear Y  - linearise, take Rec.709 luminance, re-encode. This is the
//               photometrically correct conversion: a saturated blue and the grey
//               that matches its brightness map to the same value.
//   Luma Y'   - Rec.601 coefficients on the gamma-encoded signal. Wrong as
//               physics, but it is what SD video hardware did, so it is the
//               look people recognise from broadcast monochrome.
// Contrast: gamma about middle grey, applied to the mono signal only.
// Tint: multiplies the grey, in linear light.

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

float4 main(PS_INPUT input) : SV_TARGET {
    float4 col = videoTexture.Sample(videoSampler, input.uv);

    float3 lin  = spSrgbToLinear(col.rgb);
    float3 grey;

    if (Weighting == 1) {
        // Rec.601 luma on the encoded signal, kept encoded.
        float y = dot(col.rgb, float3(0.299, 0.587, 0.114));
        grey = spSrgbToLinear(float3(y, y, y));
    } else {
        grey = spLuma(lin).xxx;
    }

    // Contrast about 0.18 (middle grey in linear light), not about 0.5. Pivoting
    // on 0.5 linear would sit two stops above mid grey and crush everything below.
    grey = 0.18 * pow(max(grey / 0.18, 1e-6), Contrast);

    grey *= spSrgbToLinear(Tint.rgb);

    // Blend in linear light: a lerp between an encoded colour and its encoded
    // grey dips in brightness at the midpoint, which reads as a dead spot as the
    // slider crosses 0.5.
    float3 outLin = lerp(lin, grey, Blend);

    // No tonemap: nothing here can exceed the input range except Tint and
    // Contrast, and a monochrome conversion is expected to be a pass-through of
    // the source's own grade rather than a re-grade of it. Clamp is the honest
    // behaviour for the two controls that can overshoot.
    float3 outCol = spLinearToSrgb(saturate(outLin));

    // The linear round-trip quantises hardest in the shadows, where the encode
    // curve is steepest; without dither a graded sky bands visibly.
    outCol = spDither(outCol, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(outCol), col.a);
}
