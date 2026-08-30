/*{
    "SHADER_TYPE": "generative",
    "DESCRIPTION": "Conway's Life, B3/S23, with no buffer to hold the board: the generation seed comes from the clock and three rule steps are unrolled per pixel, so the grid reseeds every 1/updateHz seconds and visibly evolves. Cells are drawn as rounded anti-aliased tiles coloured by how many of the four sampled stages they survived, each carrying an inverse-square afterglow from the previous generation.",
    "INPUTS": [
        {"NAME": "cellSz",         "LABEL": "Cell Size (px)", "TYPE": "float", "MIN": 2.0,  "MAX": 24.0, "DEFAULT": 8.0},
        {"NAME": "updateHz",       "LABEL": "Update Rate Hz", "TYPE": "float", "MIN": 0.1,  "MAX": 8.0,  "DEFAULT": 2.0},
        {"NAME": "initialDensity", "LABEL": "Density",        "TYPE": "float", "MIN": 0.1,  "MAX": 0.9,  "DEFAULT": 0.45},
        {"NAME": "ageSaturation",  "LABEL": "Age Colour",     "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.8},
        {"NAME": "wrapEdges",      "LABEL": "Wrap Edges",     "TYPE": "bool",  "DEFAULT": true},
        {"NAME": "cellGap",        "LABEL": "Cell Gap",       "TYPE": "float", "MIN": 0.0,  "MAX": 0.6,  "DEFAULT": 0.2},
        {"NAME": "cellGlow",       "LABEL": "Cell Glow",      "TYPE": "float", "MIN": 0.0,  "MAX": 3.0,  "DEFAULT": 0.7},
        {"NAME": "paletteShift",   "LABEL": "Palette Shift",  "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.0},
        {"NAME": "CellTint",       "LABEL": "Cell Tint",      "TYPE": "color", "DEFAULT": [1.0,1.0,1.0,1.0]},
        {"NAME": "exposure",       "LABEL": "Exposure",       "TYPE": "float", "MIN": 0.1,  "MAX": 4.0,  "DEFAULT": 1.0},
        {"NAME": "vignetteAmt",    "LABEL": "Vignette",       "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.25},
        {"NAME": "rmsIn",          "LABEL": "Level",          "TYPE": "audio", "BAND": "rms"},
        {"NAME": "bassIn",         "LABEL": "Bass",           "TYPE": "audio", "BAND": "bass"},
        {"NAME": "beatIn",         "LABEL": "Beat",           "TYPE": "audio", "BAND": "beat"},
        {"NAME": "audioAmount",    "LABEL": "Audio Amount",   "TYPE": "float", "MIN": 0.0,  "MAX": 1.0,  "DEFAULT": 0.6}
    ]
}*/

// Conway Game of Life (B3/S23) with chromatic cell age.
// A single-pass shader cannot maintain inter-frame state, so the solution
// expands the GoL recurrence directly: for cell C at generation G, the state
// is computed by unrolling 3 levels of the B3/S23 rule into a 7×7 hash-seeded
// initial configuration.  Each generation uses floor(time*updateHz) as a seed,
// so the initial grid changes every 1/updateHz seconds and the 3-step evolved
// result is displayed.  Age = cells alive at each of the 4 sampled stages
// (initial + 3 steps), driving position along a cosine palette.
//
// Cells are drawn as rounded tiles with an anti-aliased edge rather than as
// whole grid squares: at a cell size of a few pixels, hard square cells alias
// into a crawling checkerboard whenever the grid is not an exact pixel multiple.
// Each cell of the *previous* generation (the 3x3 s2 neighbourhood, which is the
// last state known for anything other than the centre cell) also emits an
// inverse-square glow, so the field carries a one-generation afterimage.

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

// Signed distance to a rounded box, in cell units.
float sdRoundBox(float2 p, float2 b, float r) {
    float2 d = abs(p) - b + r;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv   = input.uv;

    // --- Audio modulation ---
    // Level accelerates the generation clock, bass tightens the grid and seeds a
    // denser soup, beats brighten the surviving cells. audioAmount 0 = static rules.
    float aRms  = rmsIn  * audioAmount;
    float aBass = bassIn * audioAmount;
    float aBeat = beatIn * audioAmount;

    // Map UV to cell coordinates
    float  cellPix  = max(cellSz * (1.0 - aBass * 0.45), 2.0);
    float2 cellPx   = resolution / cellPix;
    int2   cell     = int2(floor(uv * cellPx));
    int    cx       = cell.x;
    int    cy       = cell.y;
    float  seed     = floor(time * updateHz * (1.0 + aRms * 3.0));
    float  density  = saturate(initialDensity + aBass * 0.25);

    // Position within the cell, and the size of one screen pixel in cell units.
    // Derived from cellPix directly: frac() makes the cell coordinate jump at
    // every boundary, so fwidth() on it reports a whole cell, not a pixel.
    float2 q      = frac(uv * cellPx) - 0.5;
    float  pxCell = 1.0 / cellPix;

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
            initG[iy * 7 + ix] = initCell(nx, ny, seed, density);
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

    float3 tint = spSrgbToLinear(CellTint.rgb);

    // Afterglow from the previous generation, inverse-square in cell units.
    float3 col = float3(0.0, 0.0, 0.0);
    float  kr2 = 0.13;
    [unroll] for (int gy = -1; gy <= 1; gy++) {
        [unroll] for (int gx = -1; gx <= 1; gx++) {
            if (s2[(gy + 1) * 3 + (gx + 1)]) {
                float2 dv = q - float2(float(gx), float(gy));
                col += tint * cellGlow * 0.22 * kr2 / (dot(dv, dv) + kr2);
            }
        }
    }

    if (alive) {
        // Age: stages at which this cell was alive (0–4)
        int age = (initG[3 * 7 + 3] ? 1 : 0)
                + (s1[2 * 5 + 2]     ? 1 : 0)
                + (s2[4]             ? 1 : 0)
                + 1; // alive at final step (always true here)

        // Palette position by age. The old raw-hue ramp put the youngest cells on
        // pure red and the oldest on pure blue, two colours of very different
        // luminance, so age read as brightness rather than as colour.
        float  t   = float(age - 1) / 3.0 * 0.6 + paletteShift;
        float3 pal = max(spPalette(t, float3(0.5, 0.45, 0.5),
                                      float3(0.5, 0.45, 0.45),
                                      float3(1.0, 1.0, 1.0),
                                      float3(0.15, 0.35, 0.6)), 0.0);
        float3 cellCol = lerp(spLuma(pal).xxx, pal, ageSaturation) * tint;

        // Rounded tile with a one-pixel filtered edge.
        float2 half2 = (0.5 - cellGap * 0.5).xx;
        float  d     = sdRoundBox(q, half2, min(half2.x, 0.5) * 0.35);
        float  body  = 1.0 - smoothstep(-pxCell, pxCell, d);

        col += cellCol * body * (1.4 + aBeat * 1.6);
    }

    col *= exposure;
    col *= spVignette(uv, vignetteAmt, 0.85);

    col = spLinearToSrgb(spTonemapTanh(col));
    col = spDither(col, input.pos.xy, 1.0 / 255.0);

    return float4(saturate(col), 1.0);
}
