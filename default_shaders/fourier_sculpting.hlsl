/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "filterMode",     "LABEL": "Filter Type",   "TYPE": "long",
         "VALUES": [0,1,2,3,4,5], "LABELS": ["Low-pass","High-pass","Band-pass","Directional","Annular","Notch"], "DEFAULT": 0},
        {"NAME": "cutoffFreq",     "LABEL": "Cutoff",        "TYPE": "float", "MIN": 0.001, "MAX": 1.0,   "DEFAULT": 0.1},
        {"NAME": "bandWidth",      "LABEL": "Bandwidth",     "TYPE": "float", "MIN": 0.001, "MAX": 0.5,   "DEFAULT": 0.1},
        {"NAME": "filterAngle",    "LABEL": "Angle (deg)",   "TYPE": "float", "MIN": 0.0,   "MAX": 360.0, "DEFAULT": 0.0},
        {"NAME": "angleWidth",     "LABEL": "Angle Width",   "TYPE": "float", "MIN": 1.0,   "MAX": 180.0, "DEFAULT": 45.0},
        {"NAME": "magnitudeGamma", "LABEL": "Magnitude γ",   "TYPE": "float", "MIN": 0.1,   "MAX": 3.0,   "DEFAULT": 1.0},
        {"NAME": "showSpectrum",   "LABEL": "Show Spectrum", "TYPE": "bool",  "DEFAULT": false},
        {"NAME": "Exposure",       "LABEL": "Exposure",      "TYPE": "float", "MIN": 0.1,   "MAX": 4.0,   "DEFAULT": 1.0}
    ]
}*/

// ISF packing: eight scalars at offsets 0..7. 8/32 floats used.

// Fourier-domain sculpting approximation.
// True per-pixel FFT is infeasible in a single pass; each mode is implemented as a
// spatial convolution or frequency-selective kernel that approximates the equivalent
// Fourier-domain mask: low-pass → Gaussian blur, high-pass → unsharp mask,
// band-pass → DoG, directional → 1-D blur along chosen angle, annular → ring DoG,
// notch → directional high-pass perpendicular to angle.

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

// --- Working space ---------------------------------------------------------------
// Every convolution below runs in linear light. Averaging sRGB code values is the
// single largest error in the original: a blur across a hard light/dark edge lands
// well below the true mean radiance, which is why sRGB-space blurs darken and why the
// DoG modes came out muddy.
//
// The linearisation is gamma 2.0 (square / square-root) rather than spSrgbToLinear.
// The exact sRGB curve costs a pow() per tap, and the widest kernel here takes 625
// taps per blur with two blurs in the annular mode, over a thousand pow() calls per
// pixel. Gamma 2.0 is the standard stand-in: the round trip is exact for an unfiltered
// pixel, so nothing shifts when the filter is a no-op, and it recovers almost all of
// the benefit of filtering in linear at two instructions.
float3 toLin(float3 c)  { return c * c; }
float3 toDisp(float3 c) { return sqrt(max(c, 0.0)); }

// Mid-grey (display 0.5) expressed in the working space. The signed-difference modes
// bias by this so negative excursions survive to the output.
static const float kMidGrey = 0.25;

float3 sampleLin(float2 uv) {
    return toLin(videoTexture.Sample(videoSampler, uv).rgb);
}

// Highlight-only roll-off, in linear.
// The library tonemaps are deliberately not used here: they are scene-referred curves
// and this shader's input is already-graded display-referred footage. spTonemapACES
// lifts linear 0.2 to 0.30, so on a low-pass (near-identity) setting it would apply a
// visible grade to the user's picture rather than protect the highlights. This curve is
// the identity below the knee, C1-continuous at it, and asymptotic to 1 above it: a
// safety net for the modes that add energy, and nothing at all for the modes that do not.
float3 rollHighlights(float3 c, float knee) {
    float3 over = max(c - knee, 0.0);
    return min(c, knee) + (1.0 - knee) * over / (over + (1.0 - knee));
}

// Gaussian-weighted box blur with radius controlled by cutoff (lower = wider blur)
float3 gaussianBlur(float2 uv, float2 px, float radius) {
    float3 acc = 0;
    float  wt  = 0;
    int    r   = (int)clamp(radius, 1.0, 12.0);
    [loop] for (int y = -r; y <= r; y++) {
        [loop] for (int x = -r; x <= r; x++) {
            float g = exp(-float(x*x + y*y) / (2.0 * radius * radius));
            acc += sampleLin(uv + float2(x, y) * px) * g;
            wt  += g;
        }
    }
    return acc / wt;
}

// Wedge blur: 1-D blur along `angleDeg`, fanned out to a cone of total width
// `halfWidthDeg`. Each tap's perpendicular offset grows with its distance along the
// axis, so the sampled region is a bow-tie about the chosen direction rather than a
// bare line. This is what `angleWidth` now drives; the parameter was parsed and shown
// in the UI but never read by the shader, so the Directional and Notch modes ignored
// it entirely; at the 45-degree default the wedge is a modest fan.
float3 directionalBlur(float2 uv, float2 px, float angleDeg, float halfWidthDeg, float radius) {
    float  a    = radians(angleDeg);
    float2 dir  = float2(cos(a), sin(a));
    float2 perp = float2(-dir.y, dir.x);
    float  tanH = tan(radians(clamp(halfWidthDeg, 1.0, 170.0)) * 0.5);

    int    r   = (int)clamp(radius, 1.0, 10.0);
    float3 acc = sampleLin(uv);
    float  wt  = 1.0;

    [loop] for (int i = 1; i <= r; i++) {
        float along  = float(i);
        float spread = along * tanH;
        [unroll] for (int s = -1; s <= 1; s += 2) {
            float2 lat = perp * spread * float(s);
            acc += sampleLin(uv + ( dir * along + lat) * px);
            acc += sampleLin(uv + (-dir * along + lat) * px);
            wt  += 2.0;
        }
    }
    return acc / wt;
}

float3 applyGamma(float3 c, float g) {
    return pow(max(c, 0.0), 1.0 / max(g, 0.001));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv   = input.uv;
    float2 px   = 1.0 / resolution;
    float3 orig = sampleLin(uv);

    // Radius maps inversely to cutoffFreq: lower freq = larger kernel
    float kernelR = clamp((1.0 - cutoffFreq) * 12.0 + 1.0, 1.0, 12.0);
    float bandR   = clamp(bandWidth * 12.0 + 0.5, 0.5, 6.0);

    float3 result = orig;

    if (filterMode == 0) {
        // Low-pass: Gaussian blur
        result = gaussianBlur(uv, px, kernelR);

    } else if (filterMode == 1) {
        // High-pass: original minus low-pass residual.
        // Differences are left unclamped here and carried to the roll-off at the end,
        // rather than saturate()d per mode; clipping each channel at its own input
        // level is what tinted the blown areas of the residual.
        float3 lo = gaussianBlur(uv, px, kernelR);
        result = max(orig - lo + kMidGrey, 0.0);

    } else if (filterMode == 2) {
        // Band-pass: difference of Gaussians
        float3 loA = gaussianBlur(uv, px, kernelR);
        float3 loB = gaussianBlur(uv, px, max(kernelR - bandR, 0.5));
        result = max((loB - loA) * 4.0 + kMidGrey, 0.0);

    } else if (filterMode == 3) {
        // Directional: blur along chosen angle, preserve perpendicular edges
        result = directionalBlur(uv, px, filterAngle, angleWidth, kernelR);

    } else if (filterMode == 4) {
        // Annular: band-pass with sharp inner and outer cutoffs (ring in freq domain)
        float3 outerBlur = gaussianBlur(uv, px, kernelR + bandR);
        float3 innerBlur = gaussianBlur(uv, px, max(kernelR - bandR, 0.5));
        result = max((innerBlur - outerBlur) * 3.0 + kMidGrey, 0.0);

    } else {
        // Notch: directional high-pass, removing features aligned to the angle
        float3 dBlur     = directionalBlur(uv, px, filterAngle, angleWidth, kernelR * 0.5);
        float3 dBlurPerp = directionalBlur(uv, px, filterAngle + 90.0, angleWidth, kernelR * 0.5);
        result = max(orig - dBlur * 0.5 + dBlurPerp * 0.3 + kMidGrey * 0.4, 0.0);
    }

    // Magnitude gamma adjustment
    result = applyGamma(result, magnitudeGamma);

    // Spectrum overlay: local spatial frequency magnitude as a heatmap.
    // spPalette rather than the previous float3(f, f*0.4, 1-f) ramp; that ramp was
    // not monotonic in luminance, so equal steps in frequency did not read as equal
    // steps in the image and mid-frequencies were indistinguishable from low.
    if (showSpectrum) {
        float3 hi   = abs(orig - gaussianBlur(uv, px, 2.0));
        float  freq = saturate(spLuma(hi) * 8.0);
        float3 heat = spPalette(freq * 0.75,
                                float3(0.20, 0.10, 0.30),
                                float3(0.65, 0.55, 0.45),
                                float3(1.00, 1.00, 1.00),
                                float3(0.00, 0.25, 0.55));
        result = lerp(result, max(heat, 0.0), 0.6);
    }

    result *= Exposure;

    // No procedural edges: every boundary in the output comes from the footage through
    // a filtered texture fetch, so there is nothing here for spAAStep to band-limit.

    float3 col = toDisp(rollHighlights(result, 0.8));

    // Wide-kernel blurs produce exactly the long shallow ramps that band on 8 bits.
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
