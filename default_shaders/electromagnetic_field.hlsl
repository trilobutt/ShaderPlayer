/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        {"NAME": "chargeCount",   "LABEL": "Charges",       "TYPE": "long",
         "VALUES": [2,3,4,5,6,8], "LABELS": ["2","3","4","5","6","8"], "DEFAULT": 4},
        {"NAME": "chargeSignMode","LABEL": "Signs",         "TYPE": "long",
         "VALUES": [0,1,2,3], "LABELS": ["Alternating","+All","-All","Random"], "DEFAULT": 0},
        {"NAME": "lineCount",     "LABEL": "Field Density", "TYPE": "long",
         "VALUES": [4,8,16,32], "LABELS": ["Low","Medium","High","Very High"], "DEFAULT": 8},
        {"NAME": "integrationStep","LABEL": "Step Size",    "TYPE": "float", "MIN": 0.001,"MAX": 0.02, "DEFAULT": 0.006},
        {"NAME": "displayMode",   "LABEL": "Display",       "TYPE": "long",
         "VALUES": [0,1,2,3], "LABELS": ["Field Lines","Force Vectors","E Heatmap","Equipotential"], "DEFAULT": 0},
        {"NAME": "colourByMag",   "LABEL": "Colour by |E|", "TYPE": "bool",  "DEFAULT": true},
        {"NAME": "fieldFalloff",  "LABEL": "Falloff Exp",   "TYPE": "float", "MIN": 0.5,  "MAX": 3.0,  "DEFAULT": 1.0},
        {"NAME": "FieldColour",   "LABEL": "Field Colour",  "TYPE": "color", "DEFAULT": [0.65,0.85,1.0,1.0]}
    ]
}*/

// N point charges placed in a slowly rotating ring.  E-field and electric potential V
// are computed analytically per-pixel; the display mode selects the visualisation:
// LIC streamlines (field lines), force-vector arrow glyphs, |E| heat map, or
// equipotential contour lines.

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

float3 heatmap(float t) {
    t = saturate(t);
    return float3(
        smoothstep(0.0, 0.6, t),
        smoothstep(0.2, 0.8, t) * (1.0 - smoothstep(0.8, 1.0, t)),
        1.0 - smoothstep(0.4, 1.0, t)
    );
}
float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Compute E and V from up to 8 charges.
// Charge positions cPos[i] are in UV [0,1]^2 space; pos is aspect-corrected.
void computeField(float2 pos, float ar, float2 cPos[8], float cSign[8],
                  int nCharges, float falloff, out float2 outE, out float outV) {
    float2 E = float2(0, 0);
    float  V = 0.0;
    [loop] for (int i = 0; i < 8; i++) {
        if (i >= nCharges) break;
        // Convert charge UV → aspect space to match pos
        float2 r = pos - cPos[i] * float2(ar, 1.0);
        float  d = max(length(r), 0.005);
        E += cSign[i] * r / pow(d, falloff + 2.0);
        V += cSign[i] / pow(d, falloff);
    }
    outE = E;
    outV = V;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float  ar  = resolution.x / resolution.y;
    // Work in aspect-corrected space to preserve Euclidean field geometry
    float2 pos = uv * float2(ar, 1.0);

    // --- Generate charge positions (UV space, slowly rotating ring) ---
    float  rotate = time * 0.12;
    float2 cPos [8];
    float  cSign[8];
    [unroll] for (int i = 0; i < 8; i++) {
        float theta  = float(i) / 8.0 * 6.28318 + rotate;
        float jitter = frac(sin(float(i) * 127.1) * 43758.5453) * 0.1;
        cPos[i]  = float2(0.5, 0.5) + float2(cos(theta), sin(theta)) * (0.28 + jitter);
        float hr = frac(sin(float(i) * 311.7) * 43758.5453);
        if      (chargeSignMode == 0) cSign[i] = (i % 2 == 0) ? 1.0 : -1.0;
        else if (chargeSignMode == 1) cSign[i] = 1.0;
        else if (chargeSignMode == 2) cSign[i] = -1.0;
        else                          cSign[i] = hr > 0.5 ? 1.0 : -1.0;
    }

    float2 E; float V;
    computeField(pos, ar, cPos, cSign, chargeCount, fieldFalloff, E, V);

    float  Emag = length(E);
    float  Eang = atan2(E.y, E.x);

    float3 col = float3(0.0, 0.0, 0.02);

    if (displayMode == 0) {
        // Field lines via LIC along the E direction
        float  licVal  = 0.0;
        float2 p2      = pos;
        int    licSteps = 4 + lineCount;
        [loop] for (int s = 0; s < 36; s++) {
            if (s >= licSteps) break;
            float2 En; float Vn;
            computeField(p2, ar, cPos, cSign, chargeCount, fieldFalloff, En, Vn);
            float enL  = max(length(En), 0.0001);
            float2 dir = En / enL;
            p2        += dir * integrationStep;
            float2 hiUV = p2 * 28.0;
            licVal += (frac(hiUV.x + hiUV.y) > 0.5) ? 1.0 : 0.0;
        }
        licVal /= float(max(licSteps, 1));
        float brightness = pow(saturate(licVal), 0.8);
        float hue = colourByMag ? saturate(0.65 - saturate(log(Emag + 1.0) * 0.25) * 0.5)
                                : frac(Eang / 6.28318 + 0.5);
        col = hsv2rgb(float3(hue, 0.85, brightness)) * FieldColour.rgb;

    } else if (displayMode == 1) {
        // Force vectors: arrow glyphs at a grid
        float  gridFreq = 8.0;
        float2 gridCell = floor(uv * gridFreq);
        float2 cellCtr  = (gridCell + 0.5) / gridFreq;
        float2 cellPos  = cellCtr * float2(ar, 1.0);
        float2 Eg; float Vg;
        computeField(cellPos, ar, cPos, cSign, chargeCount, fieldFalloff, Eg, Vg);
        float EgL    = max(length(Eg), 0.0001);
        float2 Enorm = Eg / EgL;
        float2 rel   = (uv - cellCtr) * gridFreq * 2.0;
        float  arrowLen = saturate(EgL * 0.3);
        float  along    = dot(rel, Enorm);
        float  perp     = abs(dot(rel, float2(-Enorm.y, Enorm.x)));
        float  onShaft  = step(perp, 0.1) * step(-arrowLen, along) * step(along, arrowLen);
        float  headDist = length(rel - Enorm * arrowLen);
        float  onHead   = step(headDist, 0.18);
        float  arrow    = max(onShaft, onHead);
        float3 arrowCol = colourByMag ? heatmap(saturate(EgL * 0.4)) : float3(0.7, 0.9, 1.0);
        col = arrowCol * arrow;

    } else if (displayMode == 2) {
        // |E| heat map
        float logE = log(Emag + 1.0) * 0.4;
        col = colourByMag ? heatmap(saturate(logE))
                          : hsv2rgb(float3(frac(Eang / 6.28318), 0.8, saturate(logE)));

    } else {
        // Equipotential contours
        float contour = 1.0 - abs(frac(V * 0.25) - 0.5) * 2.0;
        contour = smoothstep(0.85, 1.0, contour);
        float3 contourCol = (V > 0.0) ? float3(1.0, 0.4, 0.2) : float3(0.3, 0.6, 1.0);
        col = contourCol * contour + float3(0.01, 0.01, 0.04);
    }

    // Charge glyph overlay (glow discs)
    [loop] for (int ci = 0; ci < 8; ci++) {
        if (ci >= chargeCount) break;
        float  cdist   = length(uv - cPos[ci]) * resolution.y;
        float  glow    = exp(-cdist * 0.12) * 0.4;
        float3 chgCol  = cSign[ci] > 0.0 ? float3(1.0, 0.3, 0.2) : float3(0.3, 0.5, 1.0);
        col += chgCol * glow;
    }

    return float4(saturate(col), 1.0);
}
