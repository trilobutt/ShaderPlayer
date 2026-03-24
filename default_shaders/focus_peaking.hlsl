/*{
    "INPUTS": [
        {"NAME": "Threshold",  "LABEL": "Edge Threshold", "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.15},
        {"NAME": "Opacity",    "LABEL": "Overlay Opacity", "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.85},
        {"NAME": "PeakColor",  "LABEL": "Peak Colour",    "TYPE": "color",
         "DEFAULT": [1.0, 0.0, 0.0, 1.0]}
    ]
}*/

// Focus Peaking
// Runs a Sobel edge-detection filter on luminance and overlays detected edges
// in the chosen colour.  Lower Threshold highlights more of the frame; raise it
// to show only the sharpest regions.
//
// PeakColor starts at cbuffer offset 4 (next 4-aligned slot after two floats).

Texture2D    videoTexture : register(t0);
SamplerState videoSampler : register(s0);

cbuffer Constants : register(b0) {
    float  time;
    float  padding1;
    float2 resolution;
    float2 videoResolution;
    float2 padding2;
    float4 custom[4];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float SampleLuma(float2 uv) {
    return dot(videoTexture.Sample(videoSampler, uv).rgb,
               float3(0.2126, 0.7152, 0.0722));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float4 col = videoTexture.Sample(videoSampler, input.uv);
    float2 tx  = 1.0 / videoResolution;

    // Sobel 3x3 kernel on luminance
    float tl = SampleLuma(input.uv + float2(-tx.x,  tx.y));
    float tm = SampleLuma(input.uv + float2( 0.0,   tx.y));
    float tr = SampleLuma(input.uv + float2( tx.x,  tx.y));
    float ml = SampleLuma(input.uv + float2(-tx.x,  0.0 ));
    float mr = SampleLuma(input.uv + float2( tx.x,  0.0 ));
    float bl = SampleLuma(input.uv + float2(-tx.x, -tx.y));
    float bm = SampleLuma(input.uv + float2( 0.0,  -tx.y));
    float br = SampleLuma(input.uv + float2( tx.x, -tx.y));

    float gx = (-tl + tr) + (-2.0 * ml + 2.0 * mr) + (-bl + br);
    float gy = ( tl + 2.0 * tm + tr) - (bl + 2.0 * bm + br);
    float mag = sqrt(gx * gx + gy * gy);

    if (mag > Threshold) {
        float blend = saturate((mag - Threshold) * 5.0) * Opacity;
        col.rgb = lerp(col.rgb, PeakColor.rgb, blend);
    }

    return col;
}
