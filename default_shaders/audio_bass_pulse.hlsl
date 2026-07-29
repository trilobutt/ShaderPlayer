/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "PulseBass",     "TYPE": "audio", "BAND": "bass",  "LABEL": "Bass" },
    { "NAME": "PulseBeat",     "TYPE": "audio", "BAND": "beat",  "LABEL": "Beat" },
    { "NAME": "PulseStrength", "TYPE": "float", "DEFAULT": 0.06, "MIN": 0.0, "MAX": 0.3,  "LABEL": "Pulse Strength" },
    { "NAME": "ChromaAmt",     "TYPE": "float", "DEFAULT": 0.018,"MIN": 0.0, "MAX": 0.12, "LABEL": "Chroma Shift" },
    { "NAME": "BeatFlash",     "TYPE": "float", "DEFAULT": 0.5,  "MIN": 0.0, "MAX": 1.0,  "LABEL": "Beat Flash" },
    { "NAME": "Exposure",      "TYPE": "float", "DEFAULT": 1.0,  "MIN": 0.1, "MAX": 4.0,  "STEP": 0.05, "LABEL": "Exposure" },
    { "NAME": "FlashColor",    "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0],          "LABEL": "Flash Colour" },
    { "NAME": "BloomAmt",      "TYPE": "float", "DEFAULT": 0.5,  "MIN": 0.0, "MAX": 3.0,  "STEP": 0.01, "LABEL": "Bass Bloom" },
    { "NAME": "VignetteAmt",   "TYPE": "float", "DEFAULT": 0.2,  "MIN": 0.0, "MAX": 1.0,  "STEP": 0.01, "LABEL": "Vignette" }
  ]
}*/

// ISF packing:
// PulseStrength offset 0
// ChromaAmt     offset 1
// BeatFlash     offset 2
// Exposure      offset 3
// FlashColor    offset 4 (color, 4-aligned)
// BloomAmt      offset 8
// VignetteAmt   offset 9
// 10/32 floats used.

Texture2D videoTexture  : register(t0);
SamplerState videoSampler : register(s0);
Texture2D noiseTexture  : register(t1);
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

// Highlight knee for the bloom. Only energy above this contributes, so the streaks
// come off specular hits and light sources rather than smearing the whole frame.
static const float kBloomKnee = 0.45;

float3 sampleLinear(float2 uv) {
    return spSrgbToLinear(videoTexture.Sample(videoSampler, saturate(uv)).rgb);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv     = input.uv;
    float2 centre = float2(0.5, 0.5);
    float2 dir    = uv - centre;

    // Radial expansion on bass hits.
    float expand = PulseBass * PulseStrength;
    float2 uvWarp = uv + dir * expand;

    // Chromatic aberration scaled by bass. Sampled and recombined in linear light: a
    // per-channel offset applied to sRGB values shifts the luminance of the fringe as
    // well as its hue, which is why sRGB-space splits look dirty rather than optical.
    float shift = ChromaAmt * (1.0 + PulseBass * 8.0);
    float3 cR = sampleLinear(uvWarp + dir * shift);
    float3 cG = sampleLinear(uvWarp);
    float3 cB = sampleLinear(uvWarp - dir * shift);

    float3 col = float3(cR.r, cG.g, cB.b);

    // Radial highlight bloom. There is no feedback buffer, so the streak is built by
    // marching outward along the pixel's own radial line and accumulating whatever
    // highlight energy sits further out — the analytic equivalent of a zoom blur on the
    // bright pass. Accumulated in HDR and left to the tonemap, so a hard bass hit reads
    // as light spilling rather than as the frame going flat white.
    float3 bloom = 0.0.xxx;
    if (BloomAmt > 0.001) {
        float reach = 0.10 + PulseBass * 0.55;
        [unroll] for (int i = 1; i <= 8; ++i) {
            float s = float(i) / 8.0;
            float3 c = sampleLinear(centre + dir * (1.0 + s * reach));
            bloom += max(c - kBloomKnee, 0.0) / (1.0 + s * 5.0);
        }
        bloom *= BloomAmt * (0.12 + PulseBass * 1.2);
    }
    col += bloom;

    // Beat flash with user colour.
    col = lerp(col, spSrgbToLinear(FlashColor.rgb), PulseBeat * BeatFlash);

    // Vignette pulses open on bass, so the frame breathes with the track instead of
    // sitting behind a static mask.
    col *= spVignette(uv, VignetteAmt * saturate(1.0 - PulseBass * 1.5), 0.7);

    col *= Exposure;

    // No procedural edges in this shader — every boundary comes from the video's own
    // filtered sampling, so there is nothing for spAAStep to band-limit.

    // tanh over ACES: the bloom accumulator is unbounded, and tanh keeps the roll-off
    // soft enough that a hard hit stays legible as brightness.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
