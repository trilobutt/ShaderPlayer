/*{
  "SHADER_TYPE": "generative",
  "INPUTS": [
    { "NAME": "FeedRate",  "TYPE": "float", "MIN": 0.01, "MAX": 0.1,  "DEFAULT": 0.055, "LABEL": "Feed Rate (F)" },
    { "NAME": "KillRate",  "TYPE": "float", "MIN": 0.04, "MAX": 0.07, "DEFAULT": 0.062, "LABEL": "Kill Rate (k)" },
    { "NAME": "AnimSpeed", "TYPE": "float", "MIN": 0.0,  "MAX": 2.0,  "DEFAULT": 0.5,   "LABEL": "Anim Speed" },
    { "NAME": "ColourMap", "TYPE": "long",  "VALUES": [0,1,2,3], "LABELS": ["Blue","Fire","Mint","Grey"], "DEFAULT": 0, "LABEL": "Colour Map" }
  ]
}*/

// Turing instability pattern approximation using multi-scale Difference-of-Gaussians
// applied to an animated Perlin noise field.
// FeedRate (F) and KillRate (k) select morphological regimes:
//   F≈0.02 k≈0.05 → maze/labyrinth     F≈0.04 k≈0.06 → stripes
//   F≈0.06 k≈0.062 → spots/coral       F≈0.08 k≈0.065 → scattered dots
// This is a stateless approximation — parameters map to pattern geometry
// rather than running a true Gray-Scott integration.

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

// Sample Perlin channel of the noise texture with domain warp.
float sampleNoise(float2 uv, float scl, float2 offset) {
    return noiseTexture.Sample(noiseSampler, uv * scl + offset).r;
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

    // Threshold with soft edges — maps to the [0,1] activator concentration "u".
    float threshold = (FeedRate - 0.01) * 8.0 - 0.5;
    float u = smoothstep(threshold - 0.15, threshold + 0.15, combined);

    // Colour maps — (v = 1 - u as the inhibitor complement)
    float v = 1.0 - u;
    float3 col;
    if (ColourMap == 1) {
        // Fire: black → dark red → orange → yellow → white
        col = lerp(float3(0.0, 0.0, 0.0),
                   lerp(float3(0.7, 0.0, 0.0),
                        lerp(float3(1.0, 0.5, 0.0),
                             float3(1.0, 1.0, 0.8), saturate(u * 3.0 - 2.0)),
                        saturate(u * 3.0 - 1.0)),
                   saturate(u * 3.0));
    } else if (ColourMap == 2) {
        // Mint / teal: deep navy → cyan → pale mint
        col = lerp(float3(0.0, 0.05, 0.15),
                   lerp(float3(0.0, 0.65, 0.7), float3(0.85, 1.0, 0.95), u),
                   u);
    } else if (ColourMap == 3) {
        // Greyscale
        col = float3(u, u, u);
    } else {
        // Blue (default): midnight → electric blue → ice white
        col = lerp(float3(0.0, 0.0, 0.08),
                   lerp(float3(0.0, 0.2, 0.9), float3(0.8, 0.95, 1.0), u),
                   u);
    }

    return float4(col, 1.0);
}
