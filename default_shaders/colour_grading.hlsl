/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "liftRed",       "LABEL": "Lift R",     "TYPE": "float", "MIN": -0.5,   "MAX": 0.5,   "DEFAULT": 0.0},
        {"NAME": "liftGreen",     "LABEL": "Lift G",     "TYPE": "float", "MIN": -0.5,   "MAX": 0.5,   "DEFAULT": 0.0},
        {"NAME": "liftBlue",      "LABEL": "Lift B",     "TYPE": "float", "MIN": -0.5,   "MAX": 0.5,   "DEFAULT": 0.0},
        {"NAME": "gammaRed",      "LABEL": "Gamma R",    "TYPE": "float", "MIN": 0.1,    "MAX": 3.0,   "DEFAULT": 1.0},
        {"NAME": "gammaGreen",    "LABEL": "Gamma G",    "TYPE": "float", "MIN": 0.1,    "MAX": 3.0,   "DEFAULT": 1.0},
        {"NAME": "gammaBlue",     "LABEL": "Gamma B",    "TYPE": "float", "MIN": 0.1,    "MAX": 3.0,   "DEFAULT": 1.0},
        {"NAME": "gainRed",       "LABEL": "Gain R",     "TYPE": "float", "MIN": 0.0,    "MAX": 3.0,   "DEFAULT": 1.0},
        {"NAME": "gainGreen",     "LABEL": "Gain G",     "TYPE": "float", "MIN": 0.0,    "MAX": 3.0,   "DEFAULT": 1.0},
        {"NAME": "gainBlue",      "LABEL": "Gain B",     "TYPE": "float", "MIN": 0.0,    "MAX": 3.0,   "DEFAULT": 1.0},
        {"NAME": "contrastAmt",   "LABEL": "Contrast",   "TYPE": "float", "MIN": 0.0,    "MAX": 2.0,   "DEFAULT": 1.0},
        {"NAME": "saturation",    "LABEL": "Saturation", "TYPE": "float", "MIN": 0.0,    "MAX": 2.0,   "DEFAULT": 1.0},
        {"NAME": "hueRotation",   "LABEL": "Hue Rotate", "TYPE": "float", "MIN": -180.0, "MAX": 180.0, "DEFAULT": 0.0},
        {"NAME": "vignetteAmount","LABEL": "Vignette",   "TYPE": "float", "MIN": 0.0,    "MAX": 1.0,   "DEFAULT": 0.0},
        {"NAME": "hiRolloff",     "LABEL": "Highlight Rolloff", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.0}
    ]
}*/

// Lift / Gamma / Gain grade, plus contrast, saturation, hue and a vignette.
//
// The three tonal controls are applied as one operator, in the order the Grade
// node defines them:
//
//     out = pow(in * (gain - lift) + lift, 1 / gamma)
//
// so lift is where black lands, gain is where white lands, and gamma bends the
// curve between them. Applying them as three separate passes (the previous
// behaviour: scale, then power, then add) makes lift a flat offset on the whole
// image instead of a black point, and puts gamma on the wrong side of gain, so
// no two controls are independent and neither matches any other grading tool.
//
// Lift/gamma/gain and contrast run on the gamma-encoded signal. That is what the
// operators are defined against and what every other grading surface a colourist
// touches does; converting to linear first would silently change the meaning of
// every value already saved in config.json. Saturation and the vignette are the
// exceptions and are noted where they happen.
//
// Highlight Rolloff trades hard clipping for a shoulder. It defaults to 0, i.e.
// the clip, because a grading tool must be able to show you exactly what is
// clipped; raise it once the grade is set to recover what gain pushed past white.

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

// Hue rotation in YIQ. The HSV round trip it replaces is discontinuous at the
// grey axis (hue is undefined there and the reconstruction picks an arbitrary
// one), so near-neutral pixels used to fly off to a random hue and speckle. A
// rotation of the chroma plane leaves Y untouched and has no such singularity.
float3 hueRotateYIQ(float3 c, float ang) {
    static const float3x3 kToYIQ = float3x3(
        0.299,  0.587,  0.114,
        0.596, -0.275, -0.321,
        0.212, -0.523,  0.311);
    static const float3x3 kToRGB = float3x3(
        1.0,  0.956,  0.619,
        1.0, -0.272, -0.647,
        1.0, -1.106,  1.703);

    float3 yiq = mul(kToYIQ, c);
    float cs = cos(ang), sn = sin(ang);
    return mul(kToRGB, float3(yiq.x,
                              yiq.y * cs - yiq.z * sn,
                              yiq.y * sn + yiq.z * cs));
}

float3 softShoulder(float3 x, float knee) {
    float3 over = max(x - knee, 0.0);
    return min(x, knee) + (1.0 - knee) * over / (over + (1.0 - knee));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float4 col = videoTexture.Sample(videoSampler, input.uv);

    float3 lift  = float3(liftRed,  liftGreen,  liftBlue);
    float3 gain  = float3(gainRed,  gainGreen,  gainBlue);
    float3 gamma = max(float3(gammaRed, gammaGreen, gammaBlue), 0.001);

    // Lift / gamma / gain as one operator.
    col.rgb = pow(max(col.rgb * (gain - lift) + lift, 0.0), 1.0 / gamma);

    // Contrast about encoded 18% grey. Pivoting on 0.5 would sit most of a stop
    // above mid grey, so raising contrast would darken the whole image as a side
    // effect of steepening it.
    const float kMidGrey = 0.4620;
    col.rgb = (col.rgb - kMidGrey) * contrastAmt + kMidGrey;

    // Saturation toward a grey of the same *linear* luminance, re-encoded. Taking
    // the grey from the encoded values instead makes a fully desaturated blue far
    // brighter than the blue it came from.
    float y = spLuma(spSrgbToLinear(col.rgb));
    float3 grey = spLinearToSrgb(y.xxx);
    col.rgb = lerp(grey, col.rgb, saturation);

    // Negated so a positive rotation runs red -> yellow -> green, matching every
    // hue wheel and the HSV implementation this replaced. +I is orange and +Q is
    // magenta, so an unnegated rotation would run red -> magenta.
    if (abs(hueRotation) > 1e-4) {
        col.rgb = hueRotateYIQ(col.rgb, -radians(hueRotation));
    }

    // Vignette in linear light: it is a light-transport operation, and a lerp to
    // black on encoded values pulls the midtones down much faster than a lens does.
    if (vignetteAmount > 1e-4) {
        float3 lin = spSrgbToLinear(col.rgb) * spVignette(input.uv, vignetteAmount, 0.7);
        col.rgb = spLinearToSrgb(lin);
    }

    col.rgb = lerp(saturate(col.rgb), softShoulder(max(col.rgb, 0.0), 0.85), hiRolloff);

    // Gamma pushes stretch the shadows across fewer code values than they came
    // from, which is where a graded image bands first.
    col.rgb = spDither(col.rgb, input.pos.xy, 1.0 / 255.0);

    col.rgb = saturate(col.rgb);
    col.a   = 1.0;
    return col;
}
