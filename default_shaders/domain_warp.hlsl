/*{
    "DESCRIPTION": "Fractal domain warping (Inigo Quilez fbm style) applied to video, with optional chromatic aberration",
    "INPUTS": [
        { "NAME": "WarpStrength", "LABEL": "Warp Strength",  "TYPE": "float", "DEFAULT": 0.12, "MIN": 0.0, "MAX": 0.5,  "STEP": 0.005 },
        { "NAME": "WarpLayers",   "LABEL": "Warp Layers",    "TYPE": "long",  "DEFAULT": 2,   "MIN": 1,   "MAX": 3               },
        { "NAME": "NoiseFreq",    "LABEL": "Noise Frequency","TYPE": "float", "DEFAULT": 2.5, "MIN": 0.5, "MAX": 10.0, "STEP": 0.1  },
        { "NAME": "WarpSpeed",    "LABEL": "Warp Speed",     "TYPE": "float", "DEFAULT": 0.06,"MIN": 0.0, "MAX": 0.5,  "STEP": 0.01 },
        { "NAME": "ChromaSplit",  "LABEL": "Chroma Split",   "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 0.04, "STEP": 0.001}
    ]
}*/

// ISF packing:
// WarpStrength offset 0 → custom[0].x
// WarpLayers   offset 1 → int(custom[0].y)
// NoiseFreq    offset 2 → custom[0].z
// WarpSpeed    offset 3 → custom[0].w
// ChromaSplit  offset 4 → custom[1].x

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

// 2D vector-valued fbm built from the noise texture R channel.
// Uses two offset samples to approximate a 2D gradient noise output.
float2 fbm2D(float2 p, float tOffset) {
    float2 result = float2(0.0, 0.0);
    float amp  = 0.5;
    float freq = 1.0;

    [loop]
    for (int i = 0; i < 4; ++i) {
        float2 sp = p * freq + float2(tOffset, tOffset * 0.73);
        float nx = noiseTexture.Sample(noiseSampler, sp).r * 2.0 - 1.0;
        float ny = noiseTexture.Sample(noiseSampler, sp + float2(0.43, 0.27)).r * 2.0 - 1.0;
        result += float2(nx, ny) * amp;
        freq *= 2.03;
        amp  *= 0.48;
    }
    return result;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float  warpStr  = custom[0].x;
    int    layerCnt = int(custom[0].y);
    float  noiseF   = custom[0].z;
    float  warpSpd  = custom[0].w;
    float  chromaSp = custom[1].x;

    float2 uv  = input.uv;
    float  tOff = time * warpSpd;

    // Layer 1: single warp
    float2 q = fbm2D(uv * noiseF, tOff);
    float2 warpedUV = uv + warpStr * q;

    // Layer 2: warp the warp
    if (layerCnt >= 2) {
        float2 rr = fbm2D(uv * noiseF + q + float2(1.7, 9.2), tOff);
        warpedUV = uv + warpStr * rr;
    }

    // Layer 3: blend of both warp fields
    if (layerCnt >= 3) {
        float2 ss = fbm2D(uv * noiseF + q * 0.5 + float2(5.2, 1.3), tOff);
        warpedUV = uv + warpStr * (q * 0.5 + ss * 0.5);
    }

    // Chromatic aberration on the warped coordinates
    float3 col;
    col.r = videoTexture.Sample(videoSampler, warpedUV + float2( chromaSp, 0.0)).r;
    col.g = videoTexture.Sample(videoSampler, warpedUV                        ).g;
    col.b = videoTexture.Sample(videoSampler, warpedUV - float2( chromaSp, 0.0)).b;

    return float4(col, 1.0);
}
