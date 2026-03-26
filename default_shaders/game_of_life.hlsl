/*{
    "SHADER_TYPE": "generative",
    "INPUTS": [
        {"NAME": "cellSz",         "LABEL": "Cell Size (px)", "TYPE": "float", "MIN": 2.0,  "MAX": 24.0, "DEFAULT": 6.0},
        {"NAME": "updateHz",       "LABEL": "Update Rate Hz", "TYPE": "float", "MIN": 0.1,  "MAX": 8.0,  "DEFAULT": 2.0},
        {"NAME": "initialDensity", "LABEL": "Density",        "TYPE": "float", "MIN": 0.1,  "MAX": 0.9,  "DEFAULT": 0.45},
        {"NAME": "ageSaturation",  "LABEL": "Age Colour",     "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.8},
        {"NAME": "wrapEdges",      "LABEL": "Wrap Edges",     "TYPE": "bool",  "DEFAULT": true},
        {"NAME": "CellTint",       "LABEL": "Cell Tint",      "TYPE": "color", "DEFAULT": [1.0,1.0,1.0,1.0]}
    ]
}*/

// Conway Game of Life (B3/S23) with chromatic cell age.
// A single-pass shader cannot maintain inter-frame state, so the solution
// expands the GoL recurrence directly: for cell C at generation G, the state
// is computed by unrolling 3 levels of the B3/S23 rule into a 7×7 hash-seeded
// initial configuration.  Each generation uses floor(time*updateHz) as a seed,
// so the initial grid changes every 1/updateHz seconds and the 3-step evolved
// result is displayed.  Age = cells alive at each of the 4 sampled stages
// (initial + 3 steps), encoded in hue: red → yellow → green → cyan → blue.

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

float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Hash-based initial cell state for infinite grid
bool initCell(int cx, int cy, float seed, float dens) {
    float2 p = float2(float(cx), float(cy));
    float  s = fmod(seed, 997.0);
    float  h = frac(sin(dot(p + float2(s * 13.7, s * 7.31), float2(127.1, 311.7))) * 43758.5453);
    return h < dens;
}

// B3/S23 rule
bool golRule(bool alive, int nb) {
    return alive ? (nb == 2 || nb == 3) : (nb == 3);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv   = input.uv;

    // Map UV to cell coordinates
    float2 cellPx   = resolution / max(cellSz, 1.0);
    int2   cell     = int2(floor(uv * cellPx));
    int    cx       = cell.x;
    int    cy       = cell.y;
    float  seed     = floor(time * updateHz);

    // --- Fill 7×7 initial state centred on (cx, cy) ---
    bool initG[49];
    [unroll] for (int iy = 0; iy < 7; iy++) {
        [unroll] for (int ix = 0; ix < 7; ix++) {
            int nx = cx + ix - 3;
            int ny = cy + iy - 3;
            if (wrapEdges) {
                nx = ((nx % (int)cellPx.x) + (int)cellPx.x) % (int)cellPx.x;
                ny = ((ny % (int)cellPx.y) + (int)cellPx.y) % (int)cellPx.y;
            }
            initG[iy * 7 + ix] = initCell(nx, ny, seed, initialDensity);
        }
    }

    // --- Step 1: 5×5 ---
    bool s1[25];
    [unroll] for (int y1 = 0; y1 < 5; y1++) {
        [unroll] for (int x1 = 0; x1 < 5; x1++) {
            bool al = initG[(y1 + 1) * 7 + (x1 + 1)];
            int  nb = 0;
            [unroll] for (int dy = -1; dy <= 1; dy++) {
                [unroll] for (int dx = -1; dx <= 1; dx++) {
                    nb += (!(dx == 0 && dy == 0)) ? (initG[(y1 + 1 + dy) * 7 + (x1 + 1 + dx)] ? 1 : 0) : 0;
                }
            }
            s1[y1 * 5 + x1] = golRule(al, nb);
        }
    }

    // --- Step 2: 3×3 ---
    bool s2[9];
    [unroll] for (int y2 = 0; y2 < 3; y2++) {
        [unroll] for (int x2 = 0; x2 < 3; x2++) {
            bool al = s1[(y2 + 1) * 5 + (x2 + 1)];
            int  nb = 0;
            [unroll] for (int dy = -1; dy <= 1; dy++) {
                [unroll] for (int dx = -1; dx <= 1; dx++) {
                    nb += (!(dx == 0 && dy == 0)) ? (s1[(y2 + 1 + dy) * 5 + (x2 + 1 + dx)] ? 1 : 0) : 0;
                }
            }
            s2[y2 * 3 + x2] = golRule(al, nb);
        }
    }

    // --- Step 3: single centre cell ---
    bool al3 = s2[4];
    int  nb3 = 0;
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            nb3 += (!(dx == 0 && dy == 0)) ? (s2[(dy + 1) * 3 + (dx + 1)] ? 1 : 0) : 0;
        }
    }
    bool alive = golRule(al3, nb3);

    if (!alive) return float4(0.0, 0.0, 0.0, 1.0);

    // Age: stages at which this cell was alive (0–4)
    int age = (initG[3 * 7 + 3] ? 1 : 0)
            + (s1[2 * 5 + 2]     ? 1 : 0)
            + (s2[4]              ? 1 : 0)
            + 1; // alive at final step (always true here)

    // Hue: red (new) → yellow → green → cyan → blue (long-lived)
    float hue = float(age - 1) / 4.0 * 0.65;  // 0=red, 0.65=blue
    float sat = ageSaturation;
    float3 col = hsv2rgb(float3(hue, sat, 0.95)) * CellTint.rgb;

    return float4(saturate(col), 1.0);
}
