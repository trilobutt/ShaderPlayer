/*{
    "SHADER_TYPE": "audio",
    "INPUTS": [
        {"NAME": "ampLevel",         "LABEL": "Amplitude",      "TYPE": "audio",  "BAND": "rms"},
        {"NAME": "nodeCount",        "LABEL": "Node Count",     "TYPE": "long",
         "VALUES": [8,16,24,32], "LABELS": ["8","16","24","32"], "DEFAULT": 16},
        {"NAME": "kNeighbours",      "LABEL": "k Neighbours",   "TYPE": "long",
         "VALUES": [2,4,6,8], "LABELS": ["2","4","6","8"], "DEFAULT": 4},
        {"NAME": "firingThreshold",  "LABEL": "Fire Threshold", "TYPE": "float",  "MIN": 0.1,  "MAX": 0.95, "DEFAULT": 0.6},
        {"NAME": "refractoryPeriod", "LABEL": "Refractory (s)", "TYPE": "float",  "MIN": 0.05, "MAX": 1.0,  "DEFAULT": 0.3},
        {"NAME": "propagationDelay", "LABEL": "Propagation (s)","TYPE": "float",  "MIN": 0.05, "MAX": 1.0,  "DEFAULT": 0.25},
        {"NAME": "spontaneousRate",  "LABEL": "Spontaneous Hz", "TYPE": "float",  "MIN": 0.1,  "MAX": 4.0,  "DEFAULT": 1.0}
    ]
}*/

// Synaptic fire network visualiser.
// nodeCount nodes are placed via a deterministic hash; each node connects to
// kNeighbours nearest neighbours (ring-based in sorted order to approximate
// Watts-Strogatz topology).  Nodes fire periodically at spontaneousRate Hz,
// additionally triggered when audio RMS exceeds firingThreshold.  An action
// potential pulse travels each axon over propagationDelay seconds, visible as
// a bright white bolus.  Nodes enter a dimmer refractory state for
// refractoryPeriod seconds after each spike.

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

float h21(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

// Segment SDF: point p to segment (a,b), returns min distance
float segmentSDF(float2 p, float2 a, float2 b) {
    float2 ab = b - a;
    float2 ap = p - a;
    float  t  = clamp(dot(ap, ab) / (dot(ab, ab) + 0.0001), 0.0, 1.0);
    return length(ap - ab * t);
}

// Parameterised projection along segment: returns t in [0,1]
float segmentT(float2 p, float2 a, float2 b) {
    float2 ab = b - a;
    float2 ap = p - a;
    return clamp(dot(ap, ab) / (dot(ab, ab) + 0.0001), 0.0, 1.0);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float  ar  = resolution.x / resolution.y;
    float2 p   = uv * float2(ar, 1.0);

    // --- Generate node positions (up to 32) ---
    float2 nodePos [32];
    float  nodeSeed[32];

    [unroll] for (int ni = 0; ni < 32; ni++) {
        float2 hv  = float2(h21(float2(float(ni), 7.3)), h21(float2(float(ni), 13.7)));
        // Use a quasi-regular layout: Halton-like spread, slightly jittered
        float phi  = frac(float(ni) * 0.618034);          // golden-ratio sequence
        float rad  = sqrt((float(ni) + 0.5) / 32.0) * 0.42;
        float ang  = phi * 6.28318;
        float2 base = float2(0.5, 0.5) + float2(cos(ang), sin(ang)) * rad;
        nodePos[ni]  = (base + (hv - 0.5) * 0.06) * float2(ar, 1.0);
        nodeSeed[ni] = h21(float2(float(ni) * 17.3, float(ni) * 5.7));
    }

    float3 col = float3(0.01, 0.01, 0.03);  // dark background

    int   halfK = kNeighbours / 2;
    float axonWidth = 0.003 * ar;

    // --- Draw axons and action potentials ---
    [loop] for (int i = 0; i < 32; i++) {
        if (i >= nodeCount) break;
        [loop] for (int k = 1; k <= 4; k++) {
            if (k > halfK) break;
            int j = (i + k) % nodeCount;

            float2 ni2 = nodePos[i];
            float2 nj  = nodePos[j];

            // Axon SDF
            float dist = segmentSDF(p, ni2, nj);
            float axon = smoothstep(axonWidth, 0.0, dist);
            if (axon < 0.001) continue;

            // --- Action potential pulse ---
            float period   = 1.0 / max(spontaneousRate, 0.01);
            // Node i fires when phase crosses firingThreshold; audio shortens cycle
            float audioBoost = ampLevel * 0.5;
            float phase    = frac(time / max(period - audioBoost * period * 0.6, 0.05) + nodeSeed[i]);
            bool  hasFired = phase > (1.0 - firingThreshold * 0.4);

            // Pulse travel: position along axon = (phase_since_fire) / propagationDelay
            float phaseSinceFire = frac(phase + (1.0 - firingThreshold * 0.4));
            float tPulse = phaseSinceFire / max(propagationDelay, 0.01) * period;
            float pulseT = clamp(tPulse, 0.0, 1.0);

            // Pixel's t along axon
            float pixelT = segmentT(p, ni2, nj);
            float pulseDist = abs(pixelT - pulseT);
            float pulseGlow = exp(-pulseDist * 40.0) * (hasFired ? 1.0 : 0.0);

            // Refractory dim: after pulse passes, axon is dimmer
            float refractPulse = exp(-max(pixelT - pulseT, 0.0) * 10.0) * (hasFired ? 0.4 : 0.1);

            // Axon base glow (faint persistent signal)
            float baseGlow = axon * (0.05 + refractPulse * 0.15);
            float3 axonCol = float3(0.2, 0.7, 0.3) * baseGlow;

            // Bright action potential bolus
            axonCol += float3(0.9, 1.0, 0.8) * pulseGlow * axon * 0.8;

            col += axonCol;
        }
    }

    // --- Draw nodes ---
    [loop] for (int ni3 = 0; ni3 < 32; ni3++) {
        if (ni3 >= nodeCount) break;
        float nodeDist = length(p - nodePos[ni3]) * resolution.y;
        float nodeR    = 6.0 + ampLevel * 4.0;
        float nodeMask = smoothstep(nodeR, 0.0, nodeDist);
        if (nodeMask < 0.001) continue;

        float period2   = 1.0 / max(spontaneousRate, 0.01);
        float audioB2   = ampLevel * 0.5;
        float phase2    = frac(time / max(period2 - audioB2 * period2 * 0.6, 0.05) + nodeSeed[ni3]);
        bool  firing    = phase2 > (1.0 - firingThreshold * 0.4);
        float refractory = max(0.0, phase2 - (1.0 - refractoryPeriod / max(period2, 0.01))) > 0.0 &&
                           phase2 > firingThreshold ? 0.3 : 1.0;

        float3 nodeCol = float3(0.4, 0.9, 0.5) * nodeMask * refractory;
        if (firing) nodeCol += float3(0.6, 0.3, 0.1) * nodeMask * 1.5;  // fire flash (warm)
        col += nodeCol;
    }

    return float4(saturate(col), 1.0);
}
