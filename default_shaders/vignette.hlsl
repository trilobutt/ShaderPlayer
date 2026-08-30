/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "Darkens or tints the frame toward its edges. Intensity is how far the corners are pushed toward the vignette colour, Softness the width of the falloff from a hard ring just inside the corners to a gradient starting at the centre, and Roundness runs from a shape that follows the frame to a circle in screen space, which is what a real lens gives and which leaves the left and right edges darker than the top and bottom.",
    "INPUTS": [
        {"NAME": "Intensity",      "LABEL": "Intensity",   "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.5},
        {"NAME": "Softness",       "LABEL": "Softness",    "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.4},
        {"NAME": "Roundness",      "LABEL": "Roundness",   "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.0},
        {"NAME": "VignetteColor",  "LABEL": "Colour",      "TYPE": "color",
         "DEFAULT": [0.0, 0.0, 0.0, 1.0]}
    ]
}*/

// Vignette
// Intensity: how far the corners are pushed toward the vignette colour.
// Softness: width of the falloff. 0 puts a hard ring just inside the corners,
//           1 starts the falloff at the centre.
// Roundness: 0 follows the frame (the falloff reaches all four corners at once),
//            1 is circular in screen space, which is what a real lens does and
//            leaves the left and right edges darker than the top and bottom.
// Colour: what the edges fall toward. Its alpha scales the whole effect.

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

    float ar = resolution.x / max(resolution.y, 1.0);
    float2 d = (input.uv - 0.5) * 2.0;
    d.x *= lerp(1.0, ar, Roundness);

    // Normalised so r == 1 at the corner for any roundness or aspect. Without
    // this the effective intensity changes every time the window is resized.
    float rCorner = length(float2(lerp(1.0, ar, Roundness), 1.0));
    float r = length(d) / max(rCorner, 1e-4);

    // smoothstep, not a power curve: the falloff has to start somewhere definite,
    // and a pow() vignette is already darkening the centre of the frame by the
    // time it is strong enough to read at the corners.
    float v = smoothstep(1.0 - Softness, 1.0 + 1e-3, r) * Intensity * VignetteColor.a;

    // Darkening is a light-transport operation: a lerp toward black on encoded
    // values darkens the midtones far faster than the highlights, which is the
    // muddy, plasticky look of most vignette plug-ins.
    float3 lin = lerp(spSrgbToLinear(col.rgb), spSrgbToLinear(VignetteColor.rgb), v);
    float3 outCol = spLinearToSrgb(lin);

    // A vignette is a full-frame gradient of a few code values per hundred
    // pixels, which is the textbook banding case.
    outCol = spDither(outCol, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(outCol), 1.0);
}
