/*{
    "DESCRIPTION": "Polar mirror kaleidoscope — folds the video frame into N rotationally symmetric segments",
    "INPUTS": [
        { "NAME": "Segments",   "LABEL": "Segments",       "TYPE": "long",    "DEFAULT": 6,       "MIN": 2,    "MAX": 24              },
        { "NAME": "RotSpeed",   "LABEL": "Rotation Speed", "TYPE": "float",   "DEFAULT": 0.1,     "MIN": -2.0, "MAX": 2.0, "STEP": 0.01 },
        { "NAME": "ZoomKaleid", "LABEL": "Zoom",           "TYPE": "float",   "DEFAULT": 1.0,     "MIN": 0.1,  "MAX": 4.0, "STEP": 0.05 },
        { "NAME": "Centre",     "LABEL": "Centre Point",   "TYPE": "point2d", "DEFAULT": [0.5, 0.5], "MIN": [0.0, 0.0], "MAX": [1.0, 1.0] }
    ]
}*/

// ISF packing:
// Segments   offset 0 → int(custom[0].x)
// RotSpeed   offset 1 → custom[0].y
// ZoomKaleid offset 2 → custom[0].z
// (offset 3 is a hole — point2d requires even alignment, next even offset is 4)
// Centre     offset 4,5 → float2(custom[1].x, custom[1].y)

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

float4 main(PS_INPUT input) : SV_TARGET {
    int   segCount  = int(custom[0].x);
    float rotSpd    = custom[0].y;
    float zoomVal   = custom[0].z;
    float2 centreUV = float2(custom[1].x, custom[1].y);

    static const float TWO_PI = 6.28318530718;

    float2 uv = input.uv - centreUV;
    // Correct for pixel aspect ratio in polar space
    uv.x *= resolution.x / resolution.y;
    uv /= zoomVal;

    float r     = length(uv);
    float theta = atan2(uv.y, uv.x) + time * rotSpd * 0.1;

    // Wrap theta to [0, 2π] — add large multiple to avoid fmod on negatives
    theta = fmod(theta + TWO_PI * 32.0, TWO_PI);

    // Fold into one segment via mirror symmetry
    float segAngle = TWO_PI / float(segCount);
    theta = fmod(theta, segAngle);
    if (theta > segAngle * 0.5) {
        theta = segAngle - theta;
    }

    // Reconstruct cartesian UV for video sampling
    float2 newUV = r * float2(cos(theta), sin(theta));
    // Undo aspect ratio correction
    newUV.x /= (resolution.x / resolution.y);
    newUV += centreUV;

    float3 col = videoTexture.Sample(videoSampler, newUV).rgb;
    return float4(col, 1.0);
}
