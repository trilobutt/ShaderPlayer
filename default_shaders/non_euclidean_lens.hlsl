/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "curvatureAmt",    "LABEL": "Curvature",     "TYPE": "float",  "MIN": -2.0, "MAX": 2.0,   "DEFAULT": 0.5},
        {"NAME": "driftSpeed",      "LABEL": "Drift Speed",   "TYPE": "float",  "MIN": 0.0,  "MAX": 2.0,   "DEFAULT": 0.2},
        {"NAME": "opticalCentre",   "LABEL": "Optical Centre","TYPE": "point2d","MIN": [0.0,0.0], "MAX": [1.0,1.0], "DEFAULT": [0.5,0.5]},
        {"NAME": "driftAngle",      "LABEL": "Drift Angle",   "TYPE": "float",  "MIN": 0.0,  "MAX": 360.0, "DEFAULT": 45.0},
        {"NAME": "metricType",      "LABEL": "Metric",        "TYPE": "long",
         "VALUES": [0,1,2], "LABELS": ["Spherical","Hyperbolic","Toroidal"], "DEFAULT": 0},
        {"NAME": "samplingMode",    "LABEL": "Edge Mode",     "TYPE": "long",
         "VALUES": [0,1,2], "LABELS": ["Clamp","Mirror","Wrap"], "DEFAULT": 0},
        {"NAME": "animateCurvature","LABEL": "Animate K",     "TYPE": "bool",   "DEFAULT": false}
    ]
}*/

// Non-Euclidean spacetime lens.
// Spherical metric (k>0): stereographic / conformal sphere projection — objects
//   compress toward the antipodal boundary and invert beyond it.
// Hyperbolic metric (k<0): Poincaré-disk-inspired exponential edge stretching —
//   Escher Circle Limit effect applied to live video.
// Toroidal: identification of opposite edges with twist, producing a flat torus warp.
// A geodesic drift continuously moves the optical centre along a great-circle arc.

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

// HLSL has no atanh intrinsic
float myAtanh(float x) {
    x = clamp(x, -0.9999, 0.9999);
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

float2 applyEdgeMode(float2 uv, int mode) {
    if (mode == 1) { // mirror
        uv = abs(frac(uv * 0.5) * 2.0 - 1.0);
    } else if (mode == 2) { // wrap
        uv = frac(uv);
    } else { // clamp
        uv = clamp(uv, 0.0, 1.0);
    }
    return uv;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Optionally animate the curvature magnitude with a slow sinusoid
    float k = curvatureAmt;
    if (animateCurvature) k *= sin(time * 0.4) * 0.5 + 0.5;

    // Geodesic drift: translate the optical centre along a direction over time
    float da = radians(driftAngle);
    float2 drift = float2(cos(da), sin(da)) * driftSpeed * time * 0.02;
    float2 centre = frac(opticalCentre + drift);

    // Map UV to centred coordinates in [-1,1] with aspect correction
    float ar = resolution.x / resolution.y;
    float2 p  = (uv - centre) * float2(ar, 1.0) * 2.0;

    float2 warped = p;

    if (metricType == 0) {
        // Spherical metric: stereographic projection on a unit sphere
        // p maps to sphere via inverse stereographic, then we add curvature-based rotation
        float r2 = dot(p, p);
        float kAbs = abs(k);
        if (kAbs > 0.001) {
            // Stereographic warp: r → r / (1 + k * r²)
            float factor = 1.0 + kAbs * r2;
            warped = p / max(factor, 0.001);
            // For negative k: expand (hyperbolic within the spherical case)
            if (k < 0.0) warped = p * max(factor, 0.001);
        }
    } else if (metricType == 1) {
        // Hyperbolic metric: Poincaré disk model
        // Map to disk, apply Möbius + exponential stretch near boundary
        float diskR = 0.95;
        float rLen  = length(p);
        float normR = rLen / (diskR * max(abs(k), 0.01) + 1.0);
        // Exponential stretch: r → atanh(r/R) * R (geodesic distance in Poincaré disk)
        float stretched = myAtanh(clamp(normR, 0.0, 0.999)) * (diskR + 0.001);
        warped = (rLen > 0.0001) ? (p / rLen) * stretched : p;
    } else {
        // Toroidal: fold coordinates with twist proportional to curvature
        float twist = k * 0.5;
        float angle = atan2(p.y, p.x) + twist * length(p);
        float rad   = length(p) * 0.5;
        // Re-project onto torus surface
        warped = float2(
            frac((cos(angle) * rad + 0.5)),
            frac((sin(angle) * rad + 0.5))
        ) * 2.0 - 1.0;
    }

    // Convert back to UV
    float2 outUV = warped / float2(ar, 1.0) * 0.5 + centre;
    outUV = applyEdgeMode(outUV, samplingMode);

    return videoTexture.Sample(videoSampler, outUV);
}
