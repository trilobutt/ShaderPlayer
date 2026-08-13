/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "BarBass",     "TYPE": "audio", "BAND": "bass",  "LABEL": "Bass" },
    { "NAME": "BarBeat",     "TYPE": "audio", "BAND": "beat",  "LABEL": "Beat" },
    { "NAME": "BgColor",     "TYPE": "color", "DEFAULT": [0.0, 0.0, 0.06, 1.0], "LABEL": "Background" },
    { "NAME": "BarColor",    "TYPE": "color", "DEFAULT": [0.1, 0.7, 1.0,  1.0], "LABEL": "Bar Colour" },
    { "NAME": "Mirror",      "TYPE": "bool",  "DEFAULT": 1.0,  "LABEL": "Mirror" },
    { "NAME": "BarScale",    "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.1, "MAX": 3.0, "LABEL": "Bar Scale" },
    { "NAME": "PosY",        "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0, "LABEL": "Vertical Position" },
    { "NAME": "BarCount",    "TYPE": "float", "DEFAULT": 128.0, "MIN": 8.0, "MAX": 512.0, "STEP": 1.0, "LABEL": "Segments" },
    { "NAME": "BarGap",      "TYPE": "float", "DEFAULT": 0.25, "MIN": 0.0, "MAX": 0.9, "LABEL": "Segment Gap" },
    { "NAME": "BgOpacity",   "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 1.0, "LABEL": "Background Opacity" },
    { "NAME": "BarOpacity",  "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 1.0, "LABEL": "Spectrum Opacity" },
    { "NAME": "LogScale",    "TYPE": "bool",  "DEFAULT": 1.0,  "LABEL": "Log Frequency" },
    { "NAME": "Exposure",    "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.1, "MAX": 4.0,  "STEP": 0.05, "LABEL": "Exposure" },
    { "NAME": "GlowAmt",     "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 2.0,  "STEP": 0.01, "LABEL": "Glow" },
    { "NAME": "CapWidth",    "TYPE": "float", "DEFAULT": 2.0, "MIN": 0.0, "MAX": 8.0,  "STEP": 0.1,  "LABEL": "Peak Cap (px)" }
  ]
}*/

// ISF packing:
// BgColor    offset 0  → custom[0]
// BarColor   offset 4  → custom[1]
// Mirror     offset 8  → (custom[2].x > 0.5)
// BarScale   offset 9  → custom[2].y
// PosY       offset 10 → custom[2].z
// BarCount   offset 11 → custom[2].w
// BarGap     offset 12 → custom[3].x
// BgOpacity  offset 13 → custom[3].y
// BarOpacity offset 14 → custom[3].z
// LogScale   offset 15 → (custom[3].w > 0.5)
// Exposure   offset 16 → custom[4].x
// GlowAmt    offset 17 → custom[4].y
// CapWidth   offset 18 → custom[4].z
// 19/32 floats used.

// Opacity < 1 requires a Video Blend mode other than Off to show anything behind it:
// the compositor multiplies the blend by this shader's alpha.

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

// Bar index (0..1) → spectrum texture coordinate.
// The analyser's 256 output bins are linear in frequency, so on a linear axis every
// musically interesting bin is crushed into the leftmost ~15% of the texture and the
// rest of the display is dead. The log map redistributes those bins across the full
// width, which is what makes a high segment count worth having.
float freqCoord(float t, bool useLog) {
    return useLog ? (pow(512.0, saturate(t)) - 1.0) / 511.0 : saturate(t);
}

// Max spectrum magnitude over a frequency span, sampled at `taps` points.
// Multiple taps rather than one point sample: this matches the analyser's own
// max-pooling, keeps transients from dropping out between segments, and stops the
// bar tops shimmering.
float spanPeak(float fLo, float fHi, int taps) {
    float mag = 0.0;
    [loop] for (int i = 0; i < taps; ++i) {
        float t = (i + 0.5) / float(taps);
        mag = max(mag, spectrumTexture.SampleLevel(videoSampler,
                                                   float2(lerp(fLo, fHi, t), 0.5), 0).r);
    }
    return mag;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;
    float  xCoord = Mirror ? abs(uv.x * 2.0 - 1.0) : uv.x;

    // Quantise into discrete segments.
    float bars     = clamp(floor(BarCount), 4.0, 512.0);
    float barPos   = xCoord * bars;
    float barIdx   = floor(barPos);
    float barLocal = barPos - barIdx;

    float fLo = freqCoord( barIdx        / bars, LogScale);
    float fHi = freqCoord((barIdx + 1.0) / bars, LogScale);
    float mag = spanPeak(fLo, fHi, 6) * BarScale;

    // Peak cap. There is no previous-frame texture, so a true temporal peak-hold with
    // decay is not available; the cap instead tracks the peak of the surrounding
    // spectral neighbourhood (this segment widened by 1.5 segments each way). That is
    // always >= the bar itself, moves more slowly than the bar top, and falls away as
    // the local peak migrates, giving the same read as a decaying hold, from data
    // rather than from state.
    float capLo = freqCoord((barIdx - 1.5) / bars, LogScale);
    float capHi = freqCoord((barIdx + 2.5) / bars, LogScale);
    float capMag = spanPeak(capLo, capHi, 8) * BarScale;

    // Antialiased segment gap. Width is clamped to the pixel footprint so segments
    // stay clean rather than dropping out once they are narrower than a pixel.
    float aaX      = max(fwidth(barPos), 1e-5) * 0.75;
    float halfGap  = BarGap * 0.5;
    float edgeDist = min(barLocal - halfGap, (1.0 - halfGap) - barLocal);
    float inBarX   = smoothstep(-aaX, aaX, edgeDist);

    // Antialiased bar top and baseline.
    float anchor = 1.0 - PosY;
    float barTop = anchor - mag;
    float aaY    = max(fwidth(uv.y), 1e-5) * 0.75;
    float inBarY = smoothstep(barTop - aaY, barTop + aaY, uv.y) *
                   (1.0 - smoothstep(anchor - aaY, anchor + aaY, uv.y));

    float inBar = inBarX * inBarY;

    // Cap bar: a thin band sitting on the neighbourhood peak, thickness in pixels so it
    // stays one consistent line at any output resolution.
    float capTop  = anchor - capMag;
    float capHalf = CapWidth * 0.5 / max(resolution.y, 1.0);
    float capMask = (1.0 - smoothstep(capHalf - aaY, capHalf + aaY, abs(uv.y - capTop)))
                  * inBarX * step(0.0005, CapWidth) * step(capTop, anchor);

    // Gradient: dim at bottom, bright at top of bar.
    float t = (mag > 0.001) ? saturate((uv.y - barTop) / max(mag, 0.001)) : 0.0;

    // Everything below is linear light. Lerping sRGB values dips in luminance at the
    // midpoint of any ramp and reads as a muddy band.
    float3 barLin = spSrgbToLinear(BarColor.rgb);
    float3 bgLin  = spSrgbToLinear(BgColor.rgb);

    // Beat flash and colour.
    float3 barRgb = lerp(barLin * 0.35, barLin, t);
    barRgb = lerp(barRgb, 1.0.xxx, BarBeat * 0.55);

    // HDR glow above each bar. Inverse distance in pixels rather than an exponential:
    // the 1/d tail keeps a wide, soft halo that the tonemap rolls off, where exp()
    // collapsed to nothing within a few pixels and the "glow" was really just a rim.
    // The gap is halved for the glow only, so the bloom bleeds across segments the way
    // a real one would instead of being cut into strips.
    float glowBarX = smoothstep(-aaX, aaX,
                                min(barLocal - halfGap * 0.5, (1.0 - halfGap * 0.5) - barLocal));
    float glowPx   = max(barTop - uv.y, 0.0) * max(resolution.y, 1.0);
    float glow     = GlowAmt * mag * 2.0 / (1.0 + glowPx * 0.30) * max(glowBarX, 0.25);

    float3 bgRgb = bgLin + barLin * (BarBass * 0.25 + glow);

    float3 col = lerp(bgRgb, barRgb, inBar);
    col = lerp(col, lerp(barLin, 1.0.xxx, 0.6) * 1.6, capMask);
    col *= Exposure;

    // tanh rather than ACES: the glow term is unbounded inverse-distance, and tanh's
    // gentler shoulder keeps the bloom reading as brightness instead of flattening to
    // white the moment it crosses 1.
    col = spLinearToSrgb(spTonemapTanh(col));

    // The background wash and the glow tail are both wide smooth gradients, the worst
    // case for 8-bit banding.
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    // Alpha. Glow and cap keep their own contribution so they stay visible over a
    // transparent background instead of being clipped away with it.
    float bgA  = BgColor.a  * BgOpacity;
    float barA = BarColor.a * BarOpacity;
    float alpha = lerp(max(bgA, saturate(glow) * barA), barA, max(inBar, capMask));

    return float4(saturate(col), saturate(alpha));
}
