/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "ScrollSpeed",  "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.12, "LABEL": "Scroll Speed" },
    { "NAME": "FoldStrength", "TYPE": "float", "MIN": 0.0, "MAX": 0.3,  "DEFAULT": 0.08, "LABEL": "Fold Strength" },
    { "NAME": "FaultThresh",  "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.55, "LABEL": "Fault Threshold" },
    { "NAME": "ColourSat",    "TYPE": "float", "MIN": 0.0, "MAX": 2.0,  "DEFAULT": 1.2,  "LABEL": "Colour Saturation" },
    { "NAME": "BedSoftness",  "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.7,  "LABEL": "Bed Softness" },
    { "NAME": "SeamWidth",    "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.35, "LABEL": "Seam Width" },
    { "NAME": "MicaAmt",      "TYPE": "float", "MIN": 0.0, "MAX": 2.0,  "DEFAULT": 0.6,  "LABEL": "Mica Glint" },
    { "NAME": "Exposure",     "TYPE": "float", "MIN": 0.1, "MAX": 4.0,  "DEFAULT": 1.0,  "LABEL": "Exposure" },
    { "NAME": "BaseTint",     "TYPE": "color",                             "DEFAULT": [0.75,0.62,0.40,1.0], "LABEL": "Base Tint" },
    { "NAME": "VignetteAmt",  "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.3,  "LABEL": "Vignette" },
    { "NAME": "BassBand",     "TYPE": "audio", "BAND": "bass",  "LABEL": "Bass" },
    { "NAME": "MidBand",      "TYPE": "audio", "BAND": "mid",   "LABEL": "Mid" },
    { "NAME": "HighBand",     "TYPE": "audio", "BAND": "high",  "LABEL": "Treble" },
    { "NAME": "BeatIn",       "TYPE": "audio", "BAND": "beat",  "LABEL": "Beat" }
  ]
}*/

// ISF packing: eight scalars at offsets 0..7, BaseTint at 8..11 on its 4-float
// boundary, VignetteAmt at 12. 13/32 floats used.

// Audio deposits virtual sedimentary layers in real time.
// Low frequencies produce thick warm-coloured strata; high frequencies create
// fine pale laminae. Beat transients above threshold inject fault displacements.
// The canvas scrolls upward like a drill core being extracted.
//
// The analyser already applies an EMA to every band (Audio settings → Smoothing),
// so no per-shader temporal filtering is added here; there is no previous-frame
// texture to do it with in any case.

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

// Band profile for one stratum period.
// A raw frac() sawtooth has a hard discontinuity at every bed boundary, which is
// what reads as "harsh / pixelated": the step lands between samples and stair-steps
// along the fold. Blending toward a cosine profile removes the discontinuity, and
// fading to the period mean once the period drops below a pixel (fw > 0.5) removes
// the moire that otherwise appears in the fine laminae.
float bedProfile(float phase, float softness, float fw) {
    float ph   = frac(phase);
    float saw  = ph;
    float smth = 0.5 - 0.5 * cos(6.2831853 * ph);
    float band = lerp(saw, smth, saturate(softness + fw));
    return lerp(band, 0.5, saturate(fw - 0.5));
}

// Analytically antialiased boundary line: width never falls below the pixel
// footprint, so the seam stays a clean line instead of aliasing into dots.
float bedSeam(float phase, float width, float fw) {
    float dist = abs(frac(phase + 0.5) - 0.5) * 2.0;   // 0 at boundary, 1 at bed centre
    float w    = max(width, fw * 2.0);
    return 1.0 - smoothstep(0.0, w, dist);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Scroll coordinate: UV.y advances upward over time.
    float depth = uv.y + time * ScrollSpeed;

    // Bass-driven large-scale folding of strata (anticlinal / synclinal structures).
    float foldWave = sin(uv.x * 7.0 + depth * 2.5) * FoldStrength * BassBand;
    float warpedDepth = depth + foldWave;

    // Mid-frequency fine folding.
    warpedDepth += sin(uv.x * 18.0 + depth * 7.0) * FoldStrength * 0.4 * MidBand;

    // Beat transients inject fault / unconformity displacements.
    // Ramp in over the threshold rather than switching on it, and use a soft sign so
    // the fault edge is a gradient rather than a one-pixel tear.
    float faultAmt  = smoothstep(FaultThresh, FaultThresh + 0.2, BeatIn) * BeatIn * 0.15;
    float faultWave = sin(uv.x * 30.0 + depth * 5.0);
    float faultDir  = faultWave * rsqrt(faultWave * faultWave + 0.05);
    float faultedDepth = warpedDepth + faultDir * faultAmt * sin(depth * 20.0 + time * 10.0);

    // Stratum grain at three scales. Two octaves left the rock flat under a zoom:
    // the coarse bands carried the image and everything between them was featureless.
    // faultedDepth is a smooth function of uv, so implicit-LOD Sample is valid here.
    // It buys no filtering, though: the noise texture is created with a single mip
    // level, so the micro octave is point-sampled at 44x and will sparkle under
    // motion. Band-limiting it needs an explicit fade on its own footprint.
    float2 strataUV   = float2(uv.x * 1.8, faultedDepth * 3.5);
    float2 fineUV     = float2(uv.x * 5.0, faultedDepth * 12.0);
    float2 microUV    = float2(uv.x * 19.0, faultedDepth * 44.0);
    float coarseGrain = noiseTexture.Sample(noiseSampler, strataUV).r;
    float fineGrain   = noiseTexture.Sample(noiseSampler, fineUV + float2(0.3, 0.7)).r;
    float microGrain  = noiseTexture.Sample(noiseSampler, microUV + float2(0.61, 0.19)).r;

    // Stratum thickness: bass creates thick beds, treble creates thin laminae.
    float bedThickness = 3.5 + BassBand * 8.0;
    float laminaFreq   = bedThickness * 4.0 + HighBand * 20.0;

    float bedPhase    = faultedDepth * bedThickness;
    float laminaPhase = faultedDepth * laminaFreq;
    float bedFw       = fwidth(bedPhase);
    float laminaFw    = fwidth(laminaPhase);

    float bed    = bedProfile(bedPhase,    BedSoftness, bedFw);
    float lamina = bedProfile(laminaPhase, BedSoftness, laminaFw);

    // Grain texture within each bed.
    float grain = coarseGrain * 0.52 + fineGrain * 0.32 + microGrain * 0.16;
    float strataPattern = bed * 0.6 + lamina * 0.2 + grain * 0.2;

    // Colour palette: bass = warm red-brown, mid = tan/ochre, high = pale grey.
    // Kept as named rock stops rather than an IQ cosine palette: the four minerals
    // are the shader's identity, and a continuous ramp cannot hit "red sandstone into
    // pale shale" without passing through hues no rock has. Converted to linear so the
    // lerps between them do not dip in luminance at the crossover.
    float3 warmCol    = spSrgbToLinear(float3(0.60, 0.28, 0.10));   // red sandstone
    float3 neutralCol = spSrgbToLinear(float3(0.75, 0.62, 0.40));   // buff limestone
    float3 coolCol    = spSrgbToLinear(float3(0.90, 0.90, 0.85));   // pale shale
    float3 darkCol    = spSrgbToLinear(float3(0.20, 0.14, 0.08));   // dark mudstone

    float bassWeight = saturate(BassBand * 2.0);
    float highWeight = saturate(HighBand * 3.0);
    float3 baseCol   = lerp(lerp(neutralCol, warmCol, bassWeight), coolCol, highWeight);

    // Darker bands at bed boundaries (compression seams).
    float seam = bedSeam(bedPhase, SeamWidth * 0.5, bedFw);
    baseCol    = lerp(baseCol, darkCol, seam * 0.7);

    float3 col = baseCol * (0.55 + strataPattern * 0.9);
    col        = lerp(neutralCol * 0.5, col, ColourSat);
    col       *= spSrgbToLinear(BaseTint.rgb);

    // Mica glint: sparse specular flecks on the lamina crests, accumulated in HDR so a
    // fleck is a point of real brightness the tonemap rolls off, not a white pixel.
    // Treble drives it, at 8x because the high band sits around 0.02-0.15 on real music.
    if (MicaAmt > 0.001) {
        float2 cell  = floor(float2(uv.x * 260.0, faultedDepth * 220.0));
        float  spark = spHash12(cell);
        float  crest = saturate(lamina * 1.6 - 0.55);
        float  glint = pow(max(spark - 0.90, 0.0) * 10.0, 2.0) * crest;
        col += float3(1.0, 0.95, 0.86) * glint * MicaAmt * (0.15 + HighBand * 8.0);
    }

    // Flash the whole frame on a strong beat (volcanic intrusion). Additive and
    // unbounded; the tonemap decides where it rolls rather than a hard clip that
    // would shift the hue channel by channel.
    float flashBright = max(0.0, BeatIn - FaultThresh) * 2.5;
    col += spSrgbToLinear(float3(1.0, 0.85, 0.5)) * flashBright * 0.5;

    col *= spVignette(uv, VignetteAmt, 0.75);
    col *= Exposure;

    col = spLinearToSrgb(spTonemapACES(col));

    // The bed gradients are broad and low-contrast, which is textbook 8-bit banding.
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
