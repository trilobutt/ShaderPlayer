/*{
    "INPUTS": [
        {"NAME": "GridSize",   "LABEL": "Sample Grid",  "TYPE": "float",
         "MIN": 8.0, "MAX": 32.0, "DEFAULT": 16.0, "STEP": 1.0},
        {"NAME": "Brightness", "LABEL": "Brightness",   "TYPE": "float",
         "MIN": 1.0, "MAX": 30.0, "DEFAULT": 12.0},
        {"NAME": "BlendVideo", "LABEL": "Video Blend",  "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.0},
        {"NAME": "Sigma",      "LABEL": "Spread",       "TYPE": "float",
         "MIN": 0.005, "MAX": 0.1, "DEFAULT": 0.035}
    ]
}*/

// Vectorscope — CbCr chrominance distribution (BT.709)
// X-axis = Cb (blue–yellow),  Y-axis = Cr (red–cyan), both normalised −0.5..+0.5.
// Centre = neutral grey.  Outer circle = maximum saturation boundary (radius 0.5).
//
// Samples a GridSize × GridSize grid across the video.  Increase GridSize for
// greater accuracy at the cost of GPU throughput (16×16 = 256 samples per pixel).
//
// Colour of each plotted point is the weighted average RGB of video samples near
// that CbCr position, making the scope self-annotating.
//
// Reference markers: 75% colour-bar primaries (R/G/B/Ye/Cy/Mg).
// Skin tone indicator: line from origin toward Cb≈0.034, Cr≈0.214 (warm orange).

Texture2D    videoTexture : register(t0);
SamplerState videoSampler : register(s0);

cbuffer Constants : register(b0) {
    float  time;
    float  padding1;
    float2 resolution;
    float2 videoResolution;
    float2 padding2;
    float4 custom[4];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// BT.709 RGB → (Cb, Cr), range −0.5..+0.5
float2 RGBtoCbCr(float3 rgb) {
    float cb = -0.1146 * rgb.r - 0.3854 * rgb.g + 0.5000 * rgb.b;
    float cr =  0.5000 * rgb.r - 0.4542 * rgb.g - 0.0458 * rgb.b;
    return float2(cb, cr);
}

// Reference dot positions and colours for 75% colour-bar primaries.
// Returns cbcr position via out parameter; colour as return value.
float3 GetRefColor(int idx) {
    if (idx == 0) return float3(1.0, 0.2, 0.2);   // Red
    if (idx == 1) return float3(0.2, 1.0, 0.2);   // Green
    if (idx == 2) return float3(0.3, 0.5, 1.0);   // Blue
    if (idx == 3) return float3(1.0, 1.0, 0.2);   // Yellow
    if (idx == 4) return float3(0.2, 1.0, 1.0);   // Cyan
    return              float3(1.0, 0.2, 1.0);     // Magenta
}

float2 GetRefCbCr(int idx) {
    // 75% primaries: full-saturation CbCr × 0.75
    if (idx == 0) return float2(-0.1146,  0.5000) * 0.75;  // Red
    if (idx == 1) return float2(-0.3854, -0.4542) * 0.75;  // Green
    if (idx == 2) return float2( 0.5000, -0.0458) * 0.75;  // Blue
    if (idx == 3) return float2(-0.5000,  0.0458) * 0.75;  // Yellow
    if (idx == 4) return float2( 0.1146, -0.5000) * 0.75;  // Cyan
    return              float2( 0.3854,  0.4542) * 0.75;   // Magenta
}

float4 main(PS_INPUT input) : SV_TARGET {
    // Map UV to CbCr: centre (0.5, 0.5) = (Cb=0, Cr=0); Cr+ is up.
    float cbTarget = input.uv.x - 0.5;
    float crTarget = 0.5 - input.uv.y;

    int   gridN    = int(GridSize);
    float invSig2  = 1.0 / (2.0 * Sigma * Sigma);

    float  totalW   = 0.0;
    float3 accumRGB = float3(0.0, 0.0, 0.0);

    [loop]
    for (int row = 0; row < gridN; row++) {
        [loop]
        for (int col = 0; col < gridN; col++) {
            float2 sampleUV = (float2(col, row) + 0.5) / float(gridN);
            float3 rgb      = videoTexture.Sample(videoSampler, sampleUV).rgb;
            float2 cbcr     = RGBtoCbCr(rgb);

            float dcb = cbcr.x - cbTarget;
            float dcr = cbcr.y - crTarget;
            float w   = exp(-(dcb * dcb + dcr * dcr) * invSig2);

            totalW   += w;
            accumRGB += rgb * w;
        }
    }

    float invN2 = 1.0 / float(gridN * gridN);
    float3 scopeCol = float3(0.0, 0.0, 0.0);

    if (totalW > 1e-5) {
        float3 avgRGB = accumRGB / totalW;
        float  density = saturate(totalW * Brightness * invN2);
        scopeCol = avgRGB * density;
    }

    // --- Graticule ---
    float2 centre  = float2(cbTarget, crTarget);
    float  radDist = length(centre);
    float  pxSize  = 1.0 / min(resolution.x, resolution.y);
    float3 grat    = float3(0.0, 0.0, 0.0);

    // Outer boundary circle (max saturation, radius 0.5)
    if (abs(radDist - 0.5) < pxSize * 1.5)
        grat = float3(0.2, 0.2, 0.2);

    // 50% saturation ring
    if (abs(radDist - 0.25) < pxSize * 1.0)
        grat = max(grat, float3(0.1, 0.1, 0.1));

    // Crosshair through (0, 0)
    if (abs(cbTarget) < pxSize * 1.0 || abs(crTarget) < pxSize * 1.0)
        grat = max(grat, float3(0.12, 0.12, 0.12));

    // 75% colour-bar reference dots
    [unroll]
    for (int r = 0; r < 6; r++) {
        float2 refPos  = GetRefCbCr(r);
        float  dRef    = length(centre - refPos);
        if (dRef < pxSize * 4.0) {
            float t = 1.0 - dRef / (pxSize * 4.0);
            grat = max(grat, GetRefColor(r) * t * 0.65);
        }
    }

    // Skin tone line — from origin toward (Cb≈0.034, Cr≈0.214) BT.709 flesh direction
    float2 skinDir   = normalize(float2(0.034, 0.214));
    float  proj      = dot(centre, skinDir);
    float2 projected = skinDir * proj;
    float  perpDist  = length(centre - projected);
    if (proj > 0.0 && proj < 0.48 && perpDist < pxSize * 1.5)
        grat = max(grat, float3(0.45, 0.28, 0.12));

    float3 finalScope = saturate(scopeCol + grat);

    // Blend with original video
    float4 videoCol = videoTexture.Sample(videoSampler, input.uv);
    return float4(lerp(finalScope, videoCol.rgb, BlendVideo), 1.0);
}
