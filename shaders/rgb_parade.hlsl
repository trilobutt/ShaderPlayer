/*{
    "INPUTS": [
        {"NAME": "NumSamples", "LABEL": "Sample Rows", "TYPE": "float",
         "MIN": 32.0, "MAX": 256.0, "DEFAULT": 128.0, "STEP": 1.0},
        {"NAME": "Brightness", "LABEL": "Brightness",  "TYPE": "float",
         "MIN": 1.0, "MAX": 20.0, "DEFAULT": 6.0},
        {"NAME": "BlendVideo", "LABEL": "Video Blend", "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.0}
    ]
}*/

// RGB Parade
// Displays three side-by-side waveform monitors — Red, Green, Blue — each showing
// the distribution of that channel across the full frame width.
// Y-axis: 0–100 IRE (top = 100).  Graticule at every 10 IRE.
// Channel waveforms are colour-coded (red / green / blue tint).
// Useful for detecting colour cast, channel imbalance, and clipping.
// BlendVideo: 0 = pure parade on black, 1 = original video.

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

static const float kSigma = 0.009;
static const float kInvS2 = 1.0 / (2.0 * kSigma * kSigma);

float4 main(PS_INPUT input) : SV_TARGET {
    // Divide output horizontally into three equal panels (R | G | B).
    static const float kThird     = 0.33333333;
    static const float kTwoThirds = 0.66666667;

    float  panelF     = input.uv.x * 3.0;
    int    panel      = clamp(int(panelF), 0, 2);   // 0=R, 1=G, 2=B
    float  localU     = panelF - float(panel);       // 0..1 within panel
    float  targetVal  = 1.0 - input.uv.y;            // top = 1.0 (100 IRE)

    int nSamples = int(NumSamples);
    float accum  = 0.0;

    [loop]
    for (int i = 0; i < nSamples; i++) {
        float  v   = (float(i) + 0.5) / float(nSamples);
        float3 rgb = videoTexture.Sample(videoSampler, float2(localU, v)).rgb;

        float chanVal;
        if      (panel == 0) chanVal = rgb.r;
        else if (panel == 1) chanVal = rgb.g;
        else                 chanVal = rgb.b;

        float d = chanVal - targetVal;
        accum += exp(-d * d * kInvS2);
    }

    float  k       = Brightness / float(nSamples);
    float  waveMag = saturate(accum * k);

    // Colour-code each channel panel
    float3 waveCol;
    if      (panel == 0) waveCol = float3(waveMag, waveMag * 0.08, waveMag * 0.08);
    else if (panel == 1) waveCol = float3(waveMag * 0.08, waveMag, waveMag * 0.08);
    else                 waveCol = float3(waveMag * 0.08, waveMag * 0.18, waveMag);

    // --- Graticule: every 10 IRE ---
    float gridX     = frac(targetVal * 10.0 + 0.0001);
    float tolerance = 5.0 / resolution.y;
    bool  onGrid    = (gridX < tolerance || gridX > 1.0 - tolerance);
    if (onGrid) {
        float gr = 0.11;
        if (targetVal < 2.0 / resolution.y || targetVal > 1.0 - 2.0 / resolution.y)
            gr = 0.30;
        waveCol += gr;
    }

    // --- Panel divider lines ---
    bool isDivider = abs(input.uv.x - kThird)     < 1.5 / resolution.x ||
                     abs(input.uv.x - kTwoThirds) < 1.5 / resolution.x;
    if (isDivider) waveCol = float3(0.35, 0.35, 0.35);

    // --- Channel label bands: thin coloured strip at the very top ---
    if (input.uv.y < 4.0 / resolution.y) {
        if      (panel == 0) waveCol = float3(1.0, 0.2, 0.2);
        else if (panel == 1) waveCol = float3(0.2, 1.0, 0.2);
        else                 waveCol = float3(0.3, 0.5, 1.0);
    }

    waveCol = saturate(waveCol);

    float4 videoCol = videoTexture.Sample(videoSampler, input.uv);
    return float4(lerp(waveCol, videoCol.rgb, BlendVideo), 1.0);
}
