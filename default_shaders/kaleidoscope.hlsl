/*{
    "DESCRIPTION": "Polar mirror kaleidoscope: folds the video frame into N rotationally symmetric segments",
    "SHADER_TYPE": "video",
    "INPUTS": [
        { "NAME": "Segments",   "LABEL": "Segments",       "TYPE": "long",    "VALUES": [2,3,4,5,6,7,8,9,10,12,16,20,24], "LABELS": ["2","3","4","5","6","7","8","9","10","12","16","20","24"], "DEFAULT": 6 },
        { "NAME": "RotSpeed",   "LABEL": "Rotation Speed", "TYPE": "float",   "DEFAULT": 0.1,     "MIN": -2.0, "MAX": 2.0, "STEP": 0.01 },
        { "NAME": "ZoomKaleid", "LABEL": "Zoom",           "TYPE": "float",   "DEFAULT": 1.0,     "MIN": 0.1,  "MAX": 4.0, "STEP": 0.05 },
        { "NAME": "EdgeMode",   "LABEL": "Edge Mode",      "TYPE": "long",    "VALUES": [0,1,2], "LABELS": ["Clamp","Mirror","Wrap"], "DEFAULT": 1 },
        { "NAME": "Centre",     "LABEL": "Centre Point",   "TYPE": "point2d", "DEFAULT": [0.5, 0.5], "MIN": 0.0, "MAX": 1.0 }
    ]
}*/

// ISF packing:
// Segments   offset 0 -> int(custom[0].x)
// RotSpeed   offset 1 -> custom[0].y
// ZoomKaleid offset 2 -> custom[0].z
// EdgeMode   offset 3 -> int(custom[0].w)
// Centre     offset 4,5 -> float2(custom[1].x, custom[1].y)
// (the parameters are read through the generated #define aliases, not through
// custom[] directly, so inserting one does not silently re-point the others)
//
// The mirror seams themselves need no filtering: the fold is a reflection, so the
// image is continuous across every seam and only its slope flips. What does need
// handling is the centre, where the angular size of a pixel goes to infinity and
// the segment count exceeds anything the sampling rate can carry. That is
// resolved analytically below, and it is the only real aliasing in the shader.
//
// Fetches are SampleLevel. The folded coordinate is discontinuous at every seam
// and at the edge wrap, so its screen-space derivative there is meaningless, and
// with single-mip textures and a bilinear sampler nothing in the pipeline uses a
// derivative anyway. Minification at high zoom therefore cannot be filtered at
// all; expect sparkle on fine detail pulled in from the frame edges.

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
    float4 custom[8];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float2 applyEdgeMode(float2 uv, int mode) {
    if (mode == 1) {        // mirror
        return abs(frac(uv * 0.5) * 2.0 - 1.0);
    } else if (mode == 2) { // wrap
        return frac(uv);
    }
    return saturate(uv);    // clamp
}

float4 main(PS_INPUT input) : SV_TARGET {
    float  zoomVal  = max(ZoomKaleid, 0.01);
    float2 centreUV = Centre;
    float  ar       = resolution.x / max(resolution.y, 1.0);

    float2 p = (input.uv - centreUV) * float2(ar, 1.0) / zoomVal;

    float r     = length(p);
    float theta = atan2(p.y, p.x) + time * RotSpeed * 0.1;

    // Wrap theta to [0, 2pi] - add a large multiple to avoid fmod on negatives
    theta = fmod(theta + SP_TAU * 32.0, SP_TAU);

    // Fold into one segment via mirror symmetry
    float segAngle = SP_TAU / float(max(Segments, 2));
    theta = fmod(theta, segAngle);
    if (theta > segAngle * 0.5) {
        theta = segAngle - theta;
    }

    float2 newUV = r * float2(cos(theta), sin(theta));
    newUV.x /= ar;
    newUV += centreUV;
    newUV = applyEdgeMode(newUV, EdgeMode);

    float3 col = videoTexture.SampleLevel(videoSampler, newUV, 0).rgb;

    // Centre band-limit. One screen pixel subtends pw/r radians, and once that
    // exceeds half a segment the fold is packing more copies of the source into
    // the pixel than it can hold: the middle of the frame turns into a crawling
    // pinwheel of noise. Fade to the colour at the centre point instead, which is
    // the correct limit of the average as the radius goes to zero.
    float pw   = 1.0 / (resolution.y * zoomVal);
    float angW = pw / max(r, 1e-6);
    float centreFade = saturate(angW / (segAngle * 0.5));
    if (centreFade > 1e-3) {
        float3 hub = videoTexture.SampleLevel(videoSampler, applyEdgeMode(centreUV, EdgeMode), 0).rgb;
        col = lerp(col, hub, centreFade);
    }

    return float4(col, 1.0);
}
