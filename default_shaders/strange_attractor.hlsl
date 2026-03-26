/*{
  "SHADER_TYPE": "generative",
  "INPUTS": [
    { "NAME": "AttractorType", "TYPE": "long",
      "VALUES": [0,1,2,3], "LABELS": ["Lorenz","Rössler","Thomas","Halvorsen"], "DEFAULT": 0, "LABEL": "Attractor" },
    { "NAME": "IterCount",     "TYPE": "long",
      "VALUES": [60,120,180,240,300,400], "LABELS": ["60","120","180","240","300","400"], "DEFAULT": 180, "LABEL": "Iterations" },
    { "NAME": "ViewScale",     "TYPE": "float", "MIN": 0.005,"MAX": 0.08,"DEFAULT": 0.024,"LABEL": "View Scale" },
    { "NAME": "PointBright",   "TYPE": "float", "MIN": 0.2,  "MAX": 6.0, "DEFAULT": 2.5, "LABEL": "Point Brightness" },
    { "NAME": "RotationSpeed", "TYPE": "float", "MIN": 0.0,  "MAX": 5.0, "DEFAULT": 0.1, "LABEL": "Rotation Speed" },
    { "NAME": "ColourMode",    "TYPE": "long",
      "VALUES": [0,1,2], "LABELS": ["Velocity","Time","Depth"], "DEFAULT": 0, "LABEL": "Colour Mode" }
  ]
}*/

// Chaotic ODE integration rendered as a per-pixel density accumulator.
// Each pixel is seeded at a phase-space position matching its screen coordinate.
// The trajectory is integrated forward; accumulated closeness to the screen
// plane at each step lights the pixel — mapping attractor density to brightness.
// Attractor selectable: Lorenz (xz butterfly), Rössler, Thomas, Halvorsen.

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

float3 hsv2rgb(float h, float s, float v) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(h + K.xyz) * 6.0 - K.www);
    return v * lerp(K.xxx, saturate(p - K.xxx), s);
}

float3 lorenzDeriv(float3 p) {
    return float3(10.0 * (p.y - p.x),
                  p.x * (28.0 - p.z) - p.y,
                  p.x * p.y - (8.0 / 3.0) * p.z);
}
float3 rosslerDeriv(float3 p) {
    return float3(-p.y - p.z,
                   p.x + 0.2 * p.y,
                   0.2 + p.z * (p.x - 5.7));
}
float3 thomasDeriv(float3 p) {
    return float3(sin(p.y) - 0.208186 * p.x,
                  sin(p.z) - 0.208186 * p.y,
                  sin(p.x) - 0.208186 * p.z);
}
float3 halvorsenDeriv(float3 p) {
    return float3(-1.4 * p.x - 4.0 * p.y - 4.0 * p.z - p.y * p.y,
                  -1.4 * p.y - 4.0 * p.z - 4.0 * p.x - p.z * p.z,
                  -1.4 * p.z - 4.0 * p.x - 4.0 * p.y - p.x * p.x);
}

float3 getDeriv(float3 p) {
    if (AttractorType == 1) return rosslerDeriv(p);
    if (AttractorType == 2) return thomasDeriv(p);
    if (AttractorType == 3) return halvorsenDeriv(p);
    return lorenzDeriv(p);
}

// Attractor-specific coordinate centres and scales for display.
// Returns: float3(xCentre, yCentre_or_zCentre, viewScale_mult)
float3 attractorConfig() {
    // Lorenz: project in xz plane, centred at (0, 25)
    if (AttractorType == 0) return float3(0.0, 25.0, 1.0);
    // Rössler: project in xy plane, centred at (0, 0)
    if (AttractorType == 1) return float3(0.0, 0.0,  0.6);
    // Thomas: symmetric, centre at (0, 0)
    if (AttractorType == 2) return float3(0.0, 0.0,  0.25);
    // Halvorsen: centre at (0, 0)
    return float3(0.0, 0.0, 0.15);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Camera rotation angle (slow drift to expose 3D structure).
    float camAngle = time * RotationSpeed;
    float cosA     = cos(camAngle);
    float sinA     = sin(camAngle);

    float3 cfg = attractorConfig();

    // Map pixel UV to a phase-space starting position.
    // X and Z (or X and Y for Rössler) span the visible range.
    float halfRange = 1.0 / max(ViewScale, 0.001) * 0.5;
    float ax = (uv.x - 0.5) * halfRange * 2.0;
    float az = (uv.y - 0.5) * halfRange * 2.0 + cfg.y;

    float3 p;
    if (AttractorType == 0) {
        p = float3(ax, 0.0, az);         // Lorenz xz seed (y=0)
    } else if (AttractorType == 1) {
        p = float3(ax, az - cfg.y, 0.5); // Rössler xy seed
    } else if (AttractorType == 2) {
        p = float3(ax, az - cfg.y, 1.0); // Thomas
    } else {
        p = float3(ax, az - cfg.y, 1.0); // Halvorsen
    }

    float dt = 0.01;
    float acc  = 0.0;
    float accC = 0.0;

    [loop]
    for (int i = 0; i < IterCount; ++i) {
        float3 dp = getDeriv(p);
        p        += dp * dt;

        // Project attractor point onto the 2D screen plane.
        // For Lorenz: rotate in the xz plane; for others: rotate in xy.
        float projA, projB;
        if (AttractorType == 0) {
            float xC   = p.x;
            float zC   = p.z - cfg.y;
            projA = xC * cosA - zC * sinA;
            projB = xC * sinA + zC * cosA;
        } else {
            projA = p.x * cosA - p.y * sinA;
            projB = p.x * sinA + p.y * cosA;
        }

        // Screen UV of this trajectory point.
        float2 sUV  = float2(projA, projB) * ViewScale + 0.5;
        float2 dUV  = uv - sUV;
        // Aspect-correct distance.
        dUV.x      *= resolution.x / resolution.y;
        float dist2 = dot(dUV, dUV);
        float w     = exp(-dist2 * 5000.0);

        float velMag = length(dp);
        float cParam = (ColourMode == 0) ? saturate(velMag / 25.0)
                     : (ColourMode == 1) ? float(i) / float(IterCount)
                     : (AttractorType == 0) ? saturate(p.z / 50.0)
                     :                        saturate((p.z + 5.0) / 10.0);

        acc  += w;
        accC += w * cParam;
    }

    float density  = saturate(acc * PointBright * 0.1);
    float colParam = (acc > 0.0001) ? (accC / acc) : 0.0;

    float3 col = hsv2rgb(colParam * 0.65 + 0.55, 0.85, density);

    return float4(col, 1.0);
}
