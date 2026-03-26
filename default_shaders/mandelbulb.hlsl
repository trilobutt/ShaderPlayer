/*{
    "DESCRIPTION": "Ray-marched 3D Mandelbulb fractal with orbit-trap colouring and ambient occlusion",
    "SHADER_TYPE": "generative",
    "INPUTS": [
        { "NAME": "BulbPower",  "LABEL": "Bulb Power",    "TYPE": "float", "DEFAULT": 8.0, "MIN": 2.0,  "MAX": 16.0, "STEP": 0.1 },
        { "NAME": "DEIter",     "LABEL": "DE Iterations", "TYPE": "long",
          "VALUES": [4,6,8,10,12,16], "LABELS": ["4","6","8","10","12","16"], "DEFAULT": 10  },
        { "NAME": "OrbitSpeed", "LABEL": "Orbit Speed",   "TYPE": "float", "DEFAULT": 0.08,"MIN": 0.0,  "MAX": 1.0,  "STEP": 0.01 },
        { "NAME": "GlowColour", "LABEL": "Glow Colour",   "TYPE": "color", "DEFAULT": [0.2, 0.5, 1.0, 1.0]                    }
    ]
}*/

// ISF packing:
// BulbPower  offset 0 → custom[0].x
// DEIter     offset 1 → int(custom[0].y)
// OrbitSpeed offset 2 → custom[0].z
// (offset 3 is a hole — color requires mult-of-4 alignment, next is offset 4)
// GlowColour offset 4 → custom[1]  (xyzw = rgba)

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

float3 hsv2rgb(float h, float s, float v) {
    float3 p = abs(frac(float3(h, h, h) + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return v * lerp(float3(1, 1, 1), saturate(p - 1.0), s);
}

// Mandelbulb distance estimator.
// Returns the estimated distance to the surface.
// trap receives the minimum orbit trap value (distance to origin) over all iterations.
float deMandelbulb(float3 pos, float pw, int maxIter, out float trap) {
    float3 zv = pos;
    float dr   = 1.0;
    float rLen = 0.0;
    trap = 1e10;

    [loop]
    for (int i = 0; i < maxIter; ++i) {
        rLen = length(zv);
        if (rLen > 2.0) break;

        // Orbit trap: smallest distance to origin
        trap = min(trap, rLen);

        // Convert to spherical coordinates in 3D
        float theta = acos(clamp(zv.z / rLen, -1.0, 1.0));
        float phi   = atan2(zv.y, zv.x);
        float zr    = pow(rLen, pw);

        // Derivative accumulation
        dr = pow(rLen, pw - 1.0) * pw * dr + 1.0;

        // Scale and rotate
        theta *= pw;
        phi   *= pw;

        // Back to cartesian + c = pos
        zv = zr * float3(
            sin(theta) * cos(phi),
            sin(theta) * sin(phi),
            cos(theta)
        ) + pos;
    }

    return 0.5 * log(rLen) * rLen / dr;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float   bulbPow   = custom[0].x;
    int     deIterVal = int(custom[0].y);
    float   orbitSpd  = custom[0].z;
    float4  glowCol   = custom[1];

    // Orbiting camera — slow azimuth + gentle elevation bob
    float camAngle  = time * orbitSpd;
    float camHeight = sin(time * orbitSpd * 0.3) * 0.4;
    float3 camPos   = float3(sin(camAngle) * 2.4, camHeight, cos(camAngle) * 2.4);
    float3 target   = float3(0.0, 0.0, 0.0);

    float3 fw = normalize(target - camPos);
    float3 rt = normalize(cross(float3(0.0, 1.0, 0.0), fw));
    float3 upV = cross(fw, rt);

    float2 screenUV = (input.uv - 0.5) * float2(resolution.x / resolution.y, 1.0);
    float3 rayDir   = normalize(screenUV.x * rt + screenUV.y * upV + fw * 1.4);

    float tRay       = 0.0;
    float stepsTaken = 0.0;
    float trap       = 1e10;
    bool  hitBulb    = false;

    [loop]
    for (int s = 0; s < 80; ++s) {
        float3 pos = camPos + tRay * rayDir;
        float trapVal;
        float distEst = deMandelbulb(pos, bulbPow, deIterVal, trapVal);
        trap = min(trap, trapVal);

        if (distEst < 0.0003) {
            hitBulb = true;
            break;
        }
        if (tRay > 8.0) break;

        tRay        += distEst * 0.7;
        stepsTaken  += 1.0;
    }

    if (!hitBulb) {
        // Deep space background — faint vertical gradient
        float3 bgCol = float3(0.01, 0.02, 0.05) + rayDir.y * 0.02;
        return float4(saturate(bgCol), 1.0);
    }

    // Ambient occlusion from step count, orbit-trap hue
    float ao       = 1.0 - (stepsTaken / 80.0);
    float colParam = frac(trap * 2.0 + time * 0.03);
    float3 baseCol = hsv2rgb(colParam, 0.8, ao);
    float3 finalCol = baseCol * glowCol.rgb * 2.0;

    return float4(saturate(finalCol), 1.0);
}
