/*{
    "SHADER_TYPE": "audio",
    "INPUTS": [
        {"NAME": "bassLevel",        "LABEL": "Bass",            "TYPE": "audio",  "BAND": "bass"},
        {"NAME": "highLevel",        "LABEL": "Treble",          "TYPE": "audio",  "BAND": "high"},
        {"NAME": "TerrainTint",      "LABEL": "Terrain Tint",    "TYPE": "color",  "DEFAULT": [0.35,0.42,0.55,1.0]},
        {"NAME": "EruptionColor",    "LABEL": "Eruption Colour", "TYPE": "color",  "DEFAULT": [1.0,0.6,0.2,1.0]},
        {"NAME": "meshResolution",   "LABEL": "Depth Layers",    "TYPE": "long",
         "VALUES": [16,32,48,64], "LABELS": ["16 (fast)","32","48","64 (quality)"], "DEFAULT": 32},
        {"NAME": "heightScale",      "LABEL": "Height Scale",    "TYPE": "float",  "MIN": 0.1, "MAX": 20.0, "DEFAULT": 2.5},
        {"NAME": "erosionRate",      "LABEL": "Depth Fade",      "TYPE": "float",  "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.6},
        {"NAME": "orbitSpeed",       "LABEL": "Orbit Speed",     "TYPE": "float",  "MIN": 0.0, "MAX": 2.0,  "DEFAULT": 0.3},
        {"NAME": "lightAzimuth",     "LABEL": "Light Azimuth",   "TYPE": "float",  "MIN": 0.0, "MAX": 360.0,"DEFAULT": 45.0},
        {"NAME": "eruptionThreshold","LABEL": "Eruption Thresh", "TYPE": "float",  "MIN": 0.3, "MAX": 1.0,  "DEFAULT": 0.75},
        {"NAME": "showWireframe",    "LABEL": "Wireframe",       "TYPE": "bool",   "DEFAULT": false},
        {"NAME": "BgOpacity",        "LABEL": "Sky Opacity",     "TYPE": "float",  "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 1.0},
        {"NAME": "GlowAmt",          "LABEL": "Eruption Glow",   "TYPE": "float",  "MIN": 0.0, "MAX": 3.0,  "DEFAULT": 0.8},
        {"NAME": "Relief",           "LABEL": "Fine Relief",     "TYPE": "float",  "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.35},
        {"NAME": "Exposure",         "LABEL": "Exposure",        "TYPE": "float",  "MIN": 0.1, "MAX": 4.0,  "DEFAULT": 1.0},
        {"NAME": "VignetteAmt",      "LABEL": "Vignette",        "TYPE": "float",  "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.3}
    ]
}*/

// ISF packing: TerrainTint at 0..3, EruptionColor at 4..7, then twelve scalars at
// offsets 8..19. 20/32 floats used.

// Transparency: the alpha channel of Terrain Tint is the terrain's own opacity and the
// alpha of Eruption Colour the eruption's; Sky Opacity covers the background. Values
// below 1 only reveal anything when a Video Blend mode other than Off is selected:
// the compositor multiplies the blend by this shader's alpha.

// Psychoacoustic topography: FFT as terrain surface.
// The spectrum texture provides a 1D height profile; this shader renders it as
// a 3D mountain range via a scanline perspective projection.  Each depth layer
// projects the spectrum sample at the world-space X position to a screen Y; layers
// are composited back-to-front with analytic edge coverage.  The camera orbits
// on a horizontal arc driven by orbitSpeed so the terrain continuously rotates
// into view.  Transients above eruptionThreshold trigger pyroclastic amplification.
// Bass deepens valley floors; treble sharpens ridgelines.

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

// Height profile at a normalised frequency, plus a fine relief octave.
// The spectrum is 256 linearly-interpolated bins, so the raw profile is a smooth
// curve and the ridgeline has no texture of its own at any zoom. The added ripple
// is scaled by the profile itself (quiet bands stay flat) and band-limited through
// spBandLimitedCos against the per-pixel change in freq: at the far depth layers a
// screen pixel spans several whole periods, and an unfiltered sin() there would
// alias into a moire fence across the horizon.
float sampleTerrain(float freq, float dfreq) {
    float h = spectrumTexture.SampleLevel(videoSampler, float2(saturate(frac(freq)), 0.5), 0).r;
    float ripple = spBandLimitedCos(frac(freq) * 137.0, dfreq * 137.0) * 0.5 + 0.5;
    return h * (1.0 + Relief * (ripple - 0.5) * 0.9);
}

// Full world height for a frequency: profile, audio shaping, then eruption gain.
// `excess` reports how far past the eruption threshold this column sits, as a smooth
// quantity rather than the bool it used to be; a hard test put a one-pixel colour
// tear down the side of every erupting peak.
float terrainHeight(float freq, float dfreq, out float excess) {
    float wy = sampleTerrain(freq, dfreq) * heightScale;

    // Bass raises the whole profile.
    wy *= (1.0 + bassLevel * 1.8);

    // Treble sharpens the upper half of the spectrum. Ramped over a band instead of
    // switching at freq == 0.6, which was a visible vertical seam in the terrain.
    wy *= 1.0 + highLevel * 3.5 * smoothstep(0.55, 0.68, frac(freq));

    excess = max(wy / max(heightScale, 0.001) - eruptionThreshold, 0.0);
    wy *= 1.0 + excess * 3.0;
    return wy;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float  ar  = resolution.x / resolution.y;

    // Horizon sits on the bottom edge: terrain is anchored to the bottom of the frame
    // and rises from there, so peaks can scale all the way to the top rather than being
    // confined to the lower half.
    float horizY    = 1.0;
    float fovFactor = 0.35;    // perspective field-of-view scale
    float orbitAng  = time * orbitSpeed * 0.07;

    // Sky gradient fills everything the terrain does not cover. Linear light from here
    // down: the haze lerp below mixes terrain into sky every layer, and doing that on
    // sRGB values darkens the mix at every crossover.
    float skyT = saturate(1.0 - uv.y);
    float3 col = lerp(spSrgbToLinear(float3(0.02, 0.02, 0.06)),
                      spSrgbToLinear(float3(0.06, 0.04, 0.12)), skyT);
    float  alpha = BgOpacity;
    float3 glow  = 0.0.xxx;

    float3 tintLin    = spSrgbToLinear(TerrainTint.rgb);
    float3 eruptLin   = spSrgbToLinear(EruptionColor.rgb);
    float3 snowCap    = spSrgbToLinear(float3(0.92, 0.96, 1.00));
    float  pxY        = 1.0 / max(resolution.y, 1.0);

    int numLayers = meshResolution;  // 16..64

    // Iterate depth layers from far to near (layer 0 = far, N-1 = near)
    [loop] for (int layer = 0; layer < 64; layer++) {
        if (layer >= numLayers) break;

        float t    = float(layer) / float(numLayers - 1);  // 0=far, 1=near
        float wz   = lerp(9.0, 0.25, t);                   // world depth

        // World X for this pixel column at this depth, and how much it advances per
        // screen pixel; the whole footprint calculation below hangs off this.
        float wx     = (uv.x - 0.5) * wz / fovFactor * ar;
        float dwxdx  = (wz / fovFactor * ar) / max(resolution.x, 1.0);
        float dfreq  = 0.18 * dwxdx;

        float freq = wx * 0.18 + orbitAng;

        // Centre sample plus two neighbours two screen pixels either side. Sampling at
        // a fixed world offset (as this did) collapses to a sub-pixel difference on the
        // far layers, where it produced a normal built from numerical noise.
        float exC, exL, exR;
        float wy  = terrainHeight(freq,                dfreq, exC);
        float wyL = terrainHeight(freq - dfreq * 2.0,  dfreq, exL);
        float wyR = terrainHeight(freq + dfreq * 2.0,  dfreq, exR);

        // Project terrain top to screen Y.
        float sy_top = horizY - wy / wz * fovFactor;

        // Analytic edge footprint. fwidth is not available here: this is a loop with a
        // varying trip count, and sy_top is loop-carried, so the derivative would be
        // undefined. The footprint is instead one pixel of height plus the ridge's own
        // screen-space slope, which is what makes a near-horizontal ridge stay a clean
        // line while a far, near-vertical one dissolves into its own average rather
        // than crawling.
        float dsydx = (wyR - wyL) * 0.25 / wz * fovFactor;
        float w     = 0.6 * pxY + 0.6 * abs(dsydx);

        // Eruption plume glow, accumulated whether or not this layer covers the pixel:
        // inverse distance from the ridgeline in pixels, so the plume spills upward
        // into the sky instead of stopping dead at the silhouette.
        if (GlowAmt > 0.001 && exC > 0.0) {
            float dRidgePx = abs(uv.y - sy_top) * resolution.y;
            glow += eruptLin * exC / (1.0 + dRidgePx * 0.25) * (0.25 + t * 0.75);
        }

        // Coverage of this pixel by this layer, antialiased across the footprint.
        float cov = smoothstep(sy_top - w, sy_top + w, uv.y) * step(uv.y, horizY);
        if (cov < 0.002) continue;

        // --- Lighting ---
        float3 nrm = normalize(float3(wyL - wyR, 0.04 / wz, 1.0));

        float lightAzRad = radians(lightAzimuth);
        float3 lightDir  = normalize(float3(cos(lightAzRad), 0.6, sin(lightAzRad)));
        float  diffuse   = saturate(dot(nrm, lightDir)) * 0.75 + 0.25;

        // Depth-based colour: far = cool blue-grey, near = warm teal
        float depthT  = 1.0 - t;  // 0=near, 1=far
        float heightN = wy / max(heightScale * 1.2, 0.001);

        float3 rockCol  = tintLin;
        float3 deepCol  = tintLin * float3(0.29, 0.43, 0.73);
        float3 terrainBase = lerp(deepCol, rockCol, saturate(heightN * 2.0));
        terrainBase = lerp(terrainBase, snowCap, saturate((heightN - 0.7) * 4.0));

        // Atmospheric haze: fade to whatever is already behind, with depth.
        terrainBase = lerp(terrainBase, col, depthT * erosionRate * 0.8);
        float3 litCol = terrainBase * diffuse;

        float layerAlpha = TerrainTint.a;

        // Eruption: flash in the selected colour, ramped over the threshold.
        float eruptMix = saturate(exC * 6.0) * (sin(time * 15.0) * 0.3 + 0.7);
        litCol     = lerp(litCol, eruptLin, eruptMix * 0.7);
        layerAlpha = lerp(layerAlpha, EruptionColor.a, eruptMix * 0.7);

        // Wireframe: bright ridge line at terrain top, band-limited to the same
        // footprint as the silhouette so it does not break into dashes on steep ground.
        if (showWireframe) {
            float ridgeD = abs(uv.y - sy_top);
            float wire   = 1.0 - smoothstep(0.0, max(1.5 * pxY, w), ridgeD);
            litCol     += spSrgbToLinear(float3(0.3, 1.0, 0.5)) * wire * 1.6;
            layerAlpha  = max(layerAlpha, wire * 0.85);
        }

        // Composite over what is already there, weighted by coverage. The old hard
        // overwrite is what made every ridgeline a staircase.
        col   = lerp(col,   litCol,     cov);
        alpha = lerp(alpha, layerAlpha, cov);
    }

    col += glow * GlowAmt;
    alpha = saturate(alpha + spLuma(glow * GlowAmt) * EruptionColor.a);

    // Scanline shimmer from treble energy. Scaled by alpha so it does not paint colour
    // into regions the user has made transparent.
    col += highLevel * 0.03 * sin(uv.x * 200.0 + time * 10.0) * alpha;

    col *= spVignette(uv, VignetteAmt, 0.75);
    col *= Exposure;

    // tanh: the plume glow is inverse-distance and unbounded.
    col = spLinearToSrgb(spTonemapTanh(col));

    // The sky gradient and the haze ramp are both broad and low-contrast.
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), saturate(alpha));
}
