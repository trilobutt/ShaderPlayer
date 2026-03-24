/*{
  "SHADER_TYPE": "video",
  "INPUTS": [
    { "NAME": "BladeCount",        "TYPE": "long",  "MIN": 3,   "MAX": 12,  "DEFAULT": 6,  "LABEL": "Aperture Blades" },
    { "NAME": "DiffractScale",     "TYPE": "float", "MIN": 0.05,"MAX": 2.0, "DEFAULT": 0.4,"LABEL": "Starburst Scale" },
    { "NAME": "DiffractIntensity", "TYPE": "float", "MIN": 0.0, "MAX": 3.0, "DEFAULT": 1.0,"LABEL": "Diffraction Intensity" },
    { "NAME": "WavelengthSpread",  "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.4,"LABEL": "Wavelength Spread (Colour)" },
    { "NAME": "BlendStrength",     "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.6,"LABEL": "Blend Strength" },
    { "NAME": "Threshold",         "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.65,"LABEL": "Brightness Threshold" }
  ]
}*/

// Physically grounded aperture diffraction starburst.
// Each bright region in the video generates radial spikes via the Fraunhofer
// diffraction pattern of a regular N-blade aperture (analytically computed).
// Per-channel wavelength offsets produce chromatic dispersion along the spikes.
// Blended back onto the source frame.

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

#define PI 3.14159265358979

// Fraunhofer starburst intensity for an N-blade aperture at polar coords (r, theta).
// Each blade contributes a pair of radial spikes 180° apart, giving N spikes total
// (or 2N for even N blades, consistent with real anamorphic bokeh).
float starburstKernel(float2 offset, int blades, float scaleArg) {
    float r     = length(offset) / max(scaleArg, 0.001);
    float theta = atan2(offset.y, offset.x);

    float intensity = 0.0;
    float fb        = float(blades);

    [loop]
    for (int k = 0; k < blades; ++k) {
        // Spike direction for blade k
        float spikeAngle = float(k) * PI / fb;
        float cosA       = cos(spikeAngle);
        float sinA       = sin(spikeAngle);
        // Project offset onto this spoke direction.
        float proj       = abs(offset.x * cosA + offset.y * sinA);
        float perp       = abs(-offset.x * sinA + offset.y * cosA);

        // Sinc-squared along the spike, Gaussian envelope across it.
        float u          = proj * 80.0 * (1.0 / max(scaleArg, 0.001));
        float sincArg    = u * PI + 0.001;
        float sincVal    = sin(sincArg) / sincArg;
        float crossFade  = exp(-perp * perp * 2000.0 / max(scaleArg * scaleArg, 0.0001));
        intensity       += sincVal * sincVal * crossFade;
    }

    // Falloff with distance to prevent starburst from extending to infinity.
    float rFade = exp(-r * r * 4.0);
    return intensity * rFade / max(1.0, float(blades));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float4 vid = videoTexture.Sample(videoSampler, uv);
    float  aspect = resolution.x / resolution.y;

    // Accumulate diffraction contribution from bright source pixels.
    // Sample a grid of potential source locations for performance.
    const int GRID = 6;
    float3 diffract = float3(0, 0, 0);

    [loop]
    for (int sy = 0; sy < GRID; ++sy) {
        [loop]
        for (int sx = 0; sx < GRID; ++sx) {
            float2 srcUV = (float2(sx, sy) + 0.5) / float(GRID);
            float4 srcPx = videoTexture.Sample(videoSampler, srcUV);
            float  srcL  = dot(srcPx.rgb, float3(0.299, 0.587, 0.114));

            // Only bright pixels act as diffraction sources.
            float srcMask = smoothstep(Threshold, Threshold + 0.1, srcL);
            if (srcMask < 0.001) continue;

            // Offset from source to current pixel (aspect-corrected).
            float2 offset;
            offset.x = (uv.x - srcUV.x) * aspect;
            offset.y =  uv.y - srcUV.y;

            // Per-channel wavelength offset for chromatic dispersion.
            // R (long wavelength) spikes are slightly wider than B.
            float spreadR = DiffractScale * (1.0 + WavelengthSpread * 0.15);
            float spreadG = DiffractScale;
            float spreadB = DiffractScale * (1.0 - WavelengthSpread * 0.12);

            float kR = starburstKernel(offset, BladeCount, spreadR);
            float kG = starburstKernel(offset, BladeCount, spreadG);
            float kB = starburstKernel(offset, BladeCount, spreadB);

            diffract += srcPx.rgb * float3(kR, kG, kB) * srcMask * srcL;
        }
    }

    float norm = float(GRID * GRID);
    diffract   = diffract / norm * DiffractIntensity * 6.0;

    float3 col = vid.rgb + diffract * BlendStrength;

    return float4(saturate(col), 1.0);
}
