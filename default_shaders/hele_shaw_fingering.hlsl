/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        {"NAME": "viscosityRatio",  "LABEL": "Viscosity Ratio",  "TYPE": "float", "MIN": 0.01, "MAX": 1.0,  "DEFAULT": 0.15},
        {"NAME": "surfaceTension",  "LABEL": "Surface Tension",  "TYPE": "float", "MIN": 0.01, "MAX": 1.0,  "DEFAULT": 0.25},
        {"NAME": "injectionRate",   "LABEL": "Injection Rate",   "TYPE": "float", "MIN": 0.05, "MAX": 2.0,  "DEFAULT": 0.4},
        {"NAME": "noiseAmplitude",  "LABEL": "Noise Amplitude",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.5},
        {"NAME": "colourByPressure","LABEL": "Colour by Pressure","TYPE": "bool",  "DEFAULT": true},
        {"NAME": "formulation",     "LABEL": "Geometry",         "TYPE": "long",
         "VALUES": [0,1,2], "LABELS": ["Radial","Linear","Branching"], "DEFAULT": 0},
        {"NAME": "animSpeed",       "LABEL": "Anim Speed",       "TYPE": "float", "MIN": 0.0, "MAX": 5.0, "DEFAULT": 1.0},
        {"NAME": "FluidColour",     "LABEL": "Fluid Colour",     "TYPE": "color", "DEFAULT": [0.3,0.7,1.0,1.0]},
        {"NAME": "audioAmount",     "LABEL": "Audio Amount",     "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.6},
        {"NAME": "GlowAmount",      "LABEL": "Interface Glow",   "TYPE": "float", "MIN": 0.0, "MAX": 3.0, "DEFAULT": 0.7},
        {"NAME": "Exposure",        "LABEL": "Exposure",         "TYPE": "float", "MIN": 0.1, "MAX": 4.0, "DEFAULT": 1.0},
        {"NAME": "VignetteAmt",     "LABEL": "Vignette",         "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.25},
        {"NAME": "bassIn",          "LABEL": "Bass",             "TYPE": "audio", "BAND": "bass"},
        {"NAME": "highIn",          "LABEL": "Treble",           "TYPE": "audio", "BAND": "high"},
        {"NAME": "beatIn",          "LABEL": "Beat",             "TYPE": "audio", "BAND": "beat"}
    ]
}*/

// Hele-Shaw Saffman-Taylor viscous fingering (Laplacian growth model).
// A less-viscous fluid displaces a more-viscous one in a thin cell; the
// interface is unstable at wavelengths below the capillary length set by
// surfaceTension.  This shader reproduces the fractal finger morphology by
// constructing a radial FBM noise field whose angular modulation frequency
// equals 1/surfaceTension (the dominant unstable wavelength) and iterates
// sub-octaves for the fractal branching tip-splitting.  The inviscid region
// (less-viscous fluid) is determined by a threshold on this field relative
// to an injection-rate-driven growth radius.
//
// The angular perturbation is sampled on the unit direction vector rather than on
// atan2's output. Feeding the angle into a texture coordinate put a discontinuity
// along the -x axis: the fingers were cut by a straight radial seam, and any
// derivative taken across it was meaningless. A direction vector is periodic by
// construction, so the field closes on itself.
//
// All three geometries produce the same three quantities (a signed distance to the
// interface, a pressure, and the interface glow) and share one shading tail. The
// linear mode used to return early from the middle of the function with its own
// ad-hoc colouring, so it picked up none of the tonemap, dither or vignette.

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

// Pressure ramp: dark blue through red to a hot white. A cosine palette rather than
// three hand-fitted smoothsteps, which crossed zero at different points per channel
// and put a green cast through the middle of the range.
float3 pressureRamp(float t) {
    return max(spPalette(0.5 - saturate(t) * 0.5,
                         float3(0.50, 0.44, 0.42),
                         float3(0.50, 0.44, 0.42),
                         float3(1.0,  1.0,  1.0),
                         float3(0.0,  0.10, 0.22)), 0.0);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float  ar  = resolution.x / resolution.y;
    float2 p   = (uv - 0.5) * float2(ar, 1.0);

    // One output pixel measured in p units. p.y spans one unit over resolution.y
    // pixels and p.x is scaled by the same factor, so the footprint is isotropic.
    float px = 1.0 / max(resolution.y, 1.0);

    // --- Audio modulation ---
    // Bass and beats drive injection (the finger front advances), treble raises the
    // dominant unstable mode so the fingers split more finely. 0 = unmodulated growth.
    float aBass = bassIn * audioAmount;
    float aHigh = highIn * audioAmount;
    float aBeat = beatIn * audioAmount;

    // Growth radius: the front advances once and settles at its full extent.
    float growthT = saturate(time * 0.08 * animSpeed);
    float growthR = injectionRate * (0.3 + growthT * 0.5) * (1.0 + aBass * 0.6 + aBeat * 0.35);

    // Slow, non-repeating drift of the perturbation field keeps the fingers alive
    // once the front has settled, without the interface ever resetting.
    float drift = time * animSpeed * 0.015;

    // Angular frequency for dominant unstable mode
    // surfaceTension stabilises short wavelengths: low tension → more/finer fingers
    float fingerFreq = 2.0 + (1.0 - surfaceTension) * 18.0 + aHigh * 14.0;

    float rad = length(p);
    float2 dir = p / max(rad, 1e-5);   // periodic replacement for atan2

    float sd;         // signed distance to the interface, positive inside the finger
    float pressure;   // 1 at the injection point, 0 at the front

    if (formulation == 1) {
        // Linear: horizontal displacement front advancing in +x.
        float basePerturb = noiseTexture.SampleLevel(noiseSampler, float2(uv.y * fingerFreq * 0.1, 0.3 + drift), 0).r;
        float subPerturb  = noiseTexture.SampleLevel(noiseSampler, float2(uv.y * fingerFreq * 0.2, 0.7 + drift * 2.0), 0).r;
        float frontX = (basePerturb * 0.3 + subPerturb * 0.15) * noiseAmplitude + growthR;

        // Front sits at uv.x == frontX. The old form placed it at uv.x == 0.5 + frontX,
        // which put it off the right edge of the frame for most of the parameter range.
        sd       = (frontX - 0.5) * ar - p.x;
        pressure = 1.0 - uv.x / max(frontX, 0.01);

    } else if (formulation == 2) {
        // Branching: angular modulation crossed with a radial one, which is what
        // splits a finger into two as the front advances.
        float2 bUV = dir * fingerFreq * 0.045;
        float branchVal = noiseTexture.SampleLevel(noiseSampler, bUV + float2(0.0, rad * 4.0 + drift), 0).r;
        float subVal    = noiseTexture.SampleLevel(noiseSampler, bUV * 2.0 + float2(0.5, rad * 8.0 + drift * 1.7), 0).r;
        float fingerR   = growthR * (1.5 + (branchVal * 0.6 + subVal * 0.3) * noiseAmplitude * 2.0);

        sd       = fingerR - rad;
        pressure = 1.0 - rad / max(fingerR, 0.001);

    } else {
        // Radial: fingers grow outward from an injection point at the centre.
        float2 aUV = dir * fingerFreq * 0.045;
        float basePerturb = noiseTexture.SampleLevel(noiseSampler, aUV + float2(0.0, drift), 0).r;

        // Sub-octave branching (tip-splitting)
        float subPerturb = 0.0;
        float subScale = 2.0;
        float subAmp   = 0.5;
        [loop] for (int i = 0; i < 5; i++) {
            float2 sUV = aUV * subScale + float2(float(i) * 1.37, drift * (1.0 + float(i)));
            subPerturb += noiseTexture.SampleLevel(noiseSampler, sUV, 0).r * subAmp;
            subScale   *= 1.9;
            subAmp     *= 0.5;
        }

        float fingerR = growthR + (basePerturb * 0.4 + subPerturb * 0.2) * noiseAmplitude * growthR;

        sd       = fingerR - rad;
        pressure = 1.0 - rad / max(fingerR, 0.001);
    }

    pressure = saturate(pressure);

    // The interface is band-limited over its own screen footprint, floored at one
    // pixel. Previously this was a bool compare, so the finger edge (the single
    // most looked-at feature in the image) was a hard jagged staircase.
    float w      = max(max(fwidth(sd), px), 1e-6);
    float inside = smoothstep(-w, w, sd);

    // Fine mottling of the invaded fluid. A Hele-Shaw cell is never a flat plate of
    // colour, and a large uniform fill is where the image reads as synthetic.
    float mottle = noiseTexture.SampleLevel(noiseSampler, p * 9.0 + float2(drift * 3.0, 0.0), 0).r;
    float shade  = lerp(0.86, 1.14, mottle);

    float3 fluidLin = spSrgbToLinear(FluidColour.rgb);
    float3 fluidCol = colourByPressure ? pressureRamp(pressure * viscosityRatio * 4.0)
                                       : fluidLin * pressure * 2.0;

    // Ambient viscous medium outside the front.
    float3 medium = colourByPressure ? float3(0.004, 0.004, 0.012)
                                     : float3(0.008, 0.008, 0.024);

    float3 col = lerp(medium, fluidCol * shade, inside);

    // Interface glow as an inverse-distance line source rather than an exp() falloff:
    // the front is where the pressure gradient is, and it should read as the brightest
    // thing in frame without the fill having to be pushed up to meet it.
    float gw = 0.02 + aBeat * 0.01;
    col += lerp(float3(0.8, 0.95, 1.0), fluidLin, 0.35)
         * GlowAmount * 0.45 * gw / max(abs(sd), gw * 0.25);

    col *= Exposure;
    col *= spVignette(uv, VignetteAmt, 0.85);

    // tanh: the interface term is unbounded, and it holds the ramp's colour through
    // the hot front where ACES would desaturate it.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
