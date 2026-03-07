/*{
    "DESCRIPTION": "Lego mosaic effect. Colour-quantises each pixelated cell, adds a dark border simulating brick edges, and renders a 3D-lit circular stud using Blinn-Phong shading. Optional global hue shift for stylised palettes.",
    "INPUTS": [
        { "NAME": "PixelSize",      "LABEL": "Brick Size",     "TYPE": "float",  "DEFAULT": 18.0, "MIN": 8.0,  "MAX": 64.0, "STEP": 1.0 },
        { "NAME": "ColourLevels",   "LABEL": "Colour Steps",   "TYPE": "long",   "DEFAULT": 8,    "VALUES": [4,6,8,12,16], "LABELS": ["4","6","8","12","16"] },
        { "NAME": "BorderStrength", "LABEL": "Border Depth",   "TYPE": "float",  "DEFAULT": 0.6,  "MIN": 0.0,  "MAX": 1.0  },
        { "NAME": "HueShift",       "LABEL": "Hue Shift",      "TYPE": "float",  "DEFAULT": 0.0,  "MIN": -0.5, "MAX": 0.5  },
        { "NAME": "LightPos",       "LABEL": "Light Position", "TYPE": "point2d","DEFAULT": [0.3, 0.7] },
        { "NAME": "StudRadius",     "LABEL": "Stud Radius",    "TYPE": "float",  "DEFAULT": 0.28, "MIN": 0.1,  "MAX": 0.45 },
        { "NAME": "StudHeight",     "LABEL": "Stud Relief",    "TYPE": "float",  "DEFAULT": 0.4,  "MIN": 0.0,  "MAX": 1.0  }
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

// ISF offsets: PixelSize→[0].x, ColourLevels→[0].y, BorderStrength→[0].z, HueShift→[0].w
//              LightPos→custom[1].xy (point2d, offset 4), StudRadius→[1].z, StudHeight→[1].w

float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = c.g < c.b ? float4(c.bg, K.wz) : float4(c.gb, K.xy);
    float4 q = c.r < p.x ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 normPxSize = float2(PixelSize, PixelSize) / resolution;
    float2 uvPixel    = normPxSize * floor(input.uv / normPxSize);

    float4 col  = videoTexture.Sample(videoSampler, uvPixel + normPxSize * 0.5);
    float2 cellUv = frac(input.uv / normPxSize);  // [0,1] within cell

    // Colour quantisation: floor-based bit-depth reduction
    float lvls = float(ColourLevels);
    float3 quantCol = floor(col.rgb * lvls) / lvls;

    // Hue shift
    if (abs(HueShift) > 0.001) {
        float3 hsv = rgb2hsv(quantCol);
        hsv.x = frac(hsv.x + HueShift);
        quantCol = hsv2rgb(hsv);
    }

    // Border: smooth darkening at cell edges (simulates brick recess)
    float edgeDist = min(min(cellUv.x, 1.0 - cellUv.x),
                         min(cellUv.y, 1.0 - cellUv.y));
    float edgeFade = smoothstep(0.0, 0.1, edgeDist);
    float3 brickCol = quantCol * (1.0 - BorderStrength + edgeFade * BorderStrength);

    // Stud: circle centred in cell
    float2 c = cellUv - 0.5;   // [-0.5, 0.5]
    float studDist = length(c);

    if (studDist < StudRadius) {
        // Hemisphere normal from stud surface
        float2 cn = c / StudRadius;            // [-1, 1] on stud
        float  nz = sqrt(max(0.0, 1.0 - dot(cn, cn)));
        float3 N  = normalize(float3(cn.x, -cn.y, nz * max(StudHeight, 0.01)));

        // Blinn-Phong: light comes from LightPos (UV space → direction)
        float3 L = normalize(float3(LightPos.x - 0.5, LightPos.y - 0.5, 0.6));
        float3 V = float3(0.0, 0.0, 1.0);
        float3 H = normalize(L + V);

        float diffuse  = saturate(dot(N, L));
        float specular = pow(saturate(dot(N, H)), 64.0);

        float3 litCol = brickCol * (0.4 + 0.6 * diffuse) + specular * 0.6;
        return float4(saturate(litCol), 1.0);
    }

    return float4(brickCol, 1.0);
}
