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
        {"NAME": "FluidColour",     "LABEL": "Fluid Colour",     "TYPE": "color", "DEFAULT": [0.3,0.7,1.0,1.0]}
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

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float  ar  = resolution.x / resolution.y;
    float2 p   = (uv - 0.5) * float2(ar, 1.0);

    // Growth radius driven by injection rate + time
    float growthR = injectionRate * (0.3 + frac(time * 0.08 * animSpeed) * 0.5);

    // Angular frequency for dominant unstable mode
    // surfaceTension stabilises short wavelengths: low tension → more/finer fingers
    float fingerFreq = 2.0 + (1.0 - surfaceTension) * 18.0;

    float fingerRadius = 0.0;

    if (formulation == 0) {
        // Radial: fingers grow outward from injection point at centre
        float ang = atan2(p.y, p.x);
        float rad = length(p);

        // Base angular perturbation at dominant wavelength
        float2 angUV = float2(ang / 6.28318 + 0.5, 0.5);
        float basePerturb = noiseTexture.SampleLevel(noiseSampler, angUV * float2(fingerFreq * 0.15, 1.0), 0).r;

        // Sub-octave branching (tip-splitting)
        float subPerturb = 0.0;
        float subFreq = fingerFreq * 2.0;
        float subAmp  = 0.5;
        [loop] for (int i = 0; i < 5; i++) {
            float2 sUV = float2(ang / 6.28318 + float(i) * 0.13 + 0.5, float(i) * 0.3);
            subPerturb += noiseTexture.SampleLevel(noiseSampler, sUV * float2(subFreq * 0.12, 1.0), 0).r * subAmp;
            subFreq   *= 1.9;
            subAmp    *= 0.5;
        }

        fingerRadius = growthR + (basePerturb * 0.4 + subPerturb * 0.2) * noiseAmplitude * growthR;

    } else if (formulation == 1) {
        // Linear: horizontal displacement front
        float basePerturb = noiseTexture.SampleLevel(noiseSampler, float2(uv.y * fingerFreq * 0.1, 0.3), 0).r;
        float subPerturb  = noiseTexture.SampleLevel(noiseSampler, float2(uv.y * fingerFreq * 0.2, 0.7), 0).r;
        float frontX = 0.0 + (basePerturb * 0.3 + subPerturb * 0.15) * noiseAmplitude + growthR;
        fingerRadius = (p.x + 0.01 < frontX * ar) ? 1.0 : 0.0;
        // Use fingerRadius as a binary mask for linear mode
        float distToFront = abs(p.x / ar - frontX);
        float3 col3 = float3(0,0,0);
        if (p.x / ar < frontX) {
            float pressure = 1.0 - (p.x / ar + 0.5) / max(frontX + 0.5, 0.01);
            col3 = colourByPressure ? heatmap(pressure) : FluidColour.rgb;
        }
        col3 += exp(-distToFront * 30.0) * float3(1.0, 1.0, 0.8) * 0.5; // interface glow
        return float4(saturate(col3), 1.0);

    } else {
        // Branching: combine radial and perpendicular modulations
        float ang = atan2(p.y, p.x);
        float rad = length(p);
        float2 branchUV = float2(frac(ang / 6.28318 * fingerFreq * 0.5), rad * 4.0);
        float branchVal = noiseTexture.SampleLevel(noiseSampler, branchUV * 0.3, 0).r;
        float subVal    = noiseTexture.SampleLevel(noiseSampler, branchUV * 0.6 + 0.5, 0).r;
        fingerRadius = growthR * (1.5 + (branchVal * 0.6 + subVal * 0.3) * noiseAmplitude * 2.0);
    }

    float rad = length(p);
    bool inside = (rad < fingerRadius);

    // Pressure field: falls off as 1/r in the viscous region (Darcy flow)
    float pressure = inside ? (1.0 - rad / max(fingerRadius, 0.001)) : 0.0;

    // Interface glow
    float interfaceDist = abs(rad - fingerRadius);
    float interfaceGlow = exp(-interfaceDist * 25.0);

    float3 col;
    if (colourByPressure) {
        col = inside ? heatmap(pressure * viscosityRatio * 4.0) : float3(0.02, 0.02, 0.05);
    } else {
        col = inside ? FluidColour.rgb * pressure * 2.0 : float3(0.05, 0.05, 0.12);
    }
    col += interfaceGlow * float3(0.8, 0.95, 1.0) * 0.4;

    return float4(saturate(col), 1.0);
}
