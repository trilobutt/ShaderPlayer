/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "filterMode",     "LABEL": "Filter Type",   "TYPE": "long",
         "VALUES": [0,1,2,3,4,5], "LABELS": ["Low-pass","High-pass","Band-pass","Directional","Annular","Notch"], "DEFAULT": 0},
        {"NAME": "cutoffFreq",     "LABEL": "Cutoff",        "TYPE": "float", "MIN": 0.001, "MAX": 1.0,   "DEFAULT": 0.1},
        {"NAME": "bandWidth",      "LABEL": "Bandwidth",     "TYPE": "float", "MIN": 0.001, "MAX": 0.5,   "DEFAULT": 0.1},
        {"NAME": "filterAngle",    "LABEL": "Angle (deg)",   "TYPE": "float", "MIN": 0.0,   "MAX": 360.0, "DEFAULT": 0.0},
        {"NAME": "angleWidth",     "LABEL": "Angle Width",   "TYPE": "float", "MIN": 1.0,   "MAX": 180.0, "DEFAULT": 45.0},
        {"NAME": "magnitudeGamma", "LABEL": "Magnitude γ",   "TYPE": "float", "MIN": 0.1,   "MAX": 3.0,   "DEFAULT": 1.0},
        {"NAME": "showSpectrum",   "LABEL": "Show Spectrum", "TYPE": "bool",  "DEFAULT": false}
    ]
}*/

// Fourier-domain sculpting approximation.
// True per-pixel FFT is infeasible in a single pass; each mode is implemented as a
// spatial convolution or frequency-selective kernel that approximates the equivalent
// Fourier-domain mask: low-pass → Gaussian blur, high-pass → unsharp mask,
// band-pass → DoG, directional → 1-D blur along chosen angle, annular → ring DoG,
// notch → directional high-pass perpendicular to angle.

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

// Gaussian-weighted box blur with radius controlled by cutoff (lower = wider blur)
float3 gaussianBlur(float2 uv, float2 px, float radius) {
    float3 acc = 0;
    float  wt  = 0;
    int    r   = (int)clamp(radius, 1.0, 12.0);
    [loop] for (int y = -r; y <= r; y++) {
        [loop] for (int x = -r; x <= r; x++) {
            float g = exp(-float(x*x + y*y) / (2.0 * radius * radius));
            acc += videoTexture.Sample(videoSampler, uv + float2(x, y) * px).rgb * g;
            wt  += g;
        }
    }
    return acc / wt;
}

// Directional blur along angle (degrees), radius samples
float3 directionalBlur(float2 uv, float2 px, float angleDeg, float radius) {
    float a = radians(angleDeg);
    float2 dir = float2(cos(a), sin(a));
    float3 acc = 0;
    int    r   = (int)clamp(radius, 1.0, 10.0);
    [loop] for (int i = -r; i <= r; i++) {
        acc += videoTexture.Sample(videoSampler, uv + dir * float(i) * px).rgb;
    }
    return acc / float(2 * r + 1);
}

float3 applyGamma(float3 c, float g) {
    return pow(max(c, 0.0), 1.0 / max(g, 0.001));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float2 px  = 1.0 / resolution;
    float3 orig = videoTexture.Sample(videoSampler, uv).rgb;

    // Radius maps inversely to cutoffFreq: lower freq = larger kernel
    float kernelR = clamp((1.0 - cutoffFreq) * 12.0 + 1.0, 1.0, 12.0);
    float bandR   = clamp(bandWidth * 12.0 + 0.5, 0.5, 6.0);

    float3 result = orig;

    if (filterMode == 0) {
        // Low-pass: Gaussian blur
        result = gaussianBlur(uv, px, kernelR);

    } else if (filterMode == 1) {
        // High-pass: original minus low-pass residual
        float3 lo = gaussianBlur(uv, px, kernelR);
        result = saturate(orig - lo + 0.5);

    } else if (filterMode == 2) {
        // Band-pass: difference of Gaussians
        float3 loA = gaussianBlur(uv, px, kernelR);
        float3 loB = gaussianBlur(uv, px, max(kernelR - bandR, 0.5));
        result = saturate((loB - loA) * 4.0 + 0.5);

    } else if (filterMode == 3) {
        // Directional: blur along chosen angle, preserve perpendicular edges
        result = directionalBlur(uv, px, filterAngle, kernelR);

    } else if (filterMode == 4) {
        // Annular: band-pass with sharp inner and outer cutoffs (ring in freq domain)
        float3 outerBlur = gaussianBlur(uv, px, kernelR + bandR);
        float3 innerBlur = gaussianBlur(uv, px, max(kernelR - bandR, 0.5));
        result = saturate((innerBlur - outerBlur) * 3.0 + 0.5);

    } else {
        // Notch: directional high-pass — removes features aligned to the angle
        float3 dBlur = directionalBlur(uv, px, filterAngle, kernelR * 0.5);
        float3 dBlurPerp = directionalBlur(uv, px, filterAngle + 90.0, kernelR * 0.5);
        result = saturate(orig - dBlur * 0.5 + dBlurPerp * 0.3 + 0.2);
    }

    // Magnitude gamma adjustment
    result = applyGamma(result, magnitudeGamma);

    // Spectrum overlay: show the local spatial frequency magnitude as a heatmap
    if (showSpectrum) {
        float3 hi = abs(orig - gaussianBlur(uv, px, 2.0));
        float  freq = dot(hi, float3(0.299, 0.587, 0.114)) * 8.0;
        float3 heat = float3(freq, freq * 0.4, 1.0 - freq);
        result = lerp(result, saturate(heat), 0.6);
    }

    return float4(saturate(result), 1.0);
}
