/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "SymmetryOrder", "TYPE": "long",
      "VALUES": [1,2,3,4,5,6,8,12], "LABELS": ["1","2","3","4","5","6","8","12"],
      "DEFAULT": 4, "LABEL": "Symmetry" },
    { "NAME": "InnerRadius",   "TYPE": "float", "MIN": 0.0, "MAX": 0.45, "DEFAULT": 0.08,"LABEL": "Inner Radius" },
    { "NAME": "RotSpeed",      "TYPE": "float", "MIN": -2.0,"MAX": 2.0,  "DEFAULT": 0.2, "LABEL": "Rotation Speed" },
    { "NAME": "GlowWidth",     "TYPE": "float", "MIN": 0.5, "MAX": 8.0,  "DEFAULT": 3.0, "LABEL": "Glow Width" },
    { "NAME": "CoreColour",    "TYPE": "color",             "DEFAULT": [0.2, 0.5, 1.0, 1.0], "LABEL": "Core Colour" },
    { "NAME": "OuterColour",   "TYPE": "color",             "DEFAULT": [1.0, 0.25, 0.05, 1.0], "LABEL": "Outer Colour" },
    { "NAME": "AudioBassIn",   "TYPE": "audio", "BAND": "bass", "LABEL": "Bass" },
    { "NAME": "AudioHighIn",   "TYPE": "audio", "BAND": "high", "LABEL": "Treble" }
  ]
}*/

// FFT bins mapped to polar coordinates with N-fold rotational symmetry.
// Bass bins expand the luminous core; treble detail extends radial spokes.
// Produces mandala-like forms that breathe with the audio.

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

float3 hsv2rgb(float h, float s, float v) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(h + K.xyz) * 6.0 - K.www);
    return v * lerp(K.xxx, saturate(p - K.xxx), s);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Centre and correct for non-square viewport.
    float2 centred = uv - 0.5;
    centred.x *= resolution.x / resolution.y;

    float r      = length(centred);
    float angle  = atan2(centred.y, centred.x) + time * RotSpeed;

    // Fold angle into one sector of N-fold symmetry.
    float sym          = float(SymmetryOrder);
    float sectorAngle  = 2.0 * PI / sym;
    float a = fmod(angle + 64.0 * PI, sectorAngle);
    float t = a / sectorAngle;
    t = 1.0 - abs(t * 2.0 - 1.0);

    // Sample spectrum at this angular position.
    float specVal = spectrumTexture.SampleLevel(videoSampler, float2(t, 0.5), 0).r;

    // Bass widens the core more aggressively.
    float coreR   = AudioBassIn * 0.28;
    float targetR = InnerRadius + coreR + specVal * max(0.0, 0.42 - InnerRadius - coreR);

    // Treble adds a fine inner ring.
    float trebleR = InnerRadius * 0.4 + AudioHighIn * 0.1;

    // Radial glow.
    float dMain   = abs(r - targetR);
    float dTreble = abs(r - trebleR);
    float glowMain   = exp(-dMain   * dMain   * GlowWidth * 80.0)  * (specVal + 0.15) * (1.0 + AudioBassIn * 2.0);
    float glowTreble = exp(-dTreble * dTreble * GlowWidth * 300.0) * AudioHighIn * 2.5;

    // Hue cycling with user colour tint.
    float hue  = t * 0.55 + specVal * 0.25 + time * 0.04;
    float3 hsvCol = hsv2rgb(hue, 0.9, glowMain + glowTreble * 0.6);

    // Modulate towards user colours based on radial position.
    float radialT = saturate(r / max(targetR + 0.05, 0.001));
    float3 userCol = lerp(CoreColour.rgb, OuterColour.rgb, radialT);
    float3 col = lerp(hsvCol, hsvCol * userCol * 2.0, 0.55);

    // Luminous core that pulses hard with bass.
    float coreGlow = exp(-r * r / max(0.0001, coreR * coreR + 0.003)) * AudioBassIn * 3.5;
    col += CoreColour.rgb * hsv2rgb(0.62 + AudioBassIn * 0.15, 0.6, coreGlow);

    // Extra brightness boost on strong bass.
    col *= 1.0 + AudioBassIn * 1.5;

    return float4(saturate(col), 1.0);
}
