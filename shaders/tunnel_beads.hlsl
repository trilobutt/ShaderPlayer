/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        { "NAME": "Speed",          "LABEL": "Speed",          "TYPE": "float", "DEFAULT": 1.0,  "MIN": 0.0,   "MAX": 3.0,  "STEP": 0.05  },
        { "NAME": "Twist",          "LABEL": "Twist",          "TYPE": "float", "DEFAULT": 1.1,  "MIN": 0.0,   "MAX": 4.0,  "STEP": 0.05  },
        { "NAME": "Roughness",      "LABEL": "Surface Detail", "TYPE": "float", "DEFAULT": 0.06, "MIN": 0.0,   "MAX": 0.2,  "STEP": 0.005 },
        { "NAME": "BeadSize",       "LABEL": "Node Size",      "TYPE": "float", "DEFAULT": 0.03, "MIN": 0.005, "MAX": 0.15, "STEP": 0.005 },
        { "NAME": "GridDensity",    "LABEL": "Grid Density",   "TYPE": "float", "DEFAULT": 8.0,  "MIN": 2.0,   "MAX": 30.0, "STEP": 0.5   },
        { "NAME": "GridBrightness", "LABEL": "Grid Brightness","TYPE": "float", "DEFAULT": 0.35, "MIN": 0.0,   "MAX": 1.0,  "STEP": 0.05  },
        { "NAME": "Pulse",          "LABEL": "Energy Pulse",   "TYPE": "float", "DEFAULT": 0.6,  "MIN": 0.0,   "MAX": 2.0,  "STEP": 0.05  },
        { "NAME": "DataStreams",     "LABEL": "Data Streams",   "TYPE": "float", "DEFAULT": 0.4,  "MIN": 0.0,   "MAX": 1.0,  "STEP": 0.05  }
    ]
}*/

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

float2x2 rot(float a) {
    return float2x2(cos(a), cos(a + 11.0), cos(a + 33.0), cos(a));
}

float noiseVal(float2 p) {
    return sin(p.x * 3.0 + sin(p.y * 2.7)) * cos(p.y * 1.1 + cos(p.x * 2.3));
}

float fbm(float3 p) {
    float v = 0.0, a = 1.0;
    for (int i = 0; i < 7; i++) {
        v += noiseVal(p.xy + p.z * 0.5) * a;
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

float tunnelSdf(float3 p, float twist, float roughness, float spd) {
    p.xy = mul(p.xy, rot(p.z * twist));
    // Blend circular and hexagonal cross-section — gives a faceted corridor feel
    float2 ap  = abs(p.xy);
    float hexR = max(ap.x * 0.866 + ap.y * 0.5, ap.y);
    float r    = lerp(length(p.xy), hexR, 0.35);
    return 0.2 * (1.0 - r) - fbm(p + time * spd * 0.1) * roughness;
}

float4 nodeInfo(float z) {
    float i = floor((z + 2.5) * 0.2);
    return float4(cos(i * 2.4) * 0.6, sin(i * 2.4) * 0.6, i * 5.0, i);
}

// Cyber palette: electric blue / acid green / neon violet
float3 nodeColor(float i) {
    float h = frac(sin(i * 13.54) * 453.21);
    return h < 0.33 ? float3(0.0, 0.5, 1.2) : (h < 0.66 ? float3(0.2, 1.0, 0.4) : float3(0.9, 0.1, 1.3));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float spd      = Speed;
    float twist    = Twist;
    float rough    = Roughness;
    float nodeSize = BeadSize;
    float gridDens = GridDensity;
    float gridBri  = GridBrightness;
    float pulse    = Pulse;
    float streamsA = DataStreams;

    float2 U = input.uv * resolution;

    float3 dir = normalize(float3((U - 0.5 * resolution) / resolution.y, 1.0));
    // Minimal lateral sway — feels like a corridor, not an organic tube
    float3 ro = float3(sin(time * spd * 0.15) * 0.05, cos(time * spd * 0.1) * 0.05, time * spd * 1.2);

    dir.xy = mul(dir.xy, rot(time * spd * 0.12));

    // Slow heartbeat: raised-sine gives a sharp bright spike each cycle
    float hb      = pow(0.5 + 0.5 * sin(time * 1.8), 3.0);
    float pulseFac = 1.0 + pulse * 0.4 * hb;

    bool  hitNode = false;
    float rayT    = 0.0;
    float nodeId  = 0.0;
    float g1      = 0.0;
    float3 g2     = float3(0, 0, 0);
    float3 p      = ro;
    float3 col    = float3(0, 0, 0);
    float4 ni     = float4(0, 0, 0, 0);

    for (int i = 0; i < 250; i++) {
        p  = ro + dir * rayT;
        float dc = tunnelSdf(p, twist, rough, spd);
        ni = nodeInfo(p.z);
        float db = length(p - ni.xyz) - nodeSize;

        float w = min(dc, db);
        g1 += 0.002 / (0.01 + abs(dc));

        // Cyan-tinted volumetric scatter
        float flicker = (1.0 + 0.08 * (sin(time * 20.0 + ni.w * 93.1) * cos(time * 11.0 + ni.w * 17.4))) * pulseFac;
        float scatter = 0.0003 / (0.001 + db * db);
        g2 += (float3(scatter * 0.05, scatter * 0.5, scatter) + nodeColor(ni.w) * 0.005 / (0.02 + abs(db))) * flicker;

        if (abs(w) < 0.001 + rayT / 1000.0 || rayT > 25.0) {
            if (db < dc) {
                hitNode = true;
                nodeId  = ni.w;
            }
            break;
        }
        rayT += w * 0.8;
    }

    if (rayT <= 25.0) {
        if (hitNode) {
            // Glowing data node — bright core + halo tint
            col = float3(2.0, 6.0, 10.0) + nodeColor(nodeId) * 6.0 * pulseFac;
        } else {
            float eps = 0.001 + rayT / 1000.0;
            float3 nn = normalize(float3(
                tunnelSdf(p + float3(eps, 0, 0), twist, rough, spd) - tunnelSdf(p - float3(eps, 0, 0), twist, rough, spd),
                tunnelSdf(p + float3(0, eps, 0), twist, rough, spd) - tunnelSdf(p - float3(0, eps, 0), twist, rough, spd),
                tunnelSdf(p + float3(0, 0, eps), twist, rough, spd) - tunnelSdf(p - float3(0, 0, eps), twist, rough, spd)
            ));

            float3 q = p;
            q.xy = mul(q.xy, rot(q.z * twist));
            float fval     = fbm(q + time * spd * 0.1);
            float blendFac = pow(abs(clamp(fval + 0.5, 0.0, 1.0)), 2.0);
            // Deep navy → electric blue wall color
            col = lerp(float3(0.01, 0.03, 0.10), float3(0.0, 0.20, 0.55), blendFac);

            // Distant cool-white key light
            float3 ldir = ro + float3(0, 0, 5) - p;
            float  d1   = length(ldir);
            ldir /= d1;

            col = col * 0.03 + (
                col * max(dot(nn, ldir), 0.0) * 1.5 +
                float3(0.8, 0.95, 1.0) * pow(abs(max(dot(nn, normalize(ldir - dir)), 0.0)), 24.0) * smoothstep(20.0, 5.0, rayT) * 1.5
            ) / (1.0 + d1 * d1 * 0.08);

            // Nearest node as secondary light source
            ni = nodeInfo(p.z);
            float3 ndir     = ni.xyz - p;
            float  d2       = length(ndir);
            ndir /= d2;
            float3 nc2      = nodeColor(ni.w);
            float  flicker2 = (1.0 + 0.08 * (sin(time * 20.0 + ni.w * 93.1) * cos(time * 11.0 + ni.w * 17.4))) * pulseFac;

            col += (
                col * max(dot(nn, ndir), 0.0) * 2.5 +
                nc2 * pow(abs(max(dot(nn, normalize(ndir - dir)), 0.0)), 16.0) * 4.0
            ) * nc2 * flicker2 * (0.5 + 0.5 * frac(sin(ni.w * 88.1) * 12.3)) / (1.0 + d2 * d2 * 1.5);

            // Ambient occlusion
            float oa = 0.0, s = 1.0;
            for (int j = 0; j < 4; j++) {
                float h = 0.01 + 0.03 * float(j + 2);
                oa += (h - tunnelSdf(p + h * nn, twist, rough, spd)) * s;
                s  *= 0.9;
                if (oa > 0.33) break;
            }

            // Electric blue rim instead of purple
            col = (col + float3(0.05, 0.35, 1.0) * 0.08 * pow(abs(1.0 - max(dot(nn, -dir), 0.0)), 4.0) * 0.6 / (1.0 + d1 * d1 * 0.08))
                  * max(1.0 - 3.0 * oa, 0.0);

            // --- Grid overlay: angular × longitudinal lines on the tunnel wall ---
            if (gridBri > 0.01) {
                float theta  = atan2(q.y, q.x) * (1.0 / 3.14159265);
                float2 gUV   = float2(theta * gridDens * 2.0, p.z * gridDens * 0.18);
                float2 gFrac = abs(frac(gUV) - 0.5);
                float gridLine = 1.0 - smoothstep(0.0, 0.04, min(gFrac.x, gFrac.y));
                // Grid pulses with the heartbeat
                col += float3(0.0, 0.45, 1.0) * gridLine * gridBri * (0.6 + 0.4 * pulseFac);
            }

            // --- Data streams: bright packets flowing along tunnel axis ---
            if (streamsA > 0.01) {
                float theta2   = atan2(q.y, q.x) * (3.0 / 3.14159265) + 3.0; // map to [0, 6]
                float chanId   = floor(theta2);
                float chanEdge = abs(frac(theta2) - 0.5);
                float chanMask = 1.0 - smoothstep(0.12, 0.28, chanEdge);
                // Each channel has a random offset so packets are staggered
                float chanOffset  = frac(sin(chanId * 7.31) * 43.7) * 2.0;
                float streamPhase = frac(p.z * 0.25 - time * spd * 1.5 + chanOffset);
                // Sharp spike near streamPhase = 0.85
                float packet = exp(-abs(streamPhase - 0.85) * 45.0);
                float3 streamCol = (chanId < 2.0) ? float3(0.0, 0.8, 1.0) :
                                   (chanId < 4.0) ? float3(0.2, 1.0, 0.4) :
                                                    float3(0.8, 0.2, 1.0);
                col += streamCol * packet * chanMask * streamsA * 2.5;
            }
        }
    }

    // Fog: deep-space black-blue; cyan volumetric glow
    col = lerp(float3(0.0, 0.008, 0.03), col, 1.0 / exp(0.12 * rayT))
        + float3(0.0, 0.5, 1.0) * 0.1 * g1 * 0.02 / exp(0.05 * rayT)
        + g2 / exp(0.03 * rayT);

    // Filmic tonemapper (Narkowicz ACES approximation)
    col = col * (2.51 * col + 0.03) / (col * (2.43 * col + 0.59) + 0.14);

    // Vignette
    U /= resolution;
    U *= 1.0 - U;
    float vignette = pow(abs(16.0 * U.x * U.y), 0.25);
    col = pow(abs(col * vignette), (float3)2.5);

    return float4(col, 1.0);
}
