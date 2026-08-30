/*{
  "SHADER_TYPE": "video",
  "DESCRIPTION": "Composite CRT emulation: barrel tube curvature, a shadow, slot or Trinitron phosphor mask, scanlines locked to a fixed line count rather than to output resolution, phosphor bloom, defocus, and two kinds of colour misregistration (horizontal gun misconvergence and radial lens dispersion). Everything downstream of the video fetch runs in linear light, so the bloom adds like emitted light instead of clipping.",
  "INPUTS": [
    { "NAME": "Curvature",           "TYPE": "point2d", "MIN": 0.0, "MAX": 1.0, "DEFAULT": [0.5, 0.5], "LABEL": "Curvature H/V" },
    { "NAME": "Vignette",            "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.5,   "LABEL": "Vignette" },
    { "NAME": "MaskType",            "TYPE": "long",  "VALUES": [0,1,2], "LABELS": ["Shadow","Slot","Trinitron"], "DEFAULT": 0, "LABEL": "Mask Type" },
    { "NAME": "MaskScale",           "TYPE": "float", "MIN": 0.5,  "MAX": 3.0,  "DEFAULT": 1.0,   "LABEL": "Mask Scale" },
    { "NAME": "ScanlineStr",         "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.35,  "LABEL": "Scanline Strength" },
    { "NAME": "ScanlineCount",       "TYPE": "long",  "VALUES": [240,480,576,720], "LABELS": ["240 (arcade)","480 (NTSC)","576 (PAL)","720"], "DEFAULT": 480, "LABEL": "Scanlines" },
    { "NAME": "ScanlineTint",        "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0], "LABEL": "Scanline Tint" },
    { "NAME": "BloomAmount",         "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.25,  "LABEL": "Bloom" },
    { "NAME": "BloomThreshold",      "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.75,  "LABEL": "Bloom Threshold" },
    { "NAME": "BloomRadius",         "TYPE": "float", "MIN": 0.0,  "MAX": 8.0,  "DEFAULT": 2.0,   "LABEL": "Bloom Radius (px)" },
    { "NAME": "BleedStrength",       "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.3,   "LABEL": "Colour Bleed" },
    { "NAME": "ChromaticAberration", "TYPE": "float", "MIN": 0.0,  "MAX": 8.0,  "DEFAULT": 0.0,   "LABEL": "Chromatic Aberration (px)" },
    { "NAME": "BlurAmount",          "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.0,   "LABEL": "Defocus" },
    { "NAME": "BlurRadius",          "TYPE": "float", "MIN": 0.0,  "MAX": 4.0,  "DEFAULT": 1.0,   "LABEL": "Defocus Radius (px)" },
    { "NAME": "Exposure",            "TYPE": "float", "MIN": 0.5,  "MAX": 4.0,  "DEFAULT": 1.6,   "LABEL": "Exposure" }
  ]
}*/

// CRT emulation: tube curvature, phosphor mask, scanlines, phosphor bloom,
// defocus, and two kinds of colour misregistration (horizontal gun
// misconvergence and radial lens dispersion).
//
// Everything downstream of the video fetch runs in linear light, because every
// one of these effects is a multiplication of emitted light by a transmission
// factor, and phosphor bloom is light adding to light. The linearisation is
// gamma 2.0 (square/sqrt) rather than the exact sRGB curve: there are up to 30
// texture fetches per pixel to convert, the round trip is exact when the
// controls are at zero, and the residual difference against true sRGB is far
// below the mask and scanline modulation sitting on top of it.
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

// Tube curvature. Edge-weighted rather than radial: the horizontal displacement
// is driven by |y| and the vertical by |x|, so the picture stays flat along both
// centre lines and bows only toward the corners. That is how a tube is actually
// shaped, and it is the thing a k1*r^2 lens term gets wrong: a lens bulges the
// middle of every edge as hard as the corners.
//
// The upstream form takes its curvature as a DIVISOR: larger means flatter and
// zero is a division by zero, which is not a control to hand a user. The slider
// here is the reciprocal of it (0 is dead flat, 1 is maximum bulge), so the
// divisor is kCurveDiv/Curvature and the reciprocal cancels into a multiply that
// is exactly zero at zero. kCurveDiv is therefore the divisor at full slider.
static const float kCurveDiv = 1.6;

float2 crtCurve(float2 uv, float2 amount) {
    float2 p = uv * 2.0 - 1.0;
    float2 warp = abs(p.yx) * (amount / kCurveDiv);
    p += p * warp * warp;
    return p * 0.5 + 0.5;
}

// Linear-light fetch, masked to the source image. A tap landing off the picture
// must contribute nothing; clamp addressing instead smears the edge texel
// outward along the whole border, which is very visible once curvature pushes
// the corners past the frame. w is the footprint of the tube edge, measured once
// on the continuous warped coordinate in main and passed in, so this stays free
// of derivatives and can be called from inside branches and unrolled loops. It
// is a smoothstep and not a step for the same reason the border below is: a hard
// test here puts the one-pixel staircase straight back.
float3 fetchLin(float2 uv, float w) {
    // SampleLevel rather than Sample: these taps are loop-carried and sit under
    // uniform branches. Every texture in this pipeline is MipLevels = 1, so the
    // two return identical values.
    float3 c = videoTexture.SampleLevel(videoSampler, uv, 0).rgb;
    float2 d = abs(uv - 0.5) * 2.0;
    float  inside = 1.0 - smoothstep(1.0 - w, 1.0 + w, max(d.x, d.y));
    return c * c * inside;
}

// One beam sample: the three guns do not land in the same place, so each channel
// comes from its own offset. Both misregistration terms are folded into rOff and
// bOff by the caller, which keeps a sample at three fetches however many of them
// are switched on.
float3 fetchBeam(float2 uv, float2 rOff, float2 bOff, float w) {
    return float3(fetchLin(uv + rOff, w).r,
                  fetchLin(uv,        w).g,
                  fetchLin(uv + bOff, w).b);
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

    float2 warpUV = crtCurve(uv, Curvature);

    // Tube edge. The previous hard test produced a jagged one-pixel staircase all
    // the way round the curved border, which is the first thing the eye lands on.
    // edgeMax is continuous in the warped coordinate, so its derivative is a real
    // footprint and it doubles as the filter width for every fetch below.
    float2 edgeDist = abs(warpUV - 0.5) * 2.0;
    float  edgeMax  = max(edgeDist.x, edgeDist.y);
    float  ew       = max(fwidth(edgeMax), 1e-5);
    float  border   = 1.0 - smoothstep(1.0 - ew, 1.0 + ew, edgeMax);

    // Two independent misregistrations, summed into one pair of channel offsets.
    // BleedStrength is horizontal beam misconvergence, a property of the gun
    // assembly and so the same everywhere on the tube. ChromaticAberration is
    // lens dispersion, which splits the channels along the radius from the screen
    // centre; the 1e-4 is what keeps normalize() defined at the exact centre.
    float  bleedAmt = BleedStrength * 0.003;
    float2 caDir    = normalize(uv * 2.0 - 1.0 + 1e-4);
    float2 caOff    = caDir * (ChromaticAberration / resolution);
    float2 rOff     = float2(-bleedAmt, 0.0) + caOff;
    float2 bOff     = float2( bleedAmt, 0.0) - caOff;

    float3 col = fetchBeam(warpUV, rOff, bOff, ew);

    // Defocus: a beam that is not sharply converged on the phosphor, as a 4-tap
    // cross in linear light. It runs before the bloom so a defocused tube blooms
    // from the softened image, which is the order the two happen in physically.
    if (BlurAmount > 1e-3) {
        float2 blurStep = BlurRadius / resolution;
        float3 blurSum = fetchBeam(warpUV + float2(blurStep.x, 0.0), rOff, bOff, ew)
                       + fetchBeam(warpUV - float2(blurStep.x, 0.0), rOff, bOff, ew)
                       + fetchBeam(warpUV + float2(0.0, blurStep.y), rOff, bOff, ew)
                       + fetchBeam(warpUV - float2(0.0, blurStep.y), rOff, bOff, ew);
        col = lerp(col, blurSum * 0.25, BlurAmount);
    }

    // Phosphor bloom: bright areas spill into their neighbours. Added, not
    // blended: this is light leaving one phosphor and arriving at another, and
    // the previous double application (a lerp toward a sum already scaled by the
    // same slider) meant the control did nothing for its first third of travel.
    if (BloomAmount > 1e-3) {
        float2 bloomStep = BloomRadius / resolution;
        float3 bloomCol = 0.0;
        [unroll]
        for (int bx = -2; bx <= 2; ++bx) {
            for (int by = -1; by <= 1; ++by) {
                bloomCol += fetchLin(warpUV + float2(bx, by) * bloomStep, ew);
            }
        }
        bloomCol /= 15.0;
        // Threshold knee: nothing under BloomThreshold blooms at all and the
        // response above it is linear up to white. The fixed smoothstep that used
        // to sit here gave neither end of that to the user.
        float bloomMask = saturate(max(0.0, spLuma(col) - BloomThreshold)
                                   / max(1e-4, 1.0 - BloomThreshold));
        col += bloomCol * bloomMask * BloomAmount * 1.5;
    }

    // Scanlines, band-limited. spBandLimitedCos collapses to zero once the line
    // pitch passes the pixel footprint, leaving a uniform dim rather than moire.
    float scanPhase = warpUV.y * float(ScanlineCount) * SP_PI;
    float scanW     = max(fwidth(scanPhase), 1e-5);
    float scan      = 0.5 - 0.5 * spBandLimitedCos(scanPhase, scanW);
    col *= 1.0 - ScanlineStr * scan;
    // Phosphor colour. lerp(1, tint, k) is exactly 1 for a white tint, so the
    // default leaves the luminance-only dip above untouched; a warm or green tint
    // colours the gaps between the lines the way a single-phosphor tube does.
    // Alpha is the tint's own strength, per the colour-alpha convention: at 1 this is
    // unchanged, at 0 the tint drops out and the luminance dip above stands alone.
    col *= lerp(float3(1.0, 1.0, 1.0), ScanlineTint.rgb, scan * ScanlineStr * ScanlineTint.a);

    // Mask coordinate is continuous, so its derivative is exact even under the
    // curvature warp. Fold to the triad only after measuring the footprint.
    float2 maskUV = warpUV * resolution / max(MaskScale, 0.01);
    float  maskW  = max(max(fwidth(maskUV.x), fwidth(maskUV.y)), 1e-4) * 0.5;
    col *= phosphorMask(maskUV, maskW, MaskType);

    // Corner falloff. A fifth power of the larger of the two axis distances,
    // quartered before it is raised and scaled by 300 after: flat to within a
    // percent across the middle of the tube and then off a cliff in the last
    // tenth, which is what distinguishes a tube's corner shadowing from a
    // photographic vignette. Measured on the unwarped screen coordinate and
    // applied here in linear light, before the tonemap, since it is a loss of
    // emitted light and not a grade.
    float2 vDist = abs(uv * 2.0 - 1.0) * 0.25;
    float  vOff  = max(vDist.x, vDist.y);
    float  vOff2 = vOff * vOff;
    float  vig   = max(0.0, 1.0 - Vignette * 300.0 * vOff2 * vOff2 * vOff);

    col *= border * vig * Exposure;

    // The mask and scanlines together take a large bite out of the average level,
    // and Exposure puts it back, which drives the highlights well past white.
    // Tonemapping is what turns that into phosphor glare instead of flat clipping.
    col = sqrt(spTonemapACES(col));

    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
