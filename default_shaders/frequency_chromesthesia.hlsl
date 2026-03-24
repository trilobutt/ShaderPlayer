/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "PaletteMode",  "TYPE": "long",  "MIN": 0,   "MAX": 2,   "DEFAULT": 0,  "LABEL": "Palette (0=Scriabin 1=Newton 2=Rimington)" },
    { "NAME": "Saturation",   "TYPE": "float", "MIN": 0.0, "MAX": 2.0, "DEFAULT": 1.2,"LABEL": "Saturation" },
    { "NAME": "BrightGamma",  "TYPE": "float", "MIN": 0.3, "MAX": 3.0, "DEFAULT": 1.0,"LABEL": "Brightness Gamma" },
    { "NAME": "OverlayAlpha", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.0,"LABEL": "Video Overlay" },
    { "NAME": "RmsIn",        "TYPE": "audio", "BAND": "rms",  "LABEL": "RMS Level" }
  ]
}*/

// Each FFT bin is assigned a colour via classical pitch-colour synesthesia tables.
// Hue encodes frequency; brightness encodes amplitude.
// Three palette presets based on Scriabin, Newton, and Rimington's correspondences.
// OverlayAlpha blends the chromesthesia over the video source.

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

// HSV → RGB
float3 hsv2rgb(float h, float s, float v) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(h + K.xyz) * 6.0 - K.www);
    return v * lerp(K.xxx, saturate(p - K.xxx), s);
}

// Map a normalised frequency position [0,1] to a hue [0,1]
// according to one of three historical synesthesia systems.
float freqToHue(float t, int mode) {
    // Scriabin (1910): C=red, D=yellow, E=pearlwhite/cream, F=dark red,
    // G=orange, A=green, B=steely blue.  Approximated as a non-uniform hue ramp.
    // Stored as 8 hue stops (C D E F G A B C) then lerped.
    float scriabin[8] = { 0.00, 0.14, 0.10, 0.93, 0.07, 0.33, 0.62, 0.00 };
    float newton[8]   = { 0.00, 0.07, 0.17, 0.35, 0.50, 0.65, 0.85, 1.00 };
    float rimington[8]= { 0.93, 0.05, 0.12, 0.32, 0.50, 0.67, 0.78, 0.93 };

    float pos = t * 7.0;
    int   idx = (int)clamp(floor(pos), 0.0, 6.0);
    float frc = frac(pos);

    float h0, h1;
    if (mode == 0) { h0 = scriabin[idx]; h1 = scriabin[idx + 1]; }
    else if (mode == 1) { h0 = newton[idx]; h1 = newton[idx + 1]; }
    else { h0 = rimington[idx]; h1 = rimington[idx + 1]; }

    return lerp(h0, h1, frc);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // The horizontal axis maps to frequency bins.
    float freqPos = uv.x;                    // 0 = bass, 1 = treble
    float specVal = spectrumTexture.SampleLevel(videoSampler, float2(freqPos, 0.5), 0).r;

    // Apply gamma to compress low amplitudes.
    float bright = pow(saturate(specVal), BrightGamma);

    // Synesthesia colour for this frequency position.
    float hue    = freqToHue(freqPos, PaletteMode);
    float3 syCol = hsv2rgb(hue, Saturation, bright);

    // Vertical: simple glow gradient — peak at the amplitude height.
    // UV.y = 0 is top; amplitude draws up from the bottom.
    float barTop = 1.0 - bright;
    float dist   = uv.y - barTop;
    float glow   = exp(-dist * dist * 120.0) * bright;
    float fill   = (uv.y > barTop) ? bright * 0.25 : 0.0;

    float3 chromCol = syCol * (glow + fill);

    // Global brightness breathes with overall RMS.
    chromCol *= (0.3 + RmsIn * 1.4);

    // Optional video overlay.
    float4 vid = videoTexture.Sample(videoSampler, uv);
    float3 col = lerp(chromCol, vid.rgb, OverlayAlpha);

    return float4(saturate(col), 1.0);
}
