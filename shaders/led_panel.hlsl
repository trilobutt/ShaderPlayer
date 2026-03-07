/*{
    "INPUTS": [
        { "NAME": "PixelSize",    "LABEL": "Cell Size",     "TYPE": "float", "DEFAULT": 12.0, "MIN": 4.0,  "MAX": 64.0, "STEP": 1.0 },
        { "NAME": "MaskStagger",  "LABEL": "Stagger",       "TYPE": "bool",  "DEFAULT": true },
        { "NAME": "MaskBorder",   "LABEL": "Border",        "TYPE": "float", "DEFAULT": 0.12, "MIN": 0.0,  "MAX": 0.45 },
        { "NAME": "MaskIntensity","LABEL": "Brightness",    "TYPE": "float", "DEFAULT": 1.5,  "MIN": 0.5,  "MAX": 3.0  },
        { "NAME": "BgLift",       "LABEL": "Ambient Lift",  "TYPE": "float", "DEFAULT": 0.04, "MIN": 0.0,  "MAX": 0.3  }
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

// ISF offsets: PixelSize→[0].x, MaskStagger→[0].y, MaskBorder→[0].z, MaskIntensity→[0].w, BgLift→custom[1].x

float4 main(PS_INPUT input) : SV_TARGET {
    float2 normPxSize = float2(PixelSize, PixelSize) / resolution;

    // Stagger: odd columns shift down by half a cell
    float2 cellCoord = floor(input.uv / normPxSize);
    float2 uvS = input.uv;
    if (MaskStagger && (int(cellCoord.x) & 1) == 1)
        uvS.y += normPxSize.y * 0.5;

    float2 uvPixel = normPxSize * floor(uvS / normPxSize);
    float4 col = videoTexture.Sample(videoSampler, uvPixel + normPxSize * 0.5);

    float2 cellUv = frac(uvS / normPxSize);

    // Split cell into three horizontal sub-pixels: R (left), G (mid), B (right)
    float subXf  = cellUv.x * 3.0;
    int   subIdx = min(int(subXf), 2);
    float subLoc = frac(subXf);   // [0,1] within the sub-pixel

    // Dark mask between sub-pixels and around the cell perimeter
    float bord = MaskBorder;
    bool inMask = (subLoc < bord) || (subLoc > 1.0 - bord)
               || (cellUv.y < bord) || (cellUv.y > 1.0 - bord);

    float3 bg = float3(BgLift, BgLift, BgLift);
    if (inMask)
        return float4(bg, 1.0);

    // Each sub-pixel emits only its channel, boosted by MaskIntensity
    float chVal = (subIdx == 0) ? col.r : (subIdx == 1) ? col.g : col.b;
    chVal = saturate(chVal * MaskIntensity);

    float3 ledCol = (subIdx == 0) ? float3(chVal, 0.0, 0.0)
                  : (subIdx == 1) ? float3(0.0, chVal, 0.0)
                                  : float3(0.0, 0.0, chVal);

    return float4(saturate(bg + ledCol), 1.0);
}
