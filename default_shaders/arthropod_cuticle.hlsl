/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        {"NAME": "pigmentColour",  "LABEL": "Pigment Colour",  "TYPE": "color",  "DEFAULT": [0.82, 0.38, 0.08, 1.0]},
        {"NAME": "activatorDiff",  "LABEL": "Activator Diff",  "TYPE": "float",  "MIN": 0.01, "MAX": 1.0,  "DEFAULT": 0.12},
        {"NAME": "inhibitorDiff",  "LABEL": "Inhibitor Diff",  "TYPE": "float",  "MIN": 0.01, "MAX": 1.0,  "DEFAULT": 0.6},
        {"NAME": "activationRate", "LABEL": "Activation Rate", "TYPE": "float",  "MIN": 0.1,  "MAX": 4.0,  "DEFAULT": 1.8},
        {"NAME": "inhibitionRate", "LABEL": "Inhibition Rate", "TYPE": "float",  "MIN": 0.1,  "MAX": 4.0,  "DEFAULT": 1.2},
        {"NAME": "bodyGeometry",   "LABEL": "Body Geometry",   "TYPE": "long",
         "VALUES": [0,1,2,3], "LABELS": ["Cylinder","Carapace","Wing","Abdomen"], "DEFAULT": 1},
        {"NAME": "segmentCount",   "LABEL": "Segments",        "TYPE": "long",
         "VALUES": [2,3,4,5,6,8,10,12], "LABELS": ["2","3","4","5","6","8","10","12"], "DEFAULT": 6},
        {"NAME": "bgColour",       "LABEL": "Background",      "TYPE": "color", "DEFAULT": [0.92, 0.88, 0.80, 1.0]}
    ]
}*/

// Meinhardt-Gierer activator-inhibitor cuticle patterning.
// The Turing instability is approximated by computing two spatially correlated
// noise samples at wavelengths proportional to activatorDiff and inhibitorDiff
// respectively.  Their scaled difference produces the activator concentration
// field: when activationRate/inhibitionRate ratio is high the pattern is spotted
// (Fm3m / P4/mmm class); when near 1 it is labyrinthine / striped; when low it
// inverts to dark spots on pigment background (matching empirical crustacean
// and lepidopteran morphological series).  bodyGeometry distorts the UV map
// to match each arthropod body plan.

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
    float4 custom[4];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// UV distortion for each body plan
float2 applyGeometry(float2 uv, int geom, int segs) {
    float segW = 1.0 / float(segs);
    if (geom == 0) {
        // Cylinder: u wraps per segment (periodic cuticle rings)
        return float2(frac(uv.x / segW), uv.y);
    } else if (geom == 1) {
        // Carapace: segment rows offset alternately, bilateral along v
        float row = floor(uv.y * float(segs));
        float offset = (fmod(row, 2.0) > 0.5) ? segW * 0.5 : 0.0;
        return float2(frac((uv.x + offset) / segW), uv.y);
    } else if (geom == 2) {
        // Wing: bilateral symmetry + radial coordinates
        float2 c = uv - 0.5;
        float r = length(c);
        float a = atan2(c.y, c.x) / (3.14159 * 2.0) + 0.5;
        return float2(abs(a - 0.5) * 2.0 * float(segs), r * 2.0);
    } else {
        // Abdomen: strong horizontal segmentation bands
        float band = frac(uv.y * float(segs));
        float banded = float2(uv.x + floor(uv.y * float(segs)) * 0.15, band).x;
        return float2(frac(banded), band);
    }
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Apply body geometry
    float2 gUV = applyGeometry(uv, bodyGeometry, segmentCount);

    // Two-scale noise sampling for activator and inhibitor
    // Shorter wavelength = smaller diff constant (tighter sampling)
    float actFreq  = 1.0 / max(activatorDiff,  0.001) * 0.25;
    float inhFreq  = 1.0 / max(inhibitorDiff,   0.001) * 0.25;

    // Slow temporal drift to avoid perfectly static pattern
    float2 drift = float2(time * 0.007, time * 0.005);

    float actVal = noiseTexture.SampleLevel(noiseSampler, frac(gUV * actFreq * 0.1 + drift),       0).r;
    float inhVal = noiseTexture.SampleLevel(noiseSampler, frac(gUV * inhFreq * 0.1 + drift * 0.5), 0).r;

    // Activator-inhibitor: net concentration
    float conc = actVal * activationRate - inhVal * inhibitionRate;

    // Threshold to produce discrete pigmented / unpigmented regions
    float threshLow  =  0.0;
    float threshHigh =  0.1;
    float pigment    = smoothstep(threshLow, threshHigh, conc);

    float3 col = lerp(bgColour.rgb, pigmentColour.rgb, pigment);

    // Add subtle specular sheen (chitin iridescence)
    float2 normDir = gUV * 2.0 - 1.0;
    float specular = pow(max(0.0, dot(normDir, normalize(float2(0.5, -0.5)))), 6.0) * 0.15;
    col += float3(0.9, 0.95, 1.0) * specular;

    return float4(saturate(col), 1.0);
}
