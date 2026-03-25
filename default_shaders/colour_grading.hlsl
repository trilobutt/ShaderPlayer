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
        {"NAME": "saturation",    "LABEL": "Saturation", "TYPE": "float", "MIN": 0.0,    "MAX": 2.0,   "DEFAULT": 1.0},
        {"NAME": "hueRotation",   "LABEL": "Hue Rotate", "TYPE": "float", "MIN": -180.0, "MAX": 180.0, "DEFAULT": 0.0},
        {"NAME": "vignetteAmount","LABEL": "Vignette",   "TYPE": "float", "MIN": 0.0,    "MAX": 1.0,   "DEFAULT": 0.0}
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

float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = c.g < c.b ? float4(c.bg, K.wz) : float4(c.gb, K.xy);
    float4 q = c.r < p.x ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
    float d = q.x - min(q.w, q.y);
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}

float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float4 col = videoTexture.Sample(videoSampler, input.uv);

    // Gain (highlights): per-channel scale
    col.r *= gainRed;
    col.g *= gainGreen;
    col.b *= gainBlue;

    // Gamma (midtones): power curve, guard against zero
    col.r = pow(max(col.r, 0.0), 1.0 / max(gammaRed,   0.001));
    col.g = pow(max(col.g, 0.0), 1.0 / max(gammaGreen, 0.001));
    col.b = pow(max(col.b, 0.0), 1.0 / max(gammaBlue,  0.001));

    // Lift (shadows): additive offset
    col.r += liftRed;
    col.g += liftGreen;
    col.b += liftBlue;

    // Saturation: BT.709 luminance desaturation
    float lum = dot(col.rgb, float3(0.2126, 0.7152, 0.0722));
    col.rgb = lerp(float3(lum, lum, lum), col.rgb, saturation);

    // Hue rotation via HSV round-trip
    float3 hsv = rgb2hsv(col.rgb);
    hsv.x = frac(hsv.x + hueRotation / 360.0);
    col.rgb = hsv2rgb(hsv);

    // Vignette: smooth radial darkening from centre
    float2 vc = input.uv * 2.0 - 1.0;
    float vr = length(vc);
    float vf = 1.0 - smoothstep(0.25, 1.4, vr * (vignetteAmount * 1.5 + 0.0001));
    col.rgb *= vf;

    col.rgb = saturate(col.rgb);
    col.a   = 1.0;
    return col;
}
