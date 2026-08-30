/*{
    "SHADER_TYPE": "generative",
    "DESCRIPTION": "N point charges on a slowly rotating ring, with the E-field and the potential solved analytically at every pixel. Display selects the visualisation: LIC streamline field lines, force-vector arrow glyphs, an |E| heat map, or equipotential contours. Charges accumulate as inverse distance rather than as discs, so each core stays hot and its halo falls off across the whole frame.",
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
        {"NAME": "glowAmount",    "LABEL": "Glow",          "TYPE": "float", "MIN": 0.0,  "MAX": 3.0,  "DEFAULT": 1.0},
        {"NAME": "FieldColour",   "LABEL": "Field Colour",  "TYPE": "color", "DEFAULT": [0.65,0.85,1.0,1.0]},
        {"NAME": "paletteShift",  "LABEL": "Palette Shift", "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.0},
        {"NAME": "exposure",      "LABEL": "Exposure",      "TYPE": "float", "MIN": 0.1,  "MAX": 4.0,  "DEFAULT": 1.0},
        {"NAME": "vignetteAmt",   "LABEL": "Vignette",      "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.3},
        {"NAME": "bassIn",        "LABEL": "Bass",          "TYPE": "audio", "BAND": "bass"},
        {"NAME": "midIn",         "LABEL": "Mid",           "TYPE": "audio", "BAND": "mid"},
        {"NAME": "highIn",        "LABEL": "Treble",        "TYPE": "audio", "BAND": "high"},
        {"NAME": "beatIn",        "LABEL": "Beat",          "TYPE": "audio", "BAND": "beat"},
        {"NAME": "audioAmount",   "LABEL": "Audio Amount",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.6}
    ]
}*/

// N point charges placed in a slowly rotating ring.  E-field and electric potential V
// are computed analytically per-pixel; the display mode selects the visualisation:
// LIC streamlines (field lines), force-vector arrow glyphs, |E| heat map, or
// equipotential contour lines.
//
// The charges are drawn as inverse-distance accumulations rather than exp() discs:
// 1/d has no characteristic width, so the core stays hot and the halo keeps falling
// off across the whole frame instead of vanishing at a fixed radius. Everything is
// accumulated in linear light and rolled off with tanh at the end.

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

// Approximate inferno: black -> deep purple -> magenta -> orange -> pale yellow.
// Linear-light stops, smoothstep-eased so the joins have no kink. Monotonic in
// luminance, which the old blue->cyan->yellow ramp was not: it read as three
// separate bands because the middle of the ramp was brighter than the top.
float3 heatmap(float t) {
    const float3 k0 = float3(0.002, 0.000, 0.008);
    const float3 k1 = float3(0.090, 0.006, 0.190);
    const float3 k2 = float3(0.450, 0.030, 0.170);
    const float3 k3 = float3(0.900, 0.170, 0.020);
    const float3 k4 = float3(1.000, 0.720, 0.200);
    float  s = saturate(t) * 4.0;
    int    i = min(int(s), 3);
    float  f = smoothstep(0.0, 1.0, saturate(s - float(i)));
    float3 a = (i == 0) ? k0 : (i == 1) ? k1 : (i == 2) ? k2 : k3;
    float3 b = (i == 0) ? k1 : (i == 1) ? k2 : (i == 2) ? k3 : k4;
    return lerp(a, b, f);
}

// Direction colouring. An IQ cosine palette rather than a raw hue sweep: hue is
// not perceptually uniform, so an HSV rainbow keyed to field angle puts a hard
// yellow-to-green transition in the middle of what should be a smooth rotation.
float3 anglePalette(float t) {
    return max(spPalette(t + paletteShift,
                         float3(0.5,  0.45, 0.45),
                         float3(0.5,  0.45, 0.5),
                         float3(1.0,  1.0,  1.0),
                         float3(0.0,  0.2,  0.45)), 0.0);
}

// step(), filtered over a footprint the caller supplies. Used where the argument
// is discontinuous at a grid boundary: fwidth() reads that discontinuity as an
// infinitely wide pixel and smears a grey seam along every cell edge.
float aaStepW(float threshold, float value, float w) {
    return smoothstep(threshold - w, threshold + w, value);
}

// Compute E and V from up to 8 charges.
// Charge positions cPos[i] are in UV [0,1]^2 space; pos is aspect-corrected.
void computeField(float2 pos, float ar, float2 cPos[8], float cSign[8],
                  int nCharges, float falloff, out float2 outE, out float outV) {
    float2 E = float2(0, 0);
    float  V = 0.0;
    [loop] for (int i = 0; i < 8; i++) {
        if (i >= nCharges) break;
        // Convert charge UV -> aspect space to match pos
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
    // One pixel in aspect-corrected units. Both axes step by 1/resolution.y.
    float  pxA = 1.0 / resolution.y;

    // --- Audio modulation ---
    // Mid drives the ring's rotation, bass the charge magnitude and ring radius,
    // treble the contour/heatmap detail, and beats flash the charge glyphs.
    // audioAmount at 0 restores the original static field.
    float aBass = bassIn * audioAmount;
    float aMid  = midIn  * audioAmount;
    float aHigh = highIn * audioAmount;
    float aBeat = beatIn * audioAmount;

    // --- Generate charge positions (UV space, slowly rotating ring) ---
    float  rotate  = time * (0.12 + aMid * 0.55);
    float  ringR   = 1.0 + aBass * 0.4;
    float  qScale  = 1.0 + aBass * 2.5;   // charge magnitude; sign is preserved
    float2 cPos [8];
    float  cSign[8];
    [unroll] for (int i = 0; i < 8; i++) {
        float theta  = float(i) / 8.0 * 6.28318 + rotate;
        float jitter = frac(sin(float(i) * 127.1) * 43758.5453) * 0.1;
        cPos[i]  = float2(0.5, 0.5) + float2(cos(theta), sin(theta)) * (0.28 + jitter) * ringR;
        float hr = frac(sin(float(i) * 311.7) * 43758.5453);
        if      (chargeSignMode == 0) cSign[i] = (i % 2 == 0) ? 1.0 : -1.0;
        else if (chargeSignMode == 1) cSign[i] = 1.0;
        else if (chargeSignMode == 2) cSign[i] = -1.0;
        else                          cSign[i] = hr > 0.5 ? 1.0 : -1.0;
        cSign[i] *= qScale;
    }

    float2 E; float V;
    computeField(pos, ar, cPos, cSign, chargeCount, fieldFalloff, E, V);

    float  Emag = length(E);
    float  Eang = atan2(E.y, E.x);

    float3 fieldTint = spSrgbToLinear(FieldColour.rgb);
    float3 col = float3(0.0, 0.0, 0.0015);

    if (displayMode == 0) {
        // Field lines via LIC along the E direction. The base signal is a
        // band-limited grating: the old frac()>0.5 checkerboard at 28 cycles
        // beat against the pixel grid and the moire crawled with the rotation.
        // Its footprint is derived analytically because inside the advection
        // loop fwidth() would measure the derivative of an already-integrated
        // coordinate, which is meaningless wherever streamlines converge.
        float  gFreq   = 28.0 + aHigh * 40.0;
        float  foot    = SP_TAU * gFreq * 2.0 * pxA;
        float  licVal  = 0.0;
        float  wSum    = 0.0;
        float2 p2      = pos;
        int    licSteps = 4 + lineCount;
        [loop] for (int s = 0; s < 36; s++) {
            if (s >= licSteps) break;
            float2 En; float Vn;
            computeField(p2, ar, cPos, cSign, chargeCount, fieldFalloff, En, Vn);
            float enL  = max(length(En), 0.0001);
            float2 dir = En / enL;
            p2        += dir * integrationStep;
            // Cosine taper: an unweighted sum gives the far end of the trace the
            // same authority as the pixel itself, which blurs the line origin.
            float wgt  = 0.5 + 0.5 * cos(SP_PI * float(s) / float(licSteps));
            float g    = spBandLimitedCos(SP_TAU * gFreq * (p2.x + p2.y), foot);
            licVal    += (0.5 + 0.5 * g) * wgt;
            wSum      += wgt;
        }
        licVal /= max(wSum, 1e-4);
        float brightness = pow(saturate(licVal), 0.8);

        float3 lineCol = colourByMag ? heatmap(saturate(log(Emag + 1.0) * 0.22))
                                     : anglePalette(frac(Eang / SP_TAU + 0.5));
        // Field strength drives the emission, not just the hue: near a charge the
        // streamlines should blaze, far away they should be a faint filigree.
        float emission = 1.0 + glowAmount * saturate(log(Emag + 1.0) * 0.35) * 3.0;
        col = lineCol * fieldTint * brightness * emission;

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
        // `rel` restarts at every cell boundary, so its footprint is the size of
        // one pixel in cell units, computed directly from the grid frequency.
        float  relW  = gridFreq * 2.0 * (1.0 / resolution.x + 1.0 / resolution.y);
        float  arrowLen = saturate(EgL * 0.3);
        float  along    = dot(rel, Enorm);
        float  perp     = abs(dot(rel, float2(-Enorm.y, Enorm.x)));
        float  onShaft  = (1.0 - aaStepW(0.1, perp, relW))
                        * aaStepW(-arrowLen, along, relW)
                        * (1.0 - aaStepW(arrowLen, along, relW));
        float  headDist = length(rel - Enorm * arrowLen);
        float  onHead   = 1.0 - aaStepW(0.18, headDist, relW);
        float  arrow    = max(onShaft, onHead);
        float3 arrowCol = colourByMag ? heatmap(saturate(EgL * 0.4)) : float3(0.45, 0.78, 1.0);
        col = arrowCol * arrow * (1.0 + glowAmount * 0.5);

    } else if (displayMode == 2) {
        // |E| heat map
        float logE = log(Emag + 1.0) * 0.4 * (1.0 + aBass * 0.8);
        col = colourByMag ? heatmap(saturate(logE))
                          : anglePalette(frac(Eang / SP_TAU)) * saturate(logE);

    } else {
        // Equipotential contours. The triangle wave is continuous across the
        // frac() wrap (both sides of the jump evaluate to 1), so fwidth() on it
        // is well behaved and spAALine can filter the line properly: where the
        // potential gradient is steep the contours fade instead of aliasing.
        float tri = abs(frac(V * 0.25 * (1.0 + aHigh * 1.8)) - 0.5) * 2.0;
        float contour = spAALine(tri, 0.12);
        float3 contourCol = (V > 0.0) ? float3(1.0, 0.13, 0.03) : float3(0.07, 0.30, 1.0);
        col = contourCol * contour * (1.0 + glowAmount) + float3(0.001, 0.001, 0.003);
    }

    // Charge glyph overlay: inverse-distance accumulation in linear light.
    [loop] for (int ci = 0; ci < 8; ci++) {
        if (ci >= chargeCount) break;
        float  cdist   = max(length(uv - cPos[ci]) * resolution.y, 1.5);   // pixels
        float  glow    = glowAmount * (5.0 + aBeat * 12.0) / cdist;
        float3 chgCol  = cSign[ci] > 0.0 ? float3(1.0, 0.07, 0.03)
                                         : float3(0.07, 0.22, 1.0);
        col += chgCol * glow;
    }

    col *= exposure;
    col *= spVignette(uv, vignetteAmt, 0.85);

    // tanh, not ACES: the 1/d terms are unbounded and ACES bleaches the charge
    // cores to white, which loses the sign colouring exactly where it reads best.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
