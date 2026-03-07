/*{
    "DESCRIPTION": "SDF-based pixel patterns. Renders a chosen 2D shape (circle, cross, square, or diamond) inside each pixelated cell. Size modulated by luma. Inspired by @hahajohnx.",
    "INPUTS": [
        { "NAME": "PixelSize",    "LABEL": "Pixel Size",   "TYPE": "float", "DEFAULT": 16.0, "MIN": 4.0,  "MAX": 64.0, "STEP": 1.0 },
        { "NAME": "PatternType",  "LABEL": "Pattern",      "TYPE": "long",  "DEFAULT": 0,    "VALUES": [0,1,2,3], "LABELS": ["Circle","Cross","Square","Diamond"] },
        { "NAME": "SdfRadius",    "LABEL": "Base Radius",  "TYPE": "float", "DEFAULT": 0.3,  "MIN": 0.05, "MAX": 0.5  },
        { "NAME": "LumaThresh",   "LABEL": "Luma Cutoff",  "TYPE": "float", "DEFAULT": 0.1,  "MIN": 0.0,  "MAX": 1.0  },
        { "NAME": "FgColor",      "LABEL": "Shape Colour", "TYPE": "color", "DEFAULT": [0.0, 0.31, 0.933, 1.0] },
        { "NAME": "BgColor",      "LABEL": "Background",   "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0] }
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

// ISF offsets: PixelSize→[0].x, PatternType→[0].y, SdfRadius→[0].z, LumaThresh→[0].w
//              FgColor→custom[1], BgColor→custom[2]

// Returns true when cellUv is inside the chosen shape.
bool inShape(float2 p, int ptype, float r) {
    float2 c = p - 0.5;          // centre at (0,0)

    if (ptype == 0) {             // Circle
        return length(c) < r;
    }
    if (ptype == 1) {             // Cross (union of two rectangles)
        float armW = r * 0.5;
        bool horiz = (abs(c.x) < r) && (abs(c.y) < armW);
        bool vert  = (abs(c.x) < armW) && (abs(c.y) < r);
        return horiz || vert;
    }
    if (ptype == 2) {             // Square (Chebyshev)
        return max(abs(c.x), abs(c.y)) < r;
    }
    // Diamond (L1 / Manhattan)
    return (abs(c.x) + abs(c.y)) < r;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 normPxSize = float2(PixelSize, PixelSize) / resolution;
    float2 uvPixel    = normPxSize * floor(input.uv / normPxSize);

    float4 col  = videoTexture.Sample(videoSampler, uvPixel);
    float  luma = dot(float3(0.2126, 0.7152, 0.0722), col.rgb);

    if (luma < LumaThresh)
        return BgColor;

    float2 cellUv = frac(input.uv / normPxSize);

    // Radius scales with luma: brighter → larger shape
    float r = SdfRadius * luma;

    return inShape(cellUv, PatternType, r) ? FgColor : BgColor;
}
