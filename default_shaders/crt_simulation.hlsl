/*{
  "SHADER_TYPE": "video",
  "INPUTS": [
    { "NAME": "BarrelStrength", "TYPE": "float", "MIN": 0.0,  "MAX": 0.5,  "DEFAULT": 0.18,  "LABEL": "Barrel Distortion" },
    { "NAME": "MaskType",       "TYPE": "long",  "VALUES": [0,1,2], "LABELS": ["Shadow","Slot","Trinitron"], "DEFAULT": 0, "LABEL": "Mask Type" },
    { "NAME": "MaskScale",      "TYPE": "float", "MIN": 0.5,  "MAX": 3.0,  "DEFAULT": 1.0,   "LABEL": "Mask Scale" },
    { "NAME": "ScanlineStr",    "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.35,  "LABEL": "Scanline Strength" },
    { "NAME": "ScanlineCount",  "TYPE": "long",  "VALUES": [240,480,576,720], "LABELS": ["240 (arcade)","480 (NTSC)","576 (PAL)","720"], "DEFAULT": 480, "LABEL": "Scanlines" },
    { "NAME": "BloomAmount",    "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.25,  "LABEL": "Bloom" },
    { "NAME": "BleedStrength",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.3,   "LABEL": "Colour Bleed" },
    { "NAME": "Exposure",       "TYPE": "float", "MIN": 0.5,  "MAX": 4.0,  "DEFAULT": 1.6,   "LABEL": "Exposure" }
  ]
}*/

// CRT emulation: barrel distortion, phosphor mask, scanlines, phosphor bloom,
// and sub-pixel colour bleed.
//
// Everything downstream of the video fetch runs in linear light, because every
// one of these effects is a multiplication of emitted light by a transmission
// factor, and phosphor bloom is light adding to light. The linearisation is
// gamma 2.0 (square/sqrt) rather than the exact sRGB curve: there are 18 texture
// fetches per pixel to convert, the round trip is exact when the controls are at
// zero, and the residual difference against true sRGB is far below the mask and
// scanline modulation sitting on top of it.
//
// Scanlines are tied to a line count, not to the output resolution. A CRT has a
// fixed number of lines however large the tube is, and the previous
// one-cycle-per-two-output-pixels behaviour sat exactly at Nyquist, so it aliased
// into a moire wash at every window size that was not the native one. They are
// also band-limited: as the lines approach the pixel footprint they fade into a
// flat dim rather than beating against the pixel grid. Same for the mask, which
// fades out once its triad is finer than a pixel.

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

float3 fetchLin(float2 uv) {
    float3 c = videoTexture.Sample(videoSampler, uv).rgb;
    return c * c;
}

// Barrel / pincushion distortion.
float2 barrelDistort(float2 uv, float k1) {
    float2 c = uv - 0.5;
    float r2 = dot(c, c);
    return 0.5 + c * (1.0 + k1 * r2);
}

// Three phosphor mask patterns. `m` is the mask-space coordinate (one triad per
// 3 units horizontally, one row per 2 units vertically) and `w` is the width of
// one screen pixel in those same units, measured on the continuous coordinate
// before it is folded. Every edge is filtered over w, and the whole mask lerps
// to white once w approaches the size of a phosphor, since at that point the
// mask carries no information the display can show and only produces beating.
float3 phosphorMask(float2 m, float w, int mtype) {
    float px = fmod(m.x, 3.0);
    float py = fmod(m.y, 2.0);

    float3 mask;

    if (mtype == 0) {
        // Shadow mask: circular phosphor dots, one triad across.
        float3 dx = float3(px - 0.5, px - 1.5, px - 2.5);
        float  dy = py - 0.5;
        float3 d2 = dx * dx + dy * dy;
        // The threshold band is widened by the footprint, so a dot smaller than
        // a pixel dissolves instead of flickering on and off between frames.
        mask = 1.0 - smoothstep(0.18 - w, 0.38 + w, d2);
    } else if (mtype == 1) {
        // Slot mask: vertical stripes, offset on alternate rows.
        float offset = (py > 1.0) ? 1.5 : 0.0;
        float pxOff  = fmod(px + offset, 3.0);
        float3 dist  = abs(float3(pxOff - 0.5, pxOff - 1.5, pxOff - 2.5));
        // Wrap the distance so a stripe centred at 0.5 is still near px = 2.9.
        dist = min(dist, 3.0 - dist);
        float3 stripe = 1.0 - smoothstep(0.45 - w, 0.45 + w, dist);
        mask = lerp(float3(0.3, 0.3, 0.3), stripe, 0.8) + 0.1;
    } else {
        // Trinitron aperture grille: vertical stripes, no horizontal breaks.
        float3 dist = abs(float3(px - 0.5, px - 1.5, px - 2.5));
        dist = min(dist, 3.0 - dist);
        float3 stripe = 1.0 - smoothstep(0.42 - w, 0.42 + w, dist);
        mask = lerp(float3(0.2, 0.2, 0.2), stripe, 0.85) + 0.08;
    }

    // w is in mask units; one phosphor is ~1 unit wide, so a footprint past ~1
    // unit cannot resolve the triad at all.
    float resolvable = saturate(1.25 - w);
    return lerp(1.0, mask, resolvable);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    float2 warpUV = barrelDistort(uv, BarrelStrength * 0.5);

    // Tube edge. The previous hard test produced a jagged one-pixel staircase all
    // the way round the curved border, which is the first thing the eye lands on.
    float2 edgeDist = abs(warpUV - 0.5) * 2.0;
    float  edgeMax  = max(edgeDist.x, edgeDist.y);
    float  ew       = max(fwidth(edgeMax), 1e-5);
    float  border   = 1.0 - smoothstep(1.0 - ew, 1.0 + ew, edgeMax);
    float  vigSmooth = smoothstep(1.0, 0.7, edgeMax);

    // Sub-pixel colour bleed: the electron beams for the three guns do not land
    // in exactly the same place.
    float bleedAmt = BleedStrength * 0.003;
    float3 col = float3(fetchLin(warpUV + float2(-bleedAmt, 0)).r,
                        fetchLin(warpUV).g,
                        fetchLin(warpUV + float2( bleedAmt, 0)).b);

    // Phosphor bloom: bright areas spill into their neighbours. Added, not
    // blended: this is light leaving one phosphor and arriving at another, and
    // the previous double application (a lerp toward a sum already scaled by the
    // same slider) meant the control did nothing for its first third of travel.
    if (BloomAmount > 1e-3) {
        float bloomRadius = BloomAmount * 0.012;
        float3 bloomCol = 0.0;
        [unroll]
        for (int bx = -2; bx <= 2; ++bx) {
            for (int by = -1; by <= 1; ++by) {
                bloomCol += fetchLin(warpUV + float2(bx, by) * bloomRadius);
            }
        }
        bloomCol /= 15.0;
        float bloomMask = smoothstep(0.25, 0.8, spLuma(col));
        col += bloomCol * bloomMask * BloomAmount * 1.5;
    }

    // Scanlines, band-limited. spBandLimitedCos collapses to zero once the line
    // pitch passes the pixel footprint, leaving a uniform dim rather than moire.
    float scanPhase = warpUV.y * float(ScanlineCount) * SP_PI;
    float scanW     = max(fwidth(scanPhase), 1e-5);
    float scan      = 0.5 - 0.5 * spBandLimitedCos(scanPhase, scanW);
    col *= 1.0 - ScanlineStr * scan;

    // Mask coordinate is continuous, so its derivative is exact even under the
    // barrel warp. Fold to the triad only after measuring the footprint.
    float2 maskUV = warpUV * resolution / max(MaskScale, 0.01);
    float  maskW  = max(max(fwidth(maskUV.x), fwidth(maskUV.y)), 1e-4) * 0.5;
    col *= phosphorMask(maskUV, maskW, MaskType);

    col *= border * vigSmooth * Exposure;

    // The mask and scanlines together take a large bite out of the average level,
    // and Exposure puts it back, which drives the highlights well past white.
    // Tonemapping is what turns that into phosphor glare instead of flat clipping.
    col = sqrt(spTonemapACES(col));

    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
