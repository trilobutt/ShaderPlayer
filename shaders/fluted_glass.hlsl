/*{
    "INPUTS": [
        { "NAME": "FluteCount",    "LABEL": "Flute Count",    "TYPE": "long",   "DEFAULT": 16,   "VALUES": [4,8,12,16,24,32], "LABELS": ["4","8","12","16","24","32"] },
        { "NAME": "DistortionAmt", "LABEL": "Distortion",     "TYPE": "float",  "DEFAULT": 0.6,  "MIN": 0.0, "MAX": 2.0  },
        { "NAME": "FrostAmount",   "LABEL": "Frost",          "TYPE": "float",  "DEFAULT": 0.05, "MIN": 0.0, "MAX": 1.0  },
        { "NAME": "ChromaticStr",  "LABEL": "Chromatic Aber.","TYPE": "float",  "DEFAULT": 0.02, "MIN": 0.0, "MAX": 0.15 },
        { "NAME": "LightPos",      "LABEL": "Light Position", "TYPE": "point2d","DEFAULT": [0.5, 0.2] },
        { "NAME": "SpecularStr",   "LABEL": "Specular",       "TYPE": "float",  "DEFAULT": 0.6,  "MIN": 0.0, "MAX": 1.0  }
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

// ISF offsets: FluteCount→[0].x, DistortionAmt→[0].y, FrostAmount→[0].z, ChromaticStr→[0].w
//              LightPos→custom[1].xy (point2d, offset 4), SpecularStr→[1].z

float4 main(PS_INPUT input) : SV_TARGET {
    float period = 1.0 / float(FluteCount);

    // Position within the current flute: 0 = left edge, 1 = right edge
    float fluteU   = frac(input.uv.x * float(FluteCount));
    float flutePos = fluteU * 2.0 - 1.0;   // [-1, 1]; 0 = flute centre

    // Cylinder surface normal at this x position
    // A cylinder with radius 1 has normal n = (x, 0, sqrt(1-x²))
    float nz     = sqrt(max(0.0, 1.0 - flutePos * flutePos));
    float3 cylN  = normalize(float3(flutePos, 0.0, nz));

    // UV distortion: normal's x deflects the horizontal sample coordinate
    float2 distUV = input.uv + float2(-flutePos * DistortionAmt * period, 0.0);

    // Frost: noise offsets distUV in both axes (uses Perlin channel = r)
    if (FrostAmount > 0.001) {
        float2 noiseUV    = input.uv * 4.0;
        float2 noiseOff   = noiseTexture.Sample(noiseSampler, noiseUV).rg * 2.0 - 1.0;
        distUV += noiseOff * FrostAmount * period;
    }

    // Chromatic aberration: R and B shifted by flutePos (prismatic spread)
    float cr = ChromaticStr * flutePos;
    float r  = videoTexture.Sample(videoSampler, distUV + float2( cr, 0.0)).r;
    float g  = videoTexture.Sample(videoSampler, distUV).g;
    float bv = videoTexture.Sample(videoSampler, distUV + float2(-cr, 0.0)).b;
    float3 vidCol = float3(r, g, bv);

    // Blinn-Phong specular along the flute ridge
    float3 lightDir = normalize(float3(LightPos.x * 2.0 - 1.0,
                                       LightPos.y * 2.0 - 1.0, 0.8));
    float3 viewDir  = float3(0.0, 0.0, 1.0);
    float3 halfDir  = normalize(lightDir + viewDir);
    float  specPow  = pow(saturate(dot(cylN, halfDir)), 64.0);
    float3 specCol  = float3(specPow, specPow, specPow) * SpecularStr;

    return float4(saturate(vidCol + specCol), 1.0);
}
