/*{
  "SHADER_TYPE": "generative",
  "INPUTS": [
    { "NAME": "SchlaefliP",  "TYPE": "long",  "VALUES": [3,4,5,6,7,8], "LABELS": ["3","4","5","6","7","8"], "DEFAULT": 5, "LABEL": "p (polygon sides)" },
    { "NAME": "SchlaefliQ",  "TYPE": "long",  "VALUES": [3,4,5,6,7,8], "LABELS": ["3","4","5","6","7","8"], "DEFAULT": 4, "LABEL": "q (polygons/vertex)" },
    { "NAME": "MobiusSpeed", "TYPE": "float", "MIN": 0.0,  "MAX": 3.0, "DEFAULT": 0.06,         "LABEL": "Möbius Drift Speed" },
    { "NAME": "EdgeWidth",   "TYPE": "float", "MIN": 0.001,"MAX": 0.03,"DEFAULT": 0.006,        "LABEL": "Edge Width" },
    { "NAME": "TileColourA", "TYPE": "color",                           "DEFAULT": [0.15,0.35,0.75,1], "LABEL": "Tile Colour A" },
    { "NAME": "TileColourB", "TYPE": "color",                           "DEFAULT": [0.75,0.18,0.18,1], "LABEL": "Tile Colour B" }
  ]
}*/

// Conformal Poincaré disc model of the hyperbolic plane.
// Tiles any valid {p, q} Schläfli symbol with (p-2)(q-2) > 4.
// Animated Möbius transformations drift the tiling through H².
// Cells are two-coloured by parity; edges drawn as geodesics.

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

#define PI 3.14159265358979
#define MAX_ITER 40

// Complex multiply: a * b
float2 cmul(float2 a, float2 b) {
    return float2(a.x*b.x - a.y*b.y, a.x*b.y + a.y*b.x);
}

// Möbius transformation: z → (z - w) / (1 - conj(w)*z)
// Maps the Poincaré disc to itself, moving w to the origin.
float2 mobius(float2 z, float2 w) {
    float2 num = z - w;
    float2 den = float2(1.0, 0.0) - cmul(float2(w.x, -w.y), z);
    float  d2  = dot(den, den);
    return cmul(num, float2(den.x, -den.y)) / max(d2, 1e-9);
}

// Geodesic reflection of z across the geodesic that bisects p and q in the disc.
// For the fundamental domain construction we need reflection across a side
// of the fundamental polygon. We use the standard construction:
// reflect across the real axis: conj(z)
float2 reflectReal(float2 z) { return float2(z.x, -z.y); }

// Rotation by angle a.
float2 rot2(float2 z, float a) {
    return float2(z.x * cos(a) - z.y * sin(a),
                  z.x * sin(a) + z.y * cos(a));
}

// tanh is not a ps_5_0 intrinsic — manual implementation
float myTanh(float x) { float e2 = exp(2.0 * x); return (e2 - 1.0) / (e2 + 1.0); }

// Centre of the fundamental {p,q} polygon in the Poincaré disc.
// r = tanh(acosh( cos(PI/q) / sin(PI/p) ) / 2)
float fundamentalR(int p, int q) {
    float sp = sin(PI / float(p));
    float cq = cos(PI / float(q));
    // inradius of the fundamental domain
    float cosA = cq / sp;
    if (cosA <= 1.0) cosA = 1.001;
    float acoshVal = log(cosA + sqrt(cosA * cosA - 1.0));
    return myTanh(acoshVal * 0.5);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Map UV to the unit disc, corrected for aspect.
    float aspect = resolution.x / resolution.y;
    float2 z = (uv - 0.5) * 2.0;
    z.x *= aspect;

    // Points outside the disc are outside hyperbolic space.
    float rSq = dot(z, z);
    if (rSq >= 0.998) {
        return float4(0.02, 0.02, 0.02, 1.0);
    }

    // Animated Möbius drift: a point orbiting near the disc centre.
    float driftAngle = time * MobiusSpeed;
    float driftR     = 0.15 + 0.12 * sin(time * MobiusSpeed * 0.37);
    float2 driftPt   = float2(cos(driftAngle), sin(driftAngle)) * driftR;
    z = mobius(z, driftPt);

    // Fold the point into the fundamental domain of the {p,q} tiling.
    // We track parity to two-colour cells.
    int parity   = 0;
    float fp     = float(SchlaefliP);
    float fq     = float(SchlaefliQ);
    float polyR  = fundamentalR(SchlaefliP, SchlaefliQ);
    float rotAng = 2.0 * PI / fp;

    [loop]
    for (int iter = 0; iter < MAX_ITER; ++iter) {
        float2 centre = float2(polyR, 0.0);
        float2 zr     = mobius(z, centre);

        float ang     = atan2(zr.y, zr.x);
        float sectorF = floor((ang + PI) / rotAng);

        // Each sector step crosses a polygon edge — odd crossings flip parity.
        parity ^= (int(abs(sectorF)) & 1);

        float rotBack = -sectorF * rotAng;
        float2 zRot = rot2(zr, rotBack);
        float2 zOut = mobius(zRot, float2(-polyR, 0.0));

        // Relaxed tolerance so convergence fires reliably.
        if (abs(zOut.y) < 0.002 && zOut.x < polyR * 1.05) break;

        // Reflect across real axis — each reflection also flips parity.
        float2 zRef = reflectReal(zOut);
        parity = 1 - parity;
        z = zRef;

        if (dot(z, z) >= 0.9998) break;
    }

    // Compute distance to the nearest edge in the fundamental domain
    // using the distance to the edge of the fundamental polygon.
    float2 centre2 = float2(polyR, 0.0);
    float2 zLocal  = mobius(z, centre2);
    float  edgeDist = abs(zLocal.y);               // distance to real axis (one edge)
    float  edgeMask = smoothstep(0.0, EdgeWidth, edgeDist);

    // Edge colour is black; tiles alternate between TileColourA and TileColourB.
    float3 tileCol = (parity == 0) ? TileColourA.rgb : TileColourB.rgb;

    // Subtle depth shading by distance from disc centre.
    float disc_r = length(z);
    tileCol *= (0.6 + 0.4 * (1.0 - disc_r));

    float3 col = lerp(float3(0.0, 0.0, 0.0), tileCol, edgeMask);

    return float4(col, 1.0);
}
