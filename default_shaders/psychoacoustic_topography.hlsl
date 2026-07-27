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
        {"NAME": "BgOpacity",        "LABEL": "Sky Opacity",     "TYPE": "float",  "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 1.0}
    ]
}*/

// Transparency: the alpha channel of Terrain Tint is the terrain's own opacity and the
// alpha of Eruption Colour the eruption's; Sky Opacity covers the background. Values
// below 1 only reveal anything when a Video Blend mode other than Off is selected —
// the compositor multiplies the blend by this shader's alpha.

// Psychoacoustic topography: FFT as terrain surface.
// The spectrum texture provides a 1D height profile; this shader renders it as
// a 3D mountain range via a scanline perspective projection.  Each depth layer
// projects the spectrum sample at the world-space X position to a screen Y; the
// nearest layer that covers a given pixel sets its colour.  The camera orbits
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

float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float sampleTerrain(float freq) {
    return spectrumTexture.SampleLevel(videoSampler, float2(saturate(freq), 0.5), 0).r;
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

    // Sky gradient fills everything the terrain does not cover.
    float skyT = saturate(1.0 - uv.y);
    float3 col = lerp(float3(0.02, 0.02, 0.06), float3(0.06, 0.04, 0.12), skyT);
    float  alpha = BgOpacity;

    int numLayers = meshResolution;  // 16..64

    // Iterate depth layers from far to near (layer 0 = far, N-1 = near)
    [loop] for (int layer = 0; layer < 64; layer++) {
        if (layer >= numLayers) break;

        float t    = float(layer) / float(numLayers - 1);  // 0=far, 1=near
        float wz   = lerp(9.0, 0.25, t);                   // world depth

        // World X for this pixel column at this depth
        float wx   = (uv.x - 0.5) * wz / fovFactor * ar;

        // Map world X to spectrum frequency bin via orbit
        float freq = frac(wx * 0.18 + orbitAng);

        // Sample terrain height at this frequency
        float wy   = sampleTerrain(freq) * heightScale;

        // Bass deepens/raises the terrain valley floor
        wy *= (1.0 + bassLevel * 1.8);

        // Treble sharpens peaks
        if (freq > 0.6) wy *= (1.0 + highLevel * 3.5);

        // Eruption: spikes above threshold get amplified and flash
        bool erupting = wy / max(heightScale, 0.001) > eruptionThreshold;
        if (erupting) {
            float excess = (wy / max(heightScale, 0.001)) - eruptionThreshold;
            wy *= 1.0 + excess * 3.0;
        }

        // Project terrain top to screen Y
        float sy_top = horizY - wy / wz * fovFactor;

        // Pixel in this terrain layer?
        bool inTerrain = (uv.y >= sy_top) && (uv.y < horizY);
        if (!inTerrain) continue;

        // --- Lighting ---
        // Terrain normal from horizontal gradient
        float freqL = frac((wx - 0.003) * 0.18 + orbitAng);
        float freqR = frac((wx + 0.003) * 0.18 + orbitAng);
        float wyL   = sampleTerrain(freqL) * heightScale * (1.0 + bassLevel * 1.8);
        float wyR   = sampleTerrain(freqR) * heightScale * (1.0 + bassLevel * 1.8);
        float3 nrm  = normalize(float3(wyL - wyR, 0.04 / wz, 1.0));

        float lightAzRad = radians(lightAzimuth);
        float3 lightDir  = normalize(float3(cos(lightAzRad), 0.6, sin(lightAzRad)));
        float  diffuse   = saturate(dot(nrm, lightDir)) * 0.75 + 0.25;

        // Depth-based colour: far = cool blue-grey, near = warm teal
        float depthT = 1.0 - t;  // 0=near, 1=far
        float heightN = wy / max(heightScale * 1.2, 0.001);

        float3 snowCap  = float3(0.92, 0.96, 1.00);
        float3 rockCol  = TerrainTint.rgb;
        float3 deepCol  = TerrainTint.rgb * float3(0.29, 0.43, 0.73);
        float3 terrainBase = lerp(deepCol, rockCol, saturate(heightN * 2.0));
        terrainBase = lerp(terrainBase, snowCap, saturate((heightN - 0.7) * 4.0));

        // Atmospheric haze: fade to background with depth
        terrainBase = lerp(terrainBase, col, depthT * erosionRate * 0.8);
        float3 litCol = terrainBase * diffuse;

        float layerAlpha = TerrainTint.a;

        // Eruption: flash in the selected colour
        if (erupting) {
            float flash = sin(time * 15.0) * 0.3 + 0.7;
            litCol     = lerp(litCol, EruptionColor.rgb, flash * 0.7);
            layerAlpha = lerp(layerAlpha, EruptionColor.a, flash * 0.7);
        }

        // Wireframe: bright ridge line at terrain top
        if (showWireframe) {
            float ridgePx = abs(uv.y - sy_top) * resolution.y;
            if (ridgePx < 1.5) {
                litCol     = float3(0.3, 1.0, 0.5);
                layerAlpha = max(layerAlpha, 0.85);
            }
        }

        col   = litCol;      // nearer layers overwrite (back-to-front)
        alpha = layerAlpha;
    }

    // Scanline shimmer from treble energy. Scaled by alpha so it does not paint colour
    // into regions the user has made transparent.
    float shimmer = highLevel * 0.03 * sin(uv.x * 200.0 + time * 10.0);
    col += shimmer * alpha;

    return float4(saturate(col), saturate(alpha));
}
