/*{
    "INPUTS": [
        { "NAME": "PixelSize",   "LABEL": "Pixel Size",    "TYPE": "float", "DEFAULT": 16.0,  "MIN": 4.0, "MAX": 64.0, "STEP": 1.0 },
        { "NAME": "NoiseType",   "LABEL": "Noise Type",    "TYPE": "long",  "DEFAULT": 0, "VALUES": [0,1], "LABELS": ["Perlin","Voronoi"] },
        { "NAME": "NoiseFreq",   "LABEL": "Noise Freq",    "TYPE": "float", "DEFAULT": 6.0,   "MIN": 1.0, "MAX": 24.0 },
        { "NAME": "Contrast",    "LABEL": "Contrast",      "TYPE": "float", "DEFAULT": 1.0,   "MIN": 0.5, "MAX": 3.0  },
        { "NAME": "FgColor",     "LABEL": "Ink Colour",    "TYPE": "color", "DEFAULT": [0.0, 0.0, 0.0, 1.0] },
        { "NAME": "BgColor",     "LABEL": "Paper Colour",  "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0] }
    ]
}*/

Texture2D videoTexture : register(t0);
Texture2D noiseTexture : register(t1);   // Perlin→R, Voronoi→G (globally generated)
SamplerState videoSampler : register(s0);
SamplerState noiseSampler : register(s1); // WRAP addressing

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

// ISF offsets: PixelSize→[0].x, NoiseType→[0].y, NoiseFreq→[0].z, Contrast→[0].w
//              FgColor→custom[1], BgColor→custom[2]

float4 main(PS_INPUT input) : SV_TARGET {
    float2 normPxSize = float2(PixelSize, PixelSize) / resolution;
    float2 uvPixel    = normPxSize * floor(input.uv / normPxSize);

    float4 col  = videoTexture.Sample(videoSampler, uvPixel);
    float  luma = dot(float3(0.2126, 0.7152, 0.0722), col.rgb);

    // Position within the cell drives the noise coordinate (zoomed by NoiseFreq).
    // Offset by cell position so each cell shows a different slice of the noise texture.
    float2 cellUv  = frac(input.uv / normPxSize);
    float2 noiseUV = uvPixel * 0.5 + cellUv * (NoiseFreq / 64.0);

    float4 noiseSample = noiseTexture.Sample(noiseSampler, noiseUV);
    float  noiseVal    = (NoiseType == 0) ? noiseSample.r : noiseSample.g;

    // Darker video cell → lower threshold → noise is "dense" (like a heavy ASCII char).
    // Apply contrast to sharpen the luma→density mapping.
    float threshold = saturate(pow(luma, Contrast));
    bool  isInk     = (noiseVal < threshold);

    // Invert: darker areas should have MORE ink (denser pattern)
    return isInk ? BgColor : FgColor;
}
