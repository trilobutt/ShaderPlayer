/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        {"NAME": "noiseFreq",      "LABEL": "Noise Scale",   "TYPE": "float", "MIN": 0.5,  "MAX": 12.0, "DEFAULT": 3.0},
        {"NAME": "octaveCount",    "LABEL": "Octaves",       "TYPE": "long",
         "VALUES": [1,2,3,4,5,6], "LABELS": ["1","2","3","4","5","6"], "DEFAULT": 4},
        {"NAME": "persistence",    "LABEL": "Persistence",   "TYPE": "float", "MIN": 0.1,  "MAX": 0.9,  "DEFAULT": 0.5},
        {"NAME": "lacunarity",     "LABEL": "Lacunarity",    "TYPE": "float", "MIN": 1.2,  "MAX": 4.0,  "DEFAULT": 2.0},
        {"NAME": "trailFade",      "LABEL": "Trail Length",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.6},
        {"NAME": "stepSz",         "LABEL": "Step Size",     "TYPE": "float", "MIN": 0.5,  "MAX": 8.0,  "DEFAULT": 2.0},
        {"NAME": "colourByAngle",  "LABEL": "Colour By Angle","TYPE": "bool", "DEFAULT": true},
        {"NAME": "fibreDetail",    "LABEL": "Fibre Detail",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.4},
        {"NAME": "FlowTint",       "LABEL": "Flow Tint",      "TYPE": "color","DEFAULT": [0.6,0.9,1.0,1.0]},
        {"NAME": "paletteShift",   "LABEL": "Palette Shift",  "TYPE": "float","MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.0},
        {"NAME": "glowAmount",     "LABEL": "Glow",           "TYPE": "float","MIN": 0.0,  "MAX": 4.0,  "DEFAULT": 1.0},
        {"NAME": "exposure",       "LABEL": "Exposure",       "TYPE": "float","MIN": 0.1,  "MAX": 4.0,  "DEFAULT": 1.0},
        {"NAME": "vignetteAmt",    "LABEL": "Vignette",       "TYPE": "float","MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.25},
        {"NAME": "audioAmount",    "LABEL": "Audio Amount",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.6},
        {"NAME": "bassIn",         "LABEL": "Bass",          "TYPE": "audio", "BAND": "bass"},
        {"NAME": "midIn",          "LABEL": "Mid",           "TYPE": "audio", "BAND": "mid"},
        {"NAME": "beatIn",         "LABEL": "Beat",          "TYPE": "audio", "BAND": "beat"}
    ]
}*/

// Perlin curl flow field visualisation (LIC approximation).
// At each pixel the velocity field angle is read from FBM-summed noise; the
// shader then advects the sample point backward for trailSteps iterations,
// accumulating a high-frequency base signal along the path.  The resulting
// integral produces the characteristic fibrous streamline texture of Line-
// Integral Convolution.  colourByAngle tints each streamline by local flow
// direction.
//
// The base signal is a band-limited pair of cosine gratings rather than a hard
// checkerboard threshold: at 80 cycles across the frame the threshold form beats
// against the pixel grid and produces moire that swims with the flow. The
// footprint is derived analytically (see stripeSignal) because fwidth() inside
// the advection loop measures the derivative of an already-advected coordinate,
// which is meaningless at a fold in the flow map.

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

// FBM velocity angle from Perlin noise texture (R channel)
float flowAngle(float2 p, float freq, int octs, float pers, float lac) {
    float angle  = 0.0;
    float amp    = 1.0;
    float totAmp = 0.0;
    float2 fp    = p * freq;
    // Slide the noise slowly over time so the field evolves
    float2 drift = float2(time * 0.04, time * 0.027);
    [loop] for (int i = 0; i < 6; i++) {
        if (i >= octs) break;
        // lac is clamped positive by the ISF MIN, but fxc cannot see that and
        // warns on pow() with a possibly-negative base.
        float n  = noiseTexture.SampleLevel(noiseSampler, fp + drift * pow(abs(lac), float(i)), 0).r * 2.0 - 1.0;
        angle   += n * amp;
        totAmp  += amp;
        fp      *= lac;
        amp     *= pers;
    }
    // Map to full circle; 3.0 multiplier gives tighter curl patterns
    return (angle / max(totAmp, 0.001)) * 3.14159265 * 3.0;
}

// Base signal for the LIC integral, in [0,1]. `foot` is the screen-space
// footprint of one unit of the grating argument, so the gratings fade to flat
// grey where they would otherwise alias, which is the correct filtered answer.
float stripeSignal(float2 q, float foot, float detail) {
    float a1 = SP_TAU * (q.x + q.y);
    float s  = spBandLimitedCos(a1, foot * SP_TAU);
    // Second grating at 2.7x, crossed the other way: on its own the single
    // grating gives evenly spaced ribbons and nothing between them.
    float a2 = SP_TAU * 2.7 * (q.x - q.y * 0.6);
    s += spBandLimitedCos(a2, foot * SP_TAU * 2.7) * detail;
    return saturate(0.5 + 0.5 * s / (1.0 + detail));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float2 px  = 1.0 / resolution;
    float  ar  = resolution.x / resolution.y;

    // --- Audio modulation ---
    // Bass lengthens the streamline trails, mid stretches the step (the flow speeds up),
    // beats flare the brightness. audioAmount 0 = the unmodulated field.
    float aBass = bassIn * audioAmount;
    float aMid  = midIn  * audioAmount;
    float aBeat = beatIn * audioAmount;

    int   trailSteps = clamp(int(trailFade * 18.0 + 4.0 + aBass * 10.0), 4, 24);
    float pxStep     = stepSz * (1.0 + aMid * 1.6);  // step length in pixels

    // Grating frequency in cycles across the short axis, and the screen-space
    // footprint of one cycle. Both axes of the aspect-corrected coordinate step
    // by 1/resolution.y per pixel, so the footprint is a single scalar.
    const float stripeFreq = 80.0;
    float foot = 2.0 * stripeFreq / resolution.y;

    // --- LIC integration (backward trace) ---
    float licVal = 0.0;
    float wSum   = 0.0;
    float2 p     = uv;
    float2 pAR   = float2(ar, 1.0);   // aspect-ratio corrected coords

    [loop] for (int s = 0; s < 24; s++) {
        if (s >= trailSteps) break;
        float ang = flowAngle(p * pAR, noiseFreq, octaveCount, persistence, lacunarity);
        float2 vel = float2(cos(ang), -sin(ang));                // screen-space direction
        p         -= vel * pxStep * px;                          // step backward
        p          = frac(p);                                    // wrap

        // Cosine taper along the path. An unweighted box filter gives every
        // sample the same authority, so a streamline stops dead at the end of
        // the trail and the fibres read as fixed-length dashes; tapering makes
        // the tail fade and the head stay sharp.
        float wgt = 0.5 + 0.5 * cos(SP_PI * float(s) / float(trailSteps));
        licVal += stripeSignal(p * pAR * stripeFreq, foot, fibreDetail) * wgt;
        wSum   += wgt;
    }
    licVal /= max(wSum, 1e-4);

    // Brightness: squash into a nice range, apply slight gamma lift
    float brightness = pow(saturate(licVal * 1.4 - 0.1), 0.7) * (1.0 + aBeat * 1.1);

    // Colour by local flow angle
    float ang0 = flowAngle(uv * float2(ar, 1.0), noiseFreq, octaveCount, persistence, lacunarity);
    float3 tint = spSrgbToLinear(FlowTint.rgb);
    float3 col;
    if (colourByAngle) {
        // IQ cosine palette rather than a raw HSV sweep: the hue circle spends
        // most of its length in yellows and cyans that clash at equal value.
        float t = frac(ang0 / SP_TAU + 0.5) + paletteShift;
        col = spPalette(t, float3(0.5, 0.45, 0.45),
                           float3(0.5, 0.45, 0.5),
                           float3(1.0, 1.0, 1.0),
                           float3(0.0, 0.18, 0.42));
        col = max(col, 0.0) * tint * brightness;
    } else {
        col = tint * brightness;
    }

    // Superlinear glow on the brightest fibres. The LIC integral is a coverage
    // in [0,1] and cannot exceed it, so the only way to get a filament to bloom
    // is to push it well past 1 in linear light and let the tonemap roll it back.
    col *= 1.0 + glowAmount * brightness * brightness * 2.5;

    col *= exposure;
    col *= spVignette(uv, vignetteAmt, 0.8);

    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
