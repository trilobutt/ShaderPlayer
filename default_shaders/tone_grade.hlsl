/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "A per-pixel grade in four stages, each quantised to 8 bits as a lookup table would be, so a host reproduces a table-driven grade to within a code value or two: an exposure gamma, then lift, gain and gamma per channel as pow(clamp(lift + in * gain), gamma), then a filmic S curve with a lifted toe, then a vibrance boost that spares skin. Vibrance raises saturation most where it is lowest, and holds back on pixels whose hue sits near skin and whose saturation is low enough to be skin rather than a saturated fabric. Lift is in 8-bit code values.",
    "INPUTS": [
        {"NAME": "exposureGamma", "LABEL": "Exposure Gamma", "TYPE": "float", "MIN": 0.1, "MAX": 3.0, "DEFAULT": 1.0},
        {"NAME": "liftR", "LABEL": "Lift R", "TYPE": "float", "MIN": -64.0, "MAX": 64.0, "DEFAULT": 0.0},
        {"NAME": "liftG", "LABEL": "Lift G", "TYPE": "float", "MIN": -64.0, "MAX": 64.0, "DEFAULT": 0.0},
        {"NAME": "liftB", "LABEL": "Lift B", "TYPE": "float", "MIN": -64.0, "MAX": 64.0, "DEFAULT": 0.0},
        {"NAME": "gainR", "LABEL": "Gain R", "TYPE": "float", "MIN": 0.0, "MAX": 3.0, "DEFAULT": 1.0},
        {"NAME": "gainG", "LABEL": "Gain G", "TYPE": "float", "MIN": 0.0, "MAX": 3.0, "DEFAULT": 1.0},
        {"NAME": "gainB", "LABEL": "Gain B", "TYPE": "float", "MIN": 0.0, "MAX": 3.0, "DEFAULT": 1.0},
        {"NAME": "gammaR", "LABEL": "Gamma R", "TYPE": "float", "MIN": 0.1, "MAX": 3.0, "DEFAULT": 1.0},
        {"NAME": "gammaG", "LABEL": "Gamma G", "TYPE": "float", "MIN": 0.1, "MAX": 3.0, "DEFAULT": 1.0},
        {"NAME": "gammaB", "LABEL": "Gamma B", "TYPE": "float", "MIN": 0.1, "MAX": 3.0, "DEFAULT": 1.0},
        {"NAME": "sCurve", "LABEL": "S Curve", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.0},
        {"NAME": "toe", "LABEL": "Toe", "TYPE": "float", "MIN": 0.0, "MAX": 0.5, "DEFAULT": 0.0},
        {"NAME": "vibrance", "LABEL": "Vibrance", "TYPE": "float", "MIN": -1.0, "MAX": 2.0, "DEFAULT": 0.0},
        {"NAME": "skinProtect", "LABEL": "Skin Protect", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.85}
    ]
}*/

// Tone Grade: exposure gamma, lift/gain/gamma, S curve, skin-safe vibrance.
//
// Every stage runs on the gamma-encoded signal, as the grade it reproduces
// does, and ends by truncating to 8 bits. That reference builds each stage as
// a 256-entry table in floating point and stores it as uint8 by truncation, so
// a stage's output is floor(value * 255) / 255; reproducing the truncation as
// well as the curve keeps four stages within a code value or two of the
// table, where rounding instead would drift half a code value up per stage.
// The 1e-4 inside the floor absorbs float32 error on values that land exactly
// on a code value.

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
    float2 uv : TEXCOORD0;
};

float3 quantise8(float3 c) {
    return floor(saturate(c) * 255.0 + 1e-4) / 255.0;
}

float quantise8s(float x) {
    return floor(saturate(x) * 255.0 + 1e-4) / 255.0;
}

// Hue in OpenCV's 8-bit unit, half-degrees (0..180), which the skin band below
// is stated in.
float hueHalfDegrees(float3 c, float hi, float spread) {
    float h;
    if (hi == c.r) {
        h = 60.0 * (c.g - c.b) / spread;
    } else if (hi == c.g) {
        h = 120.0 + 60.0 * (c.b - c.r) / spread;
    } else {
        h = 240.0 + 60.0 * (c.r - c.g) / spread;
    }
    if (h < 0.0) {
        h += 360.0;
    }
    return h * 0.5;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float4 col = videoTexture.Sample(videoSampler, input.uv);
    float3 c = col.rgb;

    if (abs(exposureGamma - 1.0) >= 1e-3) {
        c = quantise8(pow(max(c, 0.0), exposureGamma));
    }

    float3 lifted = saturate(float3(liftR, liftG, liftB) / 255.0 + c * float3(gainR, gainG, gainB));
    c = quantise8(pow(lifted, float3(gammaR, gammaG, gammaB)));

    float3 bent = saturate(c - sCurve * 0.5 * sin(6.28318530718 * c));
    c = quantise8(toe + bent * (1.0 - toe));

    if (abs(vibrance) > 1e-6) {
        float hi = max(c.r, max(c.g, c.b));
        float lo = min(c.r, min(c.g, c.b));
        float spread = hi - lo;
        if (hi > 0.0 && spread > 0.0) {
            float s = spread / hi;
            float h = hueHalfDegrees(c, hi, spread);
            // Distance from the skin hue (11 half-degrees), wrapped round the circle.
            float d = abs(fmod(h - 11.0 + 90.0, 180.0) - 90.0);
            float nearSkin = saturate(1.0 - d / 13.0);
            float unsaturated = saturate(1.0 - (s - 0.30) / 0.30);
            float protect = skinProtect * nearSkin * unsaturated;
            float boosted = quantise8s(s * (1.0 + vibrance * (1.0 - s) * (1.0 - protect)));
            // HSV keeps hue and value, so every channel's distance below the
            // maximum scales with saturation.
            c = hi - (hi - c) * (boosted / s);
        }
    }

    return float4(saturate(c), col.a);
}
