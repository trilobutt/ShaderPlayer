/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "SymmetryOrder", "TYPE": "long",
      "VALUES": [1,2,3,4,5,6,8,12], "LABELS": ["1","2","3","4","5","6","8","12"],
      "DEFAULT": 4, "LABEL": "Symmetry" },
    { "NAME": "InnerRadius",   "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.08,"LABEL": "Inner Radius" },
    { "NAME": "RotSpeed",      "TYPE": "float", "MIN": -2.0,"MAX": 2.0,  "DEFAULT": 0.2, "LABEL": "Rotation Speed" },
    { "NAME": "GlowWidth",     "TYPE": "float", "MIN": 0.5, "MAX": 8.0,  "DEFAULT": 3.0, "LABEL": "Glow Width" },
    { "NAME": "CoreColour",    "TYPE": "color",             "DEFAULT": [0.2, 0.5, 1.0, 1.0], "LABEL": "Core Colour" },
    { "NAME": "OuterColour",   "TYPE": "color",             "DEFAULT": [1.0, 0.25, 0.05, 1.0], "LABEL": "Outer Colour" },
    { "NAME": "CoreSize",      "TYPE": "float", "MIN": 0.0, "MAX": 2.0,  "DEFAULT": 0.35,"LABEL": "Core Size" },
    { "NAME": "RingRadius",    "TYPE": "float", "MIN": 0.1, "MAX": 1.5,  "DEFAULT": 0.42,"LABEL": "Ring Radius" },
    { "NAME": "SpokeAmt",      "TYPE": "float", "MIN": 0.0, "MAX": 2.0,  "DEFAULT": 0.7, "LABEL": "Spokes" },
    { "NAME": "SpokeCount",    "TYPE": "float", "MIN": 2.0, "MAX": 48.0, "STEP": 1.0, "DEFAULT": 14.0, "LABEL": "Spoke Count" },
    { "NAME": "PaletteShift",  "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.0, "LABEL": "Palette Shift" },
    { "NAME": "Exposure",      "TYPE": "float", "MIN": 0.1, "MAX": 4.0,  "DEFAULT": 1.0, "LABEL": "Exposure" },
    { "NAME": "VignetteAmt",   "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.25,"LABEL": "Vignette" },
    { "NAME": "AudioBassIn",   "TYPE": "audio", "BAND": "bass", "LABEL": "Bass" },
    { "NAME": "AudioHighIn",   "TYPE": "audio", "BAND": "high", "LABEL": "Treble" }
  ]
}*/

// ISF packing: SymmetryOrder/InnerRadius/RotSpeed/GlowWidth at 0..3, CoreColour at
// 4..7, OuterColour at 8..11, then seven scalars at 12..18. 19/32 floats used.

// FFT bins mapped to polar coordinates with N-fold rotational symmetry.
// Bass bins expand the luminous core; treble detail extends radial spokes.
// Produces mandala-like forms that breathe with the audio.
//
// Every luminous element accumulates as inverse distance rather than as a Gaussian.
// exp(-d*d*k) is bounded by 1 and dies inside a handful of pixels, so it reads as a
// soft-edged solid; tint/d is unbounded with a long tail, which is what actually makes
// a bright thing look bright once a tonemap is rolling the highlights off.

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

// Burst palette. Replaces the previous full-sweep HSV rainbow, which put every hue on
// screen at equal saturation and left nothing for the eye to settle on; this keeps a
// blue-through-amber axis and exposes the phase as PaletteShift instead of a raw hue.
float3 burstPalette(float t) {
    return spPalette(t + PaletteShift,
                     float3(0.50, 0.45, 0.45),
                     float3(0.50, 0.48, 0.45),
                     float3(1.00, 1.00, 1.00),
                     float3(0.00, 0.18, 0.42));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Centre and correct for non-square viewport.
    float2 centred = uv - 0.5;
    centred.x *= resolution.x / resolution.y;

    float r      = length(centred);
    float angle  = atan2(centred.y, centred.x) + time * RotSpeed;

    // Fold angle into one sector of N-fold symmetry. The triangle fold on the final
    // line matters: it makes t reflect rather than wrap at the sector boundary, so t is
    // continuous everywhere and fwidth(t) below is meaningful.
    float sym          = float(SymmetryOrder);
    float sectorAngle  = 2.0 * SP_PI / sym;
    float a = fmod(angle + 64.0 * SP_PI, sectorAngle);
    float t = a / sectorAngle;
    t = 1.0 - abs(t * 2.0 - 1.0);

    // Derivative of the sector coordinate, taken before any branching. Near the centre
    // one pixel spans the whole sector, so this grows without bound, which is exactly
    // what the band-limited cosine needs to fade the spokes out there.
    float tFoot = fwidth(t);

    // Sample spectrum at this angular position.
    float specVal = spectrumTexture.SampleLevel(videoSampler, float2(t, 0.5), 0).r;

    // Core size: CoreSize sets the resting radius, bass expands it from there.
    float coreR   = CoreSize * (0.22 + AudioBassIn * 1.1);
    float targetR = InnerRadius + coreR + specVal * max(0.0, RingRadius - InnerRadius - coreR);

    // Treble adds a fine inner ring.
    float trebleR = InnerRadius * 0.4 + AudioHighIn * 0.1;

    // Inverse-distance ring glow. GlowWidth keeps its original sense (larger = tighter)
    // by scaling the distance denominator instead of a Gaussian exponent.
    float dMain   = abs(r - targetR);
    float dTreble = abs(r - trebleR);
    float glowMain   = (specVal + 0.15) * (1.0 + AudioBassIn * 2.0)
                     / (1.0 + dMain * GlowWidth * 45.0);
    float glowTreble = AudioHighIn * 3.0
                     / (1.0 + dTreble * GlowWidth * 140.0);

    // Palette in linear light, indexed by sector position and spectral energy.
    float3 pal = burstPalette(t * 0.55 + specVal * 0.25 + time * 0.04);
    float3 col = pal * (glowMain + glowTreble * 0.6);

    // Radial spokes. Second, finer angular octave so the form holds up between the
    // ring and the core instead of leaving a flat gap there. Band-limited against
    // tFoot: without it the spokes converge on the centre and alias into a rosette.
    if (SpokeAmt > 0.001) {
        float k     = max(floor(SpokeCount), 2.0);
        float spoke = spBandLimitedCos(t * SP_TAU * k, tFoot * SP_TAU * k) * 0.5 + 0.5;
        float radial = saturate(1.0 - r / max(targetR + 0.15, 1e-3));
        col += burstPalette(t * 0.55 + 0.18) * spoke * radial * radial
             * (specVal + AudioHighIn * 1.5) * SpokeAmt * 1.6;
    }

    // Modulate towards the user colours based on radial position. These are the
    // shader's interface, so they stay; the palette supplies the variation between them.
    float radialT  = saturate(r / max(targetR + 0.05, 0.001));
    float3 userCol = lerp(spSrgbToLinear(CoreColour.rgb), spSrgbToLinear(OuterColour.rgb), radialT);
    col = lerp(col, col * userCol * 2.0, 0.55);

    // Luminous core, also inverse distance. The 0.4 floor keeps it visible between
    // beats instead of collapsing to nothing.
    float coreGlow = (0.4 + AudioBassIn * 3.1) * coreR / (r + coreR * 0.35 + 1e-3);
    col += spSrgbToLinear(CoreColour.rgb) * coreGlow * 0.5;

    // Extra brightness boost on strong bass.
    col *= 1.0 + AudioBassIn * 1.5;

    col *= spVignette(uv, VignetteAmt, 0.8);
    col *= Exposure;

    // tanh: every term above is inverse-distance and unbounded, and tanh's shoulder
    // keeps the core reading as a bright object rather than a white disc.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
