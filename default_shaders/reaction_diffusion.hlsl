/*{
  "SHADER_TYPE": "generative",
  "INPUTS": [
    { "NAME": "FeedRate",    "TYPE": "float", "MIN": 0.01, "MAX": 0.1,  "DEFAULT": 0.055, "LABEL": "Feed Rate (F)" },
    { "NAME": "KillRate",    "TYPE": "float", "MIN": 0.04, "MAX": 0.07, "DEFAULT": 0.062, "LABEL": "Kill Rate (k)" },
    { "NAME": "AnimSpeed",   "TYPE": "float", "MIN": 0.0,  "MAX": 2.0,  "DEFAULT": 0.5,   "LABEL": "Anim Speed" },
    { "NAME": "ColourMap",   "TYPE": "long",  "VALUES": [0,1,2,3], "LABELS": ["Blue","Fire","Mint","Grey"], "DEFAULT": 0, "LABEL": "Colour Map" },
    { "NAME": "Detail",      "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.35,  "LABEL": "Detail" },
    { "NAME": "RidgeGlow",   "TYPE": "float", "MIN": 0.0,  "MAX": 2.0,  "DEFAULT": 0.5,   "LABEL": "Ridge Glow" },
    { "NAME": "Exposure",    "TYPE": "float", "MIN": 0.1,  "MAX": 4.0,  "DEFAULT": 1.0,   "LABEL": "Exposure" },
    { "NAME": "VignetteAmt", "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.25,  "LABEL": "Vignette" }
  ]
}*/

// Turing instability pattern approximation using multi-scale Difference-of-Gaussians
// applied to an animated Perlin noise field.
// FeedRate (F) and KillRate (k) select morphological regimes:
//   F≈0.02 k≈0.05 → maze/labyrinth     F≈0.04 k≈0.06 → stripes
//   F≈0.06 k≈0.062 → spots/coral       F≈0.08 k≈0.065 → scattered dots
// This is a stateless approximation — parameters map to pattern geometry
// rather than running a true Gray-Scott integration.
//
// The four colour maps are kept as named stop ramps rather than being replaced
// with a cosine palette: "Fire" and "Mint" are the interface here, and a cosine
// fit that reproduced them would only be a less controllable spelling of the
// same stops. What did change is that the stops are now linear-light values with
// smoothstep easing between them, so the ramps have no luminance dip at the
// joins and no visible kink where two segments meet.

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

// Sample Perlin channel of the noise texture with domain warp.
// SampleLevel(0): the fine passes run at 10x the coarse frequency and an
// implicit-derivative fetch would mip them away exactly where the detail lives.
float sampleNoise(float2 uv, float scl, float2 offset) {
    return noiseTexture.SampleLevel(noiseSampler, uv * scl + offset, 0).r;
}

// Five-stop ramp with C1 joins, in linear light.
float3 ramp5(float t, float3 k0, float3 k1, float3 k2, float3 k3, float3 k4) {
    float  s = saturate(t) * 4.0;
    int    i = min(int(s), 3);
    float  f = smoothstep(0.0, 1.0, saturate(s - float(i)));
    float3 a = (i == 0) ? k0 : (i == 1) ? k1 : (i == 2) ? k2 : k3;
    float3 b = (i == 0) ? k1 : (i == 1) ? k2 : (i == 2) ? k3 : k4;
    return lerp(a, b, f);
}

float3 colourMap(float u) {
    if (ColourMap == 1) {
        // Fire: black → dark red → orange → yellow → white
        return ramp5(u, float3(0.000, 0.000, 0.000),
                        float3(0.100, 0.000, 0.000),
                        float3(0.450, 0.010, 0.000),
                        float3(1.000, 0.210, 0.000),
                        float3(1.000, 1.000, 0.600));
    } else if (ColourMap == 2) {
        // Mint / teal: deep navy → cyan → pale mint
        return ramp5(u, float3(0.000, 0.004, 0.019),
                        float3(0.000, 0.080, 0.140),
                        float3(0.000, 0.376, 0.448),
                        float3(0.250, 0.700, 0.680),
                        float3(0.690, 1.000, 0.890));
    } else if (ColourMap == 3) {
        // Greyscale — a perceptual ramp, so it must be encoded, not linear.
        return spSrgbToLinear(saturate(u).xxx);
    }
    // Blue (default): midnight → electric blue → ice white
    return ramp5(u, float3(0.000, 0.000, 0.007),
                    float3(0.000, 0.010, 0.100),
                    float3(0.000, 0.033, 0.790),
                    float3(0.200, 0.450, 0.950),
                    float3(0.600, 0.890, 1.000));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;
    float  t  = time * AnimSpeed;

    // Domain warp: use a coarse noise pass to distort UV for the fine pass.
    // This breaks lattice regularity and produces realistic-looking RD structures.
    float2 warpVec;
    warpVec.x = sampleNoise(uv, 1.8, float2(t * 0.05, 0.0)) * 2.0 - 1.0;
    warpVec.y = sampleNoise(uv, 1.8, float2(0.0, t * 0.05 + 0.3)) * 2.0 - 1.0;
    float2 warpedUV = uv + warpVec * 0.08;

    // Multi-scale difference of Gaussians: approximates the activator–inhibitor
    // diffusion contrast at the characteristic Turing wavelength.
    // Activator diffuses slowly (small scale), inhibitor quickly (large scale).
    float scaleA = 4.0 + FeedRate * 80.0;      // activator: fine detail
    float scaleI = scaleA * (0.3 + KillRate * 5.0); // inhibitor: coarse envelope
    float actv = sampleNoise(warpedUV, scaleA, float2(t * 0.03, t * 0.02));
    float inhb = sampleNoise(warpedUV, scaleI, float2(-t * 0.02, t * 0.025 + 1.0));

    // DoG contrast: positive values = activator-dominated (pattern peaks)
    float dog  = actv - inhb * 0.85;

    // A second layer at a different orientation breaks rectangular symmetry.
    float2 rotUV  = float2(warpedUV.x * 0.707 - warpedUV.y * 0.707,
                            warpedUV.x * 0.707 + warpedUV.y * 0.707);
    float act2   = sampleNoise(rotUV, scaleA * 1.1, float2(t * 0.025 + 2.0, -t * 0.03));
    float inh2   = sampleNoise(rotUV, scaleI * 0.95, float2(-t * 0.015 + 3.0, t * 0.02));
    float dog2   = act2 - inh2 * 0.85;

    // Combine layers — the cross-orientation interference creates spots vs stripes.
    // KillRate shifts the balance between the two layers.
    float combined = dog * (1.0 - KillRate * 10.0) + dog2 * (KillRate * 10.0 - 0.3);

    // Third octave at ~2.5x. Two DoG layers give clean blobs with dead flat
    // interiors; the fine layer roughens the boundary and puts structure inside
    // the cells, which is what real coral and vein patterns have.
    if (Detail > 0.001) {
        float act3 = sampleNoise(warpedUV, scaleA * 2.6, float2(-t * 0.04, t * 0.05 + 4.0));
        float inh3 = sampleNoise(warpedUV, scaleI * 2.4, float2(t * 0.035 + 5.0, -t * 0.02));
        combined += (act3 - inh3 * 0.85) * Detail * 0.45;
    }

    // Threshold with soft edges — maps to the [0,1] activator concentration "u".
    // The fixed 0.15 half-width is the aesthetic softness; fwidth adds whatever
    // extra the pixel footprint needs so the boundary never turns into stairs at
    // high FeedRate, where scaleA reaches 12 cycles of noise per frame width.
    float threshold = (FeedRate - 0.01) * 8.0 - 0.5;
    float soft = 0.15 + fwidth(combined) * 0.5;
    float u = smoothstep(threshold - soft, threshold + soft, combined);

    float3 col = colourMap(u);

    // Inverse-distance glow on the ridge line itself (the level set where the
    // activator balances the inhibitor). It is the one feature in the image with
    // any structure to it, and lighting it is what separates the pattern from a
    // flat two-tone fill.
    if (RidgeGlow > 0.001) {
        float ridge = abs(combined - threshold);
        float rw    = max(soft * 0.5, 1e-4);
        col += colourMap(0.9) * RidgeGlow * 0.6 * rw / (ridge + rw);
    }

    col *= Exposure;
    col *= spVignette(uv, VignetteAmt, 0.85);

    col = spLinearToSrgb(spTonemapACES(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
