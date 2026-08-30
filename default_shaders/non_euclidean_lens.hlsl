/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "Warps live video through a non-Euclidean metric. Spherical (k > 0) is a stereographic conformal sphere projection, compressing objects toward an antipodal boundary and inverting them past it; hyperbolic (k < 0) stretches the edges exponentially, the Escher Circle Limit applied to footage; toroidal identifies opposite edges with a twist for a flat torus wrap. The optical centre drifts along a great-circle geodesic.",
    "INPUTS": [
        {"NAME": "curvatureAmt",    "LABEL": "Curvature",     "TYPE": "float",  "MIN": -2.0, "MAX": 2.0,   "DEFAULT": 0.5},
        {"NAME": "driftSpeed",      "LABEL": "Drift Speed",   "TYPE": "float",  "MIN": 0.0,  "MAX": 2.0,   "DEFAULT": 0.2},
        {"NAME": "opticalCentre",   "LABEL": "Optical Centre","TYPE": "point2d","MIN": 0.0, "MAX": 1.0, "DEFAULT": [0.5,0.5]},
        {"NAME": "driftAngle",      "LABEL": "Drift Angle",   "TYPE": "float",  "MIN": 0.0,  "MAX": 360.0, "DEFAULT": 45.0},
        {"NAME": "metricType",      "LABEL": "Metric",        "TYPE": "long",
         "VALUES": [0,1,2], "LABELS": ["Spherical","Hyperbolic","Toroidal"], "DEFAULT": 0},
        {"NAME": "samplingMode",    "LABEL": "Edge Mode",     "TYPE": "long",
         "VALUES": [0,1,2], "LABELS": ["Clamp","Mirror","Wrap"], "DEFAULT": 0},
        {"NAME": "animateCurvature","LABEL": "Animate K",     "TYPE": "bool",   "DEFAULT": false}
    ]
}*/

// Non-Euclidean spacetime lens.
// Spherical metric (k>0): stereographic / conformal sphere projection, objects
//   compress toward the antipodal boundary and invert beyond it.
// Hyperbolic metric (k<0): Poincare-disk-inspired exponential edge stretching,
//   the Escher Circle Limit effect applied to live video.
// Toroidal: identification of opposite edges with twist, a flat torus warp.
// A geodesic drift moves the optical centre along a great-circle arc.
//
// The lens magnifies in some regions and minifies hard in others (the hyperbolic
// metric compresses the whole plane into a disc). Minification is left
// unfiltered: the video texture is uploaded with a single mip level, so there is
// nothing to select however the footprint is measured, and supersampling here
// would cost more taps than the effect is worth. Expect sparkle at the rim of the
// hyperbolic disc on detailed footage. The fetch is SampleLevel for the same
// reason: the warped coordinate is folded and wrapped before use, so its
// derivative is meaningless, and nothing downstream would consume it.

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

// HLSL has no atanh intrinsic
float myAtanh(float x) {
    x = clamp(x, -0.9999, 0.9999);
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

float2 applyEdgeMode(float2 uv, int mode) {
    if (mode == 1) {        // mirror
        return abs(frac(uv * 0.5) * 2.0 - 1.0);
    } else if (mode == 2) { // wrap
        return frac(uv);
    }
    return saturate(uv);    // clamp
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Optionally animate the curvature magnitude with a slow sinusoid
    float k = curvatureAmt;
    if (animateCurvature) k *= sin(time * 0.4) * 0.5 + 0.5;

    // Geodesic drift. A bounded excursion, not an unbounded ramp through frac():
    // the ramp made the optical centre teleport from one edge of the frame to the
    // other every time it wrapped, and the whole image snapped with it.
    float da = radians(driftAngle);
    float2 drift = float2(cos(da), sin(da)) * 0.25 * sin(time * driftSpeed * 0.3);
    float2 centre = opticalCentre + drift;

    // Map UV to centred coordinates in [-1,1] with aspect correction
    float ar = resolution.x / max(resolution.y, 1.0);
    float2 p  = (uv - centre) * float2(ar, 1.0) * 2.0;

    float2 warped = p;
    bool   torusFold = false;

    if (metricType == 0) {
        // Spherical metric: stereographic projection on a unit sphere
        float r2 = dot(p, p);
        float kAbs = abs(k);
        if (kAbs > 0.001) {
            // Stereographic warp: r -> r / (1 + k * r^2)
            float factor = 1.0 + kAbs * r2;
            warped = p / max(factor, 0.001);
            // For negative k: expand (hyperbolic within the spherical case)
            if (k < 0.0) warped = p * max(factor, 0.001);
        }
    } else if (metricType == 1) {
        // Hyperbolic metric: Poincare disk model
        float diskR = 0.95;
        float rLen  = length(p);
        float normR = rLen / (diskR * max(abs(k), 0.01) + 1.0);
        // Exponential stretch: r -> atanh(r/R) * R (geodesic distance in the disk)
        float stretched = myAtanh(clamp(normR, 0.0, 0.999)) * (diskR + 0.001);
        warped = (rLen > 0.0001) ? (p / rLen) * stretched : p;
    } else {
        // Toroidal: rotate by an angle proportional to the radius, then identify
        // opposite edges. The rotation is applied directly to the direction vector
        // rather than by recovering an angle with atan2 and rebuilding cos/sin
        // from it: same result, one transcendental instead of three, and no 2*pi
        // wrap along -x to reason about.
        float rad = length(p) * 0.5;
        float ang = k * 0.5 * length(p);
        float c = cos(ang), s = sin(ang);
        float2 dir = (rad > 1e-5) ? p / length(p) : float2(1, 0);
        warped = float2(dir.x * c - dir.y * s, dir.x * s + dir.y * c) * rad * 2.0;
        torusFold = true;
    }

    float2 outUV = warped / float2(ar, 1.0) * 0.5 + centre;

    if (torusFold) outUV = frac(outUV);
    outUV = applyEdgeMode(outUV, samplingMode);

    return float4(videoTexture.SampleLevel(videoSampler, outUV, 0).rgb, 1.0);
}
