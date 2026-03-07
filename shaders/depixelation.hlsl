/*{
    "INPUTS": [
        { "NAME": "Levels",   "LABEL": "Pixel Levels", "TYPE": "long",  "DEFAULT": 4, "VALUES": [2,3,4,5,6], "LABELS": ["2","3","4","5","6"] },
        { "NAME": "Speed",    "LABEL": "Speed",        "TYPE": "float", "DEFAULT": 0.3, "MIN": 0.05, "MAX": 4.0 },
        { "NAME": "LoopMode", "LABEL": "Loop",         "TYPE": "bool",  "DEFAULT": true }
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

// ISF offsets: Levels→[0].x, Speed→[0].y, LoopMode→[0].z

float4 main(PS_INPUT input) : SV_TARGET {
    // Progress [0,1]: where 0 = maximum pixelation, 1 = full resolution
    float progress = LoopMode ? frac(time * Speed) : saturate(time * Speed);

    // Quantise progress into discrete levels: Levels → Levels-1 → ... → 0
    // Each level occupies 1/(Levels+1) of the progress range.
    int lvls     = Levels;
    int levelIdx = max(0, lvls - int(progress * float(lvls + 1)));

    // Block size in pixels: 2^levelIdx (1 = full res, 2^Levels = most pixelated)
    float blkSize = pow(2.0, float(levelIdx));

    float2 normPx  = float2(blkSize, blkSize) / resolution;
    float2 uvPixel = normPx * floor(input.uv / normPx);

    return videoTexture.Sample(videoSampler, uvPixel + normPx * 0.5);
}
