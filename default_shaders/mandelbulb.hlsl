/*{
    "DESCRIPTION": "Ray-marched 3D Mandelbulb, lit rather than tinted: a distance-field normal, one key light with soft shadows marched through the same estimator, four-tap ambient occlusion and a Fresnel rim. Every march step deposits energy inversely with its distance to the surface, so rays that graze the bulb without hitting it build a real halo around the silhouette and light the gaps between lobes. Bulb Power sets how many lobes the surface folds into.",
    "SHADER_TYPE": "generative",
    "INPUTS": [
        { "NAME": "BulbPower",   "LABEL": "Bulb Power",    "TYPE": "float", "DEFAULT": 8.0, "MIN": 2.0,  "MAX": 16.0, "STEP": 0.1 },
        { "NAME": "DEIter",      "LABEL": "DE Iterations", "TYPE": "long",
          "VALUES": [4,6,8,10,12,16], "LABELS": ["4","6","8","10","12","16"], "DEFAULT": 10  },
        { "NAME": "OrbitSpeed",  "LABEL": "Orbit Speed",   "TYPE": "float", "DEFAULT": 0.08,"MIN": 0.0,  "MAX": 1.0,  "STEP": 0.01 },
        { "NAME": "AudioAmount", "LABEL": "Audio Amount",  "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0,  "MAX": 1.0,  "STEP": 0.01 },
        { "NAME": "GlowColour",  "LABEL": "Glow Colour",   "TYPE": "color", "DEFAULT": [0.2, 0.5, 1.0, 1.0]                    },
        { "NAME": "PaletteShift","LABEL": "Palette Shift", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0,  "MAX": 1.0,  "STEP": 0.01 },
        { "NAME": "GlowAmount",  "LABEL": "Volumetric Glow","TYPE": "float","DEFAULT": 0.5, "MIN": 0.0,  "MAX": 3.0,  "STEP": 0.01 },
        { "NAME": "Exposure",    "LABEL": "Exposure",      "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.1,  "MAX": 4.0,  "STEP": 0.01 },
        { "NAME": "VignetteAmt", "LABEL": "Vignette",      "TYPE": "float", "DEFAULT": 0.3, "MIN": 0.0,  "MAX": 1.0,  "STEP": 0.01 },
        { "NAME": "BassIn",      "LABEL": "Bass",          "TYPE": "audio", "BAND": "bass" },
        { "NAME": "MidIn",       "LABEL": "Mid",           "TYPE": "audio", "BAND": "mid"  },
        { "NAME": "BeatIn",      "LABEL": "Beat",          "TYPE": "audio", "BAND": "beat" }
    ]
}*/

// The surface is lit rather than tinted: a distance-field normal, one key light with
// an IQ soft shadow marched through the same estimator, four-tap DE ambient occlusion
// and a Fresnel rim. Step-count "AO" was standing in for all of that, and it shades
// by how hard the march worked rather than by the shape, so grazing geometry went
// dark for no physical reason.
//
// Every march step also deposits energy inversely proportional to its distance from
// the surface. Rays that graze the bulb without hitting it accumulate the most, which
// is what puts a real halo around the silhouette and lights the lobe gaps, none of
// which a post-hit pow() ramp can produce.

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

// Mandelbulb distance estimator.
// Returns the estimated distance to the surface.
// trap receives the minimum orbit trap value (distance to origin) over all iterations.
float deMandelbulb(float3 pos, float pw, int maxIter, out float trap) {
    float3 zv = pos;
    float dr   = 1.0;
    float rLen = 0.0;
    trap = 1e10;

    [loop]
    for (int i = 0; i < maxIter; ++i) {
        rLen = length(zv);
        if (rLen > 2.0) break;

        // Orbit trap: smallest distance to origin
        trap = min(trap, rLen);

        // Convert to spherical coordinates in 3D
        float theta = acos(clamp(zv.z / rLen, -1.0, 1.0));
        float phi   = atan2(zv.y, zv.x);
        float zr    = pow(rLen, pw);

        // Derivative accumulation
        dr = pow(rLen, pw - 1.0) * pw * dr + 1.0;

        // Scale and rotate
        theta *= pw;
        phi   *= pw;

        // Back to cartesian + c = pos
        zv = zr * float3(
            sin(theta) * cos(phi),
            sin(theta) * sin(phi),
            cos(theta)
        ) + pos;
    }

    return 0.5 * log(rLen) * rLen / dr;
}

// Distance only, for the passes that do not care where the orbit went.
float mapDE(float3 pos, float pw, int maxIter) {
    float ignored;
    return deMandelbulb(pos, pw, maxIter, ignored);
}

// Tetrahedron normal: four DE evaluations rather than the six a central difference
// would need, for the same accuracy at this epsilon.
float3 calcNormal(float3 pos, float pw, int maxIter) {
    float2 e = float2(1.0, -1.0) * 0.0008;
    return normalize(e.xyy * mapDE(pos + e.xyy, pw, maxIter) +
                     e.yyx * mapDE(pos + e.yyx, pw, maxIter) +
                     e.yxy * mapDE(pos + e.yxy, pw, maxIter) +
                     e.xxx * mapDE(pos + e.xxx, pw, maxIter));
}

// IQ soft shadow. The smallest ratio of clearance to distance travelled along the
// shadow ray is the penumbra, so the whole soft shadow falls out of the same
// estimator that built the surface, with no second render target and no shadow map.
float softShadow(float3 origin, float3 dir, float pw, int maxIter, float k) {
    float res = 1.0;
    float t   = 0.02;
    [loop]
    for (int i = 0; i < 20; ++i) {
        float h = mapDE(origin + dir * t, pw, maxIter);
        res = min(res, k * h / t);
        if (res < 0.005 || t > 3.0) break;
        t += clamp(h, 0.01, 0.25);
    }
    return saturate(res);
}

// Four-tap DE occlusion: how much closer the surface is than a clear hemisphere
// would put it, sampled at increasing distance along the normal.
float calcAO(float3 pos, float3 n, float pw, int maxIter) {
    float occ = 0.0;
    float sca = 1.0;
    [unroll]
    for (int i = 1; i <= 4; ++i) {
        float h = 0.01 + 0.13 * float(i) * 0.25;
        occ += (h - mapDE(pos + n * h, pw, maxIter)) * sca;
        sca *= 0.72;
    }
    return saturate(1.0 - 2.5 * occ);
}

// Cool-to-warm cosine palette over the orbit trap. Replaces a full HSV sweep, which
// ran the whole spectrum across a trap range where most of the surface sits in one
// narrow band, so the bulb read as a single flat hue with a rainbow seam.
float3 bulbPalette(float t) {
    return max(spPalette(t + PaletteShift,
                         float3(0.50, 0.44, 0.42),
                         float3(0.45, 0.42, 0.45),
                         float3(1.0,  1.0,  1.0),
                         float3(0.00, 0.18, 0.42)), 0.0);
}

float4 main(PS_INPUT input) : SV_TARGET {
    // --- Audio modulation ---
    // Bass warps the bulb exponent (the lobes fold and unfold), mid drives the camera
    // orbit, beats pull the camera in and flare the glow. AudioAmount 0 = static bulb.
    float aBass = BassIn * AudioAmount;
    float aMid  = MidIn  * AudioAmount;
    float aBeat = BeatIn * AudioAmount;

    float   bulbPow   = clamp(BulbPower + aBass * 5.0, 2.0, 20.0);
    int     deIterVal = DEIter;
    float   orbitSpd  = OrbitSpeed * (1.0 + aMid * 2.5);
    float3  glowLin   = spSrgbToLinear(GlowColour.rgb);

    // Orbiting camera: slow azimuth + gentle elevation bob; beats dolly in.
    float camAngle  = time * orbitSpd;
    float camHeight = sin(time * orbitSpd * 0.3) * 0.4;
    float camDist   = 2.4 - aBeat * 0.55;
    float3 camPos   = float3(sin(camAngle) * camDist, camHeight, cos(camAngle) * camDist);
    float3 target   = float3(0.0, 0.0, 0.0);

    float3 fw = normalize(target - camPos);
    float3 rt = normalize(cross(float3(0.0, 1.0, 0.0), fw));
    float3 upV = cross(fw, rt);

    float2 screenUV = (input.uv - 0.5) * float2(resolution.x / resolution.y, 1.0);
    float3 rayDir   = normalize(screenUV.x * rt + screenUV.y * upV + fw * 1.4);

    float  tRay     = 0.0;
    float  trap     = 1e10;
    float  glowAcc  = 0.0;
    float  glowTrap = 0.0;   // trap value weighted by the energy each step deposited
    bool   hitBulb  = false;

    [loop]
    for (int s = 0; s < 80; ++s) {
        float3 pos = camPos + tRay * rayDir;
        float trapVal;
        float distEst = deMandelbulb(pos, bulbPow, deIterVal, trapVal);
        trap = min(trap, trapVal);

        // Volumetric deposit, attenuated with depth so the far side of the bulb does
        // not out-glow the near side. Unbounded by design; tanh rolls it off.
        float dep = exp(-tRay * 0.4) * 0.012 / (distEst + 0.014);
        glowAcc  += dep;
        glowTrap += dep * trapVal;

        if (distEst < 0.0003) { hitBulb = true; break; }
        if (tRay > 8.0) break;

        tRay += distEst * 0.7;
    }

    // Deep space background: a faint vertical gradient, present under the bulb too so
    // the silhouette sits in something rather than on flat black.
    float3 col = float3(0.006, 0.010, 0.024) + max(rayDir.y, 0.0) * 0.016;

    if (hitBulb) {
        float3 hitPos = camPos + tRay * rayDir;
        float3 nrm    = calcNormal(hitPos, bulbPow, deIterVal);

        // Key light fixed in world space so the orbiting camera reveals form.
        float3 lightDir = normalize(float3(0.55, 0.75, -0.35));
        float  sh   = softShadow(hitPos + nrm * 0.0025, lightDir, bulbPow, deIterVal, 12.0);
        float  ao   = calcAO(hitPos, nrm, bulbPow, deIterVal);
        float  diff = saturate(dot(nrm, lightDir));
        float  fres = pow(1.0 - saturate(dot(nrm, -rayDir)), 4.0);
        float3 hvec = normalize(lightDir - rayDir);
        float  spec = pow(saturate(dot(nrm, hvec)), 40.0);

        float3 albedo = bulbPalette(frac(trap * 2.0 + time * 0.03)) * glowLin;

        col  = albedo * (0.05 + 1.15 * diff * sh) * ao;          // key
        col += albedo * 0.22 * ao * (0.5 + 0.5 * nrm.y);         // sky fill
        col += glowLin * spec * sh * 1.4;                        // highlight
        col += glowLin * fres * 0.4 * ao;                        // rim
    }

    // Volumetric halo, coloured by the average trap along the ray so the glow shares
    // the surface's palette instead of being a flat tint over it.
    float3 haloCol = bulbPalette(frac(glowTrap / max(glowAcc, 1e-5) * 2.0 + time * 0.03));
    col += lerp(glowLin, haloCol * glowLin, 0.6) * glowAcc * GlowAmount * (1.0 + aBeat * 1.2);

    col *= Exposure;
    col *= spVignette(input.uv, VignetteAmt, 0.85);

    // tanh: the volumetric term is unbounded, and it holds the glow colour through
    // the hot core where ACES would push it to white.
    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
