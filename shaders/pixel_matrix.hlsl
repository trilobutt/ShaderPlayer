/*{
    "DESCRIPTION": "Threshold matrix pixel patterns. Luma is compared against an 8x8 fixed matrix to fill each cell with stylised patterns: diagonal stripes, a sine-wave, or Bayer ordered dither. Inspired by @hahajohnx.",
    "INPUTS": [
        { "NAME": "PixelSize",    "LABEL": "Pixel Size",  "TYPE": "float", "DEFAULT": 16.0, "MIN": 8.0, "MAX": 64.0, "STEP": 1.0 },
        { "NAME": "PatternType",  "LABEL": "Pattern",     "TYPE": "long",  "DEFAULT": 0, "VALUES": [0,1,2], "LABELS": ["Stripes","Sine Wave","Bayer Dither"] },
        { "NAME": "FgColor",      "LABEL": "Ink Colour",  "TYPE": "color", "DEFAULT": [0.0, 0.31, 0.933, 1.0] },
        { "NAME": "BgColor",      "LABEL": "Background",  "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0] }
    ]
}*/

Texture2D videoTexture : register(t0);
Texture2D noiseTexture : register(t1);
SamplerState videoSampler : register(s0);
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

// ISF offsets: PixelSize→[0].x, PatternType→[0].y, FgColor→custom[1], BgColor→custom[2]

// Diagonal stripes: two interlocking diagonal stripe matrices.
// Pixel is "on" when matrix[idx] > luma (ink appears in darker areas).
static const float g_stripes[64] = {
    0.2,  1.0,  1.0,  0.2,  0.2,  1.0,  1.0,  0.2,
    0.2,  0.2,  1.0,  1.0,  0.2,  0.2,  1.0,  1.0,
    1.0,  0.2,  0.2,  1.0,  1.0,  0.2,  0.2,  1.0,
    1.0,  1.0,  0.2,  0.2,  1.0,  1.0,  0.2,  0.2,
    0.2,  1.0,  1.0,  0.2,  0.2,  1.0,  1.0,  0.2,
    0.2,  0.2,  1.0,  1.0,  0.2,  0.2,  1.0,  1.0,
    1.0,  0.2,  0.2,  1.0,  1.0,  0.2,  0.2,  1.0,
    1.0,  1.0,  0.2,  0.2,  1.0,  1.0,  0.2,  0.2
};
// Cross-stripe complement (used for bright areas in stripes mode)
static const float g_crossStripe[64] = {
    1.0,  0.2,  0.2,  0.2,  0.2,  0.2,  0.2,  1.0,
    0.2,  1.0,  0.2,  0.2,  0.2,  0.2,  1.0,  0.2,
    0.2,  0.2,  1.0,  0.2,  0.2,  1.0,  0.2,  0.2,
    0.2,  0.2,  0.2,  1.0,  1.0,  0.2,  0.2,  0.2,
    0.2,  0.2,  0.2,  1.0,  1.0,  0.2,  0.2,  0.2,
    0.2,  0.2,  1.0,  0.2,  0.2,  1.0,  0.2,  0.2,
    0.2,  1.0,  0.2,  0.2,  0.2,  0.2,  1.0,  0.2,
    1.0,  0.2,  0.2,  0.2,  0.2,  0.2,  0.2,  1.0
};
// Sine-wave matrix
static const float g_sine[64] = {
    0.99, 0.75, 0.2,  0.2,  0.2,  0.2,  0.99, 0.99,
    0.99, 0.99, 0.75, 0.2,  0.2,  0.99, 0.99, 0.75,
    0.2,  0.99, 0.99, 0.75, 0.99, 0.99, 0.2,  0.2,
    0.2,  0.2,  0.99, 0.99, 0.99, 0.2,  0.2,  0.2,
    0.2,  0.2,  0.2,  0.99, 0.99, 0.99, 0.2,  0.2,
    0.2,  0.2,  0.99, 0.99, 0.75, 0.99, 0.99, 0.2,
    0.75, 0.99, 0.99, 0.2,  0.2,  0.75, 0.99, 0.99,
    0.99, 0.99, 0.2,  0.2,  0.2,  0.2,  0.75, 0.99
};
// Bayer 8x8 ordered dither (values normalised to [0,1])
static const float g_bayer[64] = {
     0.0/64.0, 32.0/64.0,  8.0/64.0, 40.0/64.0,  2.0/64.0, 34.0/64.0, 10.0/64.0, 42.0/64.0,
    48.0/64.0, 16.0/64.0, 56.0/64.0, 24.0/64.0, 50.0/64.0, 18.0/64.0, 58.0/64.0, 26.0/64.0,
    12.0/64.0, 44.0/64.0,  4.0/64.0, 36.0/64.0, 14.0/64.0, 46.0/64.0,  6.0/64.0, 38.0/64.0,
    60.0/64.0, 28.0/64.0, 52.0/64.0, 20.0/64.0, 62.0/64.0, 30.0/64.0, 54.0/64.0, 22.0/64.0,
     3.0/64.0, 35.0/64.0, 11.0/64.0, 43.0/64.0,  1.0/64.0, 33.0/64.0,  9.0/64.0, 41.0/64.0,
    51.0/64.0, 19.0/64.0, 59.0/64.0, 27.0/64.0, 49.0/64.0, 17.0/64.0, 57.0/64.0, 25.0/64.0,
    15.0/64.0, 47.0/64.0,  7.0/64.0, 39.0/64.0, 13.0/64.0, 45.0/64.0,  5.0/64.0, 37.0/64.0,
    63.0/64.0, 31.0/64.0, 55.0/64.0, 23.0/64.0, 61.0/64.0, 29.0/64.0, 53.0/64.0, 21.0/64.0
};

float4 main(PS_INPUT input) : SV_TARGET {
    float2 normPxSize = float2(PixelSize, PixelSize) / resolution;
    float2 uvPixel    = normPxSize * floor(input.uv / normPxSize);

    float4 col  = videoTexture.Sample(videoSampler, uvPixel);
    float  luma = dot(float3(0.2126, 0.7152, 0.0722), col.rgb);

    float2 cellUv = frac(input.uv / normPxSize);
    int mx   = min(int(cellUv.x * 8.0), 7);
    int my   = min(int(cellUv.y * 8.0), 7);
    int midx = my * 8 + mx;

    bool isInk = false;
    if (PatternType == 0) {
        // Stripes: use stripe matrix for dark, cross for bright
        float thresh = (luma < 0.6) ? g_stripes[midx] : g_crossStripe[midx];
        isInk = (thresh > luma);
    } else if (PatternType == 1) {
        isInk = (g_sine[midx] > luma);
    } else {
        // Bayer dither: ink where luma < bayer threshold
        isInk = (luma < g_bayer[midx]);
    }

    return isInk ? FgColor : BgColor;
}
