/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "sliceWidth",         "LABEL": "Slice Width",    "TYPE": "float", "MIN": 0.001, "MAX": 0.1,  "DEFAULT": 0.01},
        {"NAME": "slicePos",           "LABEL": "Slice Position", "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.5},
        {"NAME": "scrollAxis",         "LABEL": "Scroll Axis",    "TYPE": "long",
         "VALUES": [0, 1], "LABELS": ["Vertical", "Horizontal"], "DEFAULT": 0},
        {"NAME": "temporalSpread",     "LABEL": "Temporal Spread","TYPE": "float", "MIN": 0.0,  "MAX": 2.0,  "DEFAULT": 0.8},
        {"NAME": "blendWeight",        "LABEL": "Blend",          "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 1.0},
        {"NAME": "colourPalette",      "LABEL": "Colour Map",     "TYPE": "long",
         "VALUES": [0, 1, 2], "LABELS": ["Original", "Heat", "Monochrome"], "DEFAULT": 0}
    ]
}*/

// Slit-scan temporal splice approximation.
// Without frame history, each column/row encodes a different time phase via UV
// displacement — replicating the streak-photo effect spatially.  The axis
// perpendicular to the scan direction represents time; the scan axis is spatial.

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

float3 heatmap(float t) {
    t = saturate(t);
    return float3(
        smoothstep(0.0, 0.5, t),
        smoothstep(0.25, 0.75, t) * (1.0 - smoothstep(0.75, 1.0, t)),
        1.0 - smoothstep(0.5, 1.0, t)
    );
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Phase encodes temporal offset: pixels far from the scan position have
    // accumulated more "time" — sample the video at a shifted UV to fake history.
    float axis   = (scrollAxis == 0) ? uv.x : uv.y;    // spatial axis
    float perp   = (scrollAxis == 0) ? uv.y : uv.x;    // temporal axis

    // Distance from the slice centre gives the time offset magnitude
    float dist   = perp - slicePos;
    float phase  = dist * temporalSpread;

    // UV displacement: shift scan axis by phase * time, creating streak
    float2 scanUV = uv;
    float  scanOff = frac(phase + time * 0.05) - 0.5;
    if (scrollAxis == 0)
        scanUV.y = uv.y + scanOff * 0.5;
    else
        scanUV.x = uv.x + scanOff * 0.5;

    // Active slice: show clean video where the slice currently is
    bool inSlice = abs(dist) < sliceWidth * 0.5;
    float4 sliceCol = videoTexture.Sample(videoSampler, uv);
    float4 scanCol  = videoTexture.Sample(videoSampler, clamp(scanUV, 0.0, 1.0));

    // Blend scan vs clean based on distance from slice
    float fadeW = exp(-abs(dist) * 6.0);
    float4 col  = lerp(scanCol, sliceCol, fadeW);

    // Colour mapping
    if (colourPalette == 1) {
        float lum = dot(col.rgb, float3(0.299, 0.587, 0.114));
        col.rgb = heatmap(lum);
    } else if (colourPalette == 2) {
        float lum = dot(col.rgb, float3(0.299, 0.587, 0.114));
        col.rgb = float3(lum, lum, lum);
    }

    // Blend with original
    float4 orig = videoTexture.Sample(videoSampler, uv);
    col = lerp(orig, col, blendWeight);
    col.a = 1.0;
    return col;
}
