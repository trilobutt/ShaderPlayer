/*{
    "SHADER_TYPE": "audio",
    "INPUTS": [
        {"NAME": "bassLevel",       "LABEL": "Bass",           "TYPE": "audio",  "BAND": "bass"},
        {"NAME": "midLevel",        "LABEL": "Mid",            "TYPE": "audio",  "BAND": "mid"},
        {"NAME": "symmetryOrder",   "LABEL": "Symmetry",       "TYPE": "long",
         "VALUES": [2,3,4,6,8,12], "LABELS": ["2-fold","3-fold","4-fold","6-fold (hex)","8-fold","12-fold"], "DEFAULT": 6},
        {"NAME": "nucleationRate",  "LABEL": "Nucleation Rate","TYPE": "float",  "MIN": 0.1, "MAX": 4.0,  "DEFAULT": 1.5},
        {"NAME": "growthSpeed",     "LABEL": "Growth Speed",   "TYPE": "float",  "MIN": 0.1, "MAX": 3.0,  "DEFAULT": 1.0},
        {"NAME": "maxRadius",       "LABEL": "Max Radius",     "TYPE": "float",  "MIN": 0.1, "MAX": 1.0,  "DEFAULT": 0.45},
        {"NAME": "latticeSaturation","LABEL": "Saturation",    "TYPE": "float",  "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.8},
        {"NAME": "displaciveNoise", "LABEL": "Displace Noise", "TYPE": "float",  "MIN": 0.0, "MAX": 0.5,  "DEFAULT": 0.08},
        {"NAME": "CrystalTint",     "LABEL": "Crystal Tint",   "TYPE": "color",  "DEFAULT": [0.5, 0.8, 1.0, 1.0]},
        {"NAME": "PaletteShift",    "LABEL": "Palette Shift",  "TYPE": "float",  "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.0},
        {"NAME": "GlowAmount",      "LABEL": "Facet Glow",     "TYPE": "float",  "MIN": 0.0, "MAX": 3.0,  "DEFAULT": 0.8},
        {"NAME": "Exposure",        "LABEL": "Exposure",       "TYPE": "float",  "MIN": 0.1, "MAX": 4.0,  "DEFAULT": 1.0},
        {"NAME": "VignetteAmt",     "LABEL": "Vignette",       "TYPE": "float",  "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.3}
    ]
}*/

// Fourier crystal growth: audio-driven N-fold crystallographic patterning.
// Crystals nucleate at seed points and grow outward.  The crystal lattice
// symmetry is enforced by folding polar coordinates into a fundamental domain
// (one sector of 2π/N radians, then mirror) before generating the lattice SDF.
// Bass amplitude drives the crystal growth radius; mid frequency modulates the
// lattice spacing (simulating elastic distortion under acoustic load).
// displaciveNoise adds phason-like random displacement to the lattice vertices,
// replicating the appearance of displacively disordered quasi-crystals.
//
// The lattice edge footprint is supplied analytically, never by fwidth. The N-fold
// fold is discontinuous at every sector boundary and at the mirror line, so a
// screen-space derivative of anything downstream of it reports those seams as
// infinitely wide pixels and paints a grey star over the crystal. The fold itself is
// a rotation plus a reflection, both isometries, so the metric survives it intact and
// one output pixel is exactly 1/resolution.y in the folded space too.
//
// Not done: temporal smoothing of the band values. The bands do jitter frame to
// frame, but smoothing needs state that survives a frame and there is no
// previous-frame texture in this pipeline. The growth radius integrates time instead,
// which absorbs most of the jitter where it would be most visible.

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

float h21(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

// Cold mineral ramp: deep indigo through cyan to a pale hot white. Replaces a full
// HSV wheel, which put an arbitrary rainbow across a structure whose colour should
// read as one material catching light at different angles.
float3 crystalPalette(float t) {
    return max(spPalette(t + PaletteShift,
                         float3(0.46, 0.50, 0.56),
                         float3(0.40, 0.42, 0.44),
                         float3(1.0,  1.0,  1.0),
                         float3(0.62, 0.44, 0.22)), 0.0);
}

// Apply N-fold dihedral symmetry: fold angle into fundamental domain [0, π/N]
float2 nFold(float2 p, int N) {
    float ang = atan2(p.y, p.x);
    float r   = length(p);
    float sector = 3.14159265 * 2.0 / float(N);
    ang = fmod(ang + 3.14159265 * 2.0, sector);
    if (ang > sector * 0.5) ang = sector - ang;
    return float2(cos(ang), sin(ang)) * r;
}

// 2D hexagonal (triangular) lattice SDF: distance to nearest lattice vertex
float hexLatticeSDF(float2 p, float scale) {
    // Hexagonal basis vectors
    float2 a1 = float2(scale, 0.0);
    float2 a2 = float2(scale * 0.5, scale * 0.866025);
    // Express p in fractional lattice coordinates
    float det = a1.x * a2.y - a1.y * a2.x;
    float u   = (p.x * a2.y - p.y * a2.x) / det;
    float v   = (p.y * a1.x - p.x * a1.y) / det;
    float2 c0 = floor(float2(u, v));
    // A 2x2 candidate block is not enough on a sheared basis: the true nearest
    // vertex can sit in the cell behind, so the returned distance jumped by up to a
    // full lattice spacing along a set of straight lines through the crystal.
    float  minD = 1e9;
    [unroll] for (int iy = -1; iy <= 1; iy++) {
        [unroll] for (int ix = -1; ix <= 1; ix++) {
            float2 ci = c0 + float2(ix, iy);
            float2 lp = ci.x * a1 + ci.y * a2;
            minD = min(minD, length(p - lp));
        }
    }
    return minD;
}

// Square lattice SDF (for 4-fold, 8-fold)
float sqLatticeSDF(float2 p, float scale) {
    float2 fp = frac(p / scale + 0.5) - 0.5;
    return length(fp) * scale;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float  ar  = resolution.x / resolution.y;
    float2 p   = (uv - 0.5) * float2(ar, 1.0);

    // One output pixel in p units, and therefore in folded-lattice units too.
    float px = 1.0 / max(resolution.y, 1.0);

    // Audio-driven parameters, boosted for strong reactivity
    float audioRadius  = bassLevel   * 1.1 + midLevel * 0.45;
    float latticeScale = 0.04 + midLevel * 0.04;   // spacing modulated by mid

    // Crystal growth radius: ramps up once and then holds at maxRadius, pulsed by bass.
    float growthPhase = saturate(time * growthSpeed * 0.1) * maxRadius;
    float crystalR    = growthPhase + audioRadius * 0.15;

    // Displacive noise perturbation of the sampling position
    float2 noiseDisp = (noiseTexture.SampleLevel(noiseSampler,
        uv * 4.0 + float2(time * 0.02, 0), 0).rg * 2.0 - 1.0) * displaciveNoise * 0.1;

    float3 tintLin = spSrgbToLinear(CrystalTint.rgb);

    // Melt: a very dark medium with a slow drift, so the crystals grow in something.
    float melt = noiseTexture.SampleLevel(noiseSampler, p * 1.7 + float2(time * 0.01, 0), 0).r;
    float3 col = float3(0.002, 0.004, 0.012) * (0.5 + melt * 1.5);

    // Multiple nucleation sites
    int numNuclei = max(1, int(nucleationRate * 3.0));
    float golden = 0.618034;

    [loop] for (int ni = 0; ni < 9; ni++) {
        if (ni >= numNuclei) break;

        // Nucleation position (slowly drifting)
        float phi = frac(float(ni) * golden);
        float2 nucleus = float2(
            cos(phi * 6.28318 + time * 0.05) * 0.3,
            sin(phi * 6.28318 + time * 0.04) * 0.25
        ) * float2(ar, 1.0);

        // Coordinate relative to this nucleus
        float2 rp = p - nucleus + noiseDisp;

        // Radial envelope: crystal has grown to crystalR from this nucleus
        float rad = length(rp);
        if (rad > (crystalR + 0.05) * ar) continue;   // cull in the same units the envelope uses

        float envelope = smoothstep(crystalR + 0.05, crystalR - 0.05, rad / ar);
        if (envelope < 0.001) continue;

        // Apply N-fold symmetry
        float2 fp = nFold(rp, symmetryOrder);

        // Lattice SDF in the symmetric space
        float lSDF;
        if (symmetryOrder == 4 || symmetryOrder == 8 || symmetryOrder == 2) {
            lSDF = sqLatticeSDF(fp, latticeScale);
        } else {
            lSDF = hexLatticeSDF(fp, latticeScale);
        }

        // Crystal facets: narrow ridges at lattice vertex distances, filtered over
        // the analytic pixel footprint so they hold their width as the lattice
        // spacing shrinks under a loud mid band.
        float facetR = latticeScale * 0.3;
        float facet  = 1.0 - smoothstep(facetR - px, facetR + px, lSDF);

        // Diffraction rings between the vertices: a second, finer scale of structure
        // over what would otherwise be flat gaps. Band-limited, so once the ring
        // spacing drops below a pixel they average to flat instead of aliasing.
        float ringPhase = SP_TAU * lSDF / max(latticeScale, 1e-4) * 3.0;
        float ringW     = SP_TAU * 3.0 / max(latticeScale, 1e-4) * px;
        float ring      = 0.5 + 0.5 * spBandLimitedCos(ringPhase, ringW);

        // Colour: hue from angle within the crystal, saturation from audio
        float ang0 = atan2(fp.y, fp.x) / (3.14159 * 2.0) + 0.5;
        float hue  = frac(ang0 + float(ni) * golden + time * 0.04);
        float sat  = latticeSaturation * (0.4 + bassLevel * 1.1);
        float3 base = lerp(float3(1.0, 1.0, 1.0), crystalPalette(hue), saturate(sat));

        float bri = (facet * (0.6 + 0.4 * ring) + ring * 0.06)
                  * envelope * (0.3 + midLevel * 1.8) * (1.0 + bassLevel * 1.5);

        // Inverse-distance core at each lattice vertex. A vertex is a point source;
        // bounding it with a smoothstep capped every one of them at the same
        // brightness, which is why the lattice read as printed dots rather than light.
        float core = GlowAmount * 0.10 * facetR / max(lSDF, facetR * 0.3) * envelope;

        col += (base * bri + lerp(base, float3(1.0, 1.0, 1.0), 0.5) * core)
             * lerp(float3(1.0, 1.0, 1.0), tintLin * 2.0, 0.5);
    }

    // Spectrum halo: ring glow at current growth radius coloured by spectrum bin
    float  rr       = length(p) / ar;
    float  specFreq = frac(rr / maxRadius * 0.5 + time * growthSpeed * 0.02);
    float  specVal  = spectrumTexture.Sample(videoSampler, float2(specFreq, 0.5)).r;
    float  haloW    = 0.05;
    float  haloR    = specVal * (1.0 + bassLevel * 2.0) * haloW / max(abs(rr - crystalR), haloW * 0.3);
    col += crystalPalette(frac(atan2(p.y, p.x) / 6.28318 + 0.5)) * haloR * 0.12 * GlowAmount;

    col *= Exposure;
    col *= spVignette(uv, VignetteAmt, 0.85);

    // tanh: the vertex cores and the halo are unbounded, and it keeps the tint through
    // the hot centres where ACES would push them to white.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
