/*{
    "DESCRIPTION": "Woven crochet fabric illusion. Each pixel cell contains a rotated ellipse whose orientation alternates per cell, coloured by the video. Noise from the global texture adds yarn-like edge roughness; per-cell hue variation gives plastic colour variation.",
    "INPUTS": [
        { "NAME": "PixelSize",     "LABEL": "Cell Size",      "TYPE": "float", "DEFAULT": 14.0, "MIN": 4.0,  "MAX": 48.0, "STEP": 1.0 },
        { "NAME": "EllipseAngle",  "LABEL": "Stitch Angle",   "TYPE": "float", "DEFAULT": 35.0, "MIN": 0.0,  "MAX": 80.0 },
        { "NAME": "NoiseStrength", "LABEL": "Edge Roughness", "TYPE": "float", "DEFAULT": 0.12, "MIN": 0.0,  "MAX": 0.5  },
        { "NAME": "HueVariation",  "LABEL": "Hue Variation",  "TYPE": "float", "DEFAULT": 0.06, "MIN": 0.0,  "MAX": 0.3  },
        { "NAME": "StripeFreq",    "LABEL": "Noise Scale",    "TYPE": "float", "DEFAULT": 6.0,  "MIN": 1.0,  "MAX": 32.0 }
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

// ISF offsets: PixelSize→[0].x, EllipseAngle→[0].y, NoiseStrength→[0].z, HueVariation→[0].w, StripeFreq→custom[1].x

// Compact HSV ↔ RGB helpers (no HLSL intrinsic shadowing)
float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = c.g < c.b ? float4(c.bg, K.wz) : float4(c.gb, K.xy);
    float4 q = c.r < p.x ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 hsv2rgb(float3 c) {
    float4 K  = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p  = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 normPxSize = float2(PixelSize, PixelSize) / resolution;
    float2 cellCoord  = floor(input.uv / normPxSize);
    float2 uvPixel    = normPxSize * (cellCoord + 0.5);

    float4 col   = videoTexture.Sample(videoSampler, uvPixel);
    float2 cellUv = frac(input.uv / normPxSize) - 0.5;  // [-0.5, 0.5] centred

    // Alternating rotation: checkerboard ±EllipseAngle
    float sign     = ((int(cellCoord.x) + int(cellCoord.y)) & 1) == 0 ? 1.0 : -1.0;
    float angleRad = sign * EllipseAngle * 3.14159265 / 180.0;
    float cosA = cos(angleRad), sinA = sin(angleRad);
    float2 rotUv = float2(cosA * cellUv.x - sinA * cellUv.y,
                          sinA * cellUv.x + cosA * cellUv.y);

    // Noise-based edge roughness from t1 (Perlin channel)
    float2 noiseUV   = cellCoord / 64.0 + (cellUv + 0.5) * (StripeFreq / 64.0);
    float  noiseSamp = noiseTexture.Sample(noiseSampler, noiseUV).r;
    float  roughness = (noiseSamp - 0.5) * NoiseStrength;

    // Ellipse test: semi-axes a=0.44 (major), b=0.24 (minor)
    float ea = 0.44, eb = 0.24;
    float ellipseVal = (rotUv.x * rotUv.x) / (ea * ea)
                     + (rotUv.y * rotUv.y) / (eb * eb);
    bool onStitch = ellipseVal < (1.0 + roughness);

    if (!onStitch)
        return float4(0.02, 0.02, 0.02, 1.0);  // dark gap between stitches

    // Per-cell hue variation using cell hash
    float cellHash = frac(sin(dot(cellCoord, float2(127.1, 311.7))) * 43758.5453);
    float3 hsv = rgb2hsv(col.rgb);
    hsv.x = frac(hsv.x + cellHash * HueVariation);
    return float4(hsv2rgb(hsv), 1.0);
}
