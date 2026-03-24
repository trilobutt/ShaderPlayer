/*{
  "SHADER_TYPE": "audio",
  "INPUTS": [
    { "NAME": "ScrollSpeed",  "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.12, "LABEL": "Scroll Speed" },
    { "NAME": "FoldStrength", "TYPE": "float", "MIN": 0.0, "MAX": 0.3,  "DEFAULT": 0.08, "LABEL": "Fold Strength" },
    { "NAME": "FaultThresh",  "TYPE": "float", "MIN": 0.0, "MAX": 1.0,  "DEFAULT": 0.55, "LABEL": "Fault Threshold" },
    { "NAME": "ColourSat",    "TYPE": "float", "MIN": 0.0, "MAX": 2.0,  "DEFAULT": 1.2,  "LABEL": "Colour Saturation" },
    { "NAME": "BassBand",     "TYPE": "audio", "BAND": "bass",  "LABEL": "Bass" },
    { "NAME": "MidBand",      "TYPE": "audio", "BAND": "mid",   "LABEL": "Mid" },
    { "NAME": "HighBand",     "TYPE": "audio", "BAND": "high",  "LABEL": "Treble" },
    { "NAME": "BeatIn",       "TYPE": "audio", "BAND": "beat",  "LABEL": "Beat" }
  ]
}*/

// Audio deposits virtual sedimentary layers in real time.
// Low frequencies produce thick warm-coloured strata; high frequencies create
// fine pale laminae. Beat transients above threshold inject fault displacements.
// The canvas scrolls upward like a drill core being extracted.

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

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;

    // Scroll coordinate: UV.y advances upward over time.
    float depth = uv.y + time * ScrollSpeed;

    // Bass-driven large-scale folding of strata (anticlinal / synclinal structures).
    float foldWave = sin(uv.x * 7.0 + depth * 2.5) * FoldStrength * BassBand;
    float warpedDepth = depth + foldWave;

    // Mid-frequency fine folding.
    warpedDepth += sin(uv.x * 18.0 + depth * 7.0) * FoldStrength * 0.4 * MidBand;

    // Beat transients inject fault / unconformity displacements.
    float faultAmt = (BeatIn > FaultThresh) ? BeatIn * 0.15 : 0.0;
    float faultDir = sign(sin(uv.x * 30.0 + depth * 5.0));
    float faultedDepth = warpedDepth + faultDir * faultAmt * sin(depth * 20.0 + time * 10.0);

    // Sample noise texture for stratum grain at two scales.
    float2 strataUV   = float2(uv.x * 1.8, faultedDepth * 3.5);
    float2 fineUV     = float2(uv.x * 5.0, faultedDepth * 12.0);
    float coarseGrain = noiseTexture.Sample(noiseSampler, strataUV).r;
    float fineGrain   = noiseTexture.Sample(noiseSampler, fineUV + float2(0.3, 0.7)).r;

    // Stratum thickness: bass creates thick beds, treble creates thin laminae.
    float bedThickness = 3.5 + BassBand * 8.0;
    float bed          = frac(faultedDepth * bedThickness);
    float lamina       = frac(faultedDepth * (bedThickness * 4.0 + HighBand * 20.0));

    // Grain texture within each bed.
    float grain = coarseGrain * 0.6 + fineGrain * 0.4;
    float strataPattern = bed * 0.6 + lamina * 0.2 + grain * 0.2;

    // Colour palette: bass = warm red-brown, mid = tan/ochre, high = pale grey.
    float3 warmCol    = float3(0.60, 0.28, 0.10);   // red sandstone
    float3 neutralCol = float3(0.75, 0.62, 0.40);   // buff limestone
    float3 coolCol    = float3(0.90, 0.90, 0.85);   // pale shale
    float3 darkCol    = float3(0.20, 0.14, 0.08);   // dark mudstone

    float bassWeight = saturate(BassBand * 2.0);
    float highWeight = saturate(HighBand * 3.0);
    float3 baseCol   = lerp(lerp(neutralCol, warmCol, bassWeight), coolCol, highWeight);

    // Darker bands at bed boundaries (compression seams).
    float seam = pow(1.0 - abs(bed * 2.0 - 1.0), 6.0);
    baseCol    = lerp(baseCol, darkCol, seam * 0.7);

    float3 col = baseCol * (0.55 + strataPattern * 0.9);
    col        = lerp(neutralCol * 0.5, col, ColourSat);

    // Flash the whole frame on a strong beat (volcanic intrusion).
    float flashBright = max(0.0, BeatIn - FaultThresh) * 2.5;
    col += float3(1.0, 0.85, 0.5) * flashBright * 0.4;

    return float4(saturate(col), 1.0);
}
