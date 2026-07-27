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
    { "NAME": "LogScale",    "TYPE": "bool",  "DEFAULT": 1.0,  "LABEL": "Log Frequency" }
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

// Opacity < 1 requires a Video Blend mode other than Off to show anything behind it —
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

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;
    float  xCoord = Mirror ? abs(uv.x * 2.0 - 1.0) : uv.x;

    // Quantise into discrete segments.
    float bars     = clamp(floor(BarCount), 4.0, 512.0);
    float barPos   = xCoord * bars;
    float barIdx   = floor(barPos);
    float barLocal = barPos - barIdx;

    // Peak over the segment's frequency span. Multiple taps rather than one point
    // sample: this matches the analyser's own max-pooling, keeps transients from
    // dropping out between segments, and stops the bar tops shimmering.
    float fLo = freqCoord( barIdx        / bars, LogScale);
    float fHi = freqCoord((barIdx + 1.0) / bars, LogScale);
    float mag = 0.0;
    [unroll] for (int i = 0; i < 6; ++i) {
        float t = (i + 0.5) / 6.0;
        mag = max(mag, spectrumTexture.SampleLevel(videoSampler,
                                                   float2(lerp(fLo, fHi, t), 0.5), 0).r);
    }
    mag *= BarScale;

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

    // Gradient: dim at bottom, bright at top of bar.
    float t = (mag > 0.001) ? saturate((uv.y - barTop) / max(mag, 0.001)) : 0.0;

    // Beat flash and colour.
    float3 barRgb = lerp(BarColor.rgb * 0.4, BarColor.rgb, t);
    barRgb = lerp(barRgb, float3(1, 1, 1), BarBeat * 0.55);

    // Glow above each bar, and a bass wash on the background.
    float glowDist = max(0.0, barTop - uv.y);
    float glowAmt  = exp(-glowDist * 30.0) * mag * 0.5 * inBarX;

    float3 bgRgb = BgColor.rgb + BarColor.rgb * (BarBass * 0.25 + glowAmt);

    float3 col   = lerp(bgRgb, barRgb, inBar);

    // Alpha. Glow keeps its own contribution so it stays visible over a transparent
    // background instead of being clipped away with it.
    float bgA  = BgColor.a  * BgOpacity;
    float barA = BarColor.a * BarOpacity;
    float alpha = lerp(max(bgA, glowAmt * barA), barA, inBar);

    return float4(col, saturate(alpha));
}
