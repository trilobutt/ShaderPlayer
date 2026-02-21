/*{
    "INPUTS": [
        {"NAME": "PixelSize",  "LABEL": "Pixel Size",   "TYPE": "float",
         "MIN": 1.0, "MAX": 64.0, "STEP": 1.0, "DEFAULT": 8.0},
        {"NAME": "Tint",       "LABEL": "Tint Colour",  "TYPE": "color",
         "DEFAULT": [1.0, 1.0, 1.0, 1.0]},
        {"NAME": "ShowGrid",   "LABEL": "Show Grid",    "TYPE": "bool",
         "DEFAULT": false},
        {"NAME": "Center",     "LABEL": "Offset",       "TYPE": "point2d",
         "MIN": -0.5, "MAX": 0.5, "DEFAULT": [0.0, 0.0]},
        {"NAME": "GridColour", "LABEL": "Grid Colour",  "TYPE": "color",
         "DEFAULT": [0.0, 0.0, 0.0, 1.0]}
    ]
}*/

Texture2D videoTexture : register(t0);
SamplerState videoSampler : register(s0);

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

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv + Center;

    float2 normalised = float2(PixelSize, PixelSize) / resolution;
    float2 pixelUV    = normalised * floor(uv / normalised);

    float4 col = videoTexture.Sample(videoSampler, pixelUV) * Tint;

    // Optional grid lines at pixel boundaries
    if (ShowGrid) {
        float2 frac = frac(uv / normalised);
        float  edge = min(frac.x, frac.y);
        if (edge < 0.05) col = GridColour;
    }

    return col;
}
