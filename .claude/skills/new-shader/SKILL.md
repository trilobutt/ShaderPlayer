---
name: new-shader
description: Scaffold a new HLSL shader for ShaderPlayer with correct cbuffer layout and ISF parameter block
---

Create a new HLSL shader file in `default_shaders/` named after the argument provided (e.g. `/new-shader bloom` → `default_shaders/bloom.hlsl`).

The shader MUST use this exact cbuffer layout — deviating from it silently breaks the pipeline:

```hlsl
/*{
    "SHADER_TYPE": "video",
    "INPUTS": [
        {"NAME": "Intensity", "LABEL": "Intensity", "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 1.0}
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

float4 main(PS_INPUT input) : SV_TARGET {
    return videoTexture.Sample(videoSampler, input.uv);
}
```

**`noiseTexture`/`noiseSampler` are mandatory in ALL shaders** — declare them even if unused. BeginFrame() always binds them at t1/s1. R channel = Perlin gradient noise, G channel = Voronoi F1 (inverted, bright at cell centres). Noise sampler uses WRAP addressing.

**`SHADER_TYPE` field** — set in the ISF block root (not inside INPUTS):
- `"video"` — default; processes video input (can omit, absent = video effect)
- `"generative"` — no video dependency; purely generative animation
- `"audio"` — audio-reactive; receives auto-injected AudioConstants cbuffer and spectrum texture

**Audio shaders** (`"SHADER_TYPE": "audio"`) have the `AudioConstants` cbuffer and `spectrumTexture` **auto-injected** by the preamble — do NOT declare them manually in audio shaders. Use AudioBand ISF inputs to expose audio values as named aliases.

---

**ISF packing rules** — enforce strictly:
- `float`, `bool`, `long`, `event`: 1 slot, no alignment
- `point2d`: 2 slots, must start at next even cbufferOffset
- `color`: 4 slots, must start at next multiple-of-4 cbufferOffset
- `audio` (AudioBand): cbufferOffset = −1, consumes NO `custom[]` slot
- Maximum 16 floats total across all non-audio INPUTS — extras are silently skipped

---

**HLSL intrinsic and reserved word shadowing** — NEVER name any variable, parameter, or ISF NAME after an HLSL built-in or reserved keyword.

Forbidden intrinsics (non-exhaustive): `abs`, `acos`, `all`, `any`, `asin`, `atan`, `atan2`, `ceil`, `clamp`, `clip`, `cos`, `cosh`, `cross`, `ddx`, `ddy`, `degrees`, `distance`, `dot`, `exp`, `exp2`, `floor`, `fmod`, `frac`, `fwidth`, `length`, `lerp`, `log`, `log2`, `max`, `min`, `modf`, `mul`, `normalize`, `pow`, `radians`, `reflect`, `refract`, `round`, `rsqrt`, `saturate`, `sign`, `sin`, `sincos`, `sinh`, `smoothstep`, `sqrt`, `step`, `tan`, `tanh`, `transpose`, `trunc`

**`atanh` is NOT a built-in in HLSL ps_5_0.** If you need it, implement manually:
```hlsl
float myAtanh(float x) {
    x = clamp(x, -0.9999, 0.9999);
    return 0.5 * log((1.0 + x) / (1.0 - x));
}
```

Forbidden reserved words: `line`, `point`, `triangle`, `linear`, `sample`, `centroid`, `nointerpolation`, `precise`, `shared`, `groupshared`, `uniform`, `volatile`

Use descriptive names instead: `blurRadius`, `edgeStrength`, `tintColour`, `noiseScale`, etc.

---

**ISF type reference:**
- `float` — slider, `MIN`/`MAX`/`STEP`/`DEFAULT` (scalar)
- `bool` — checkbox, `DEFAULT` true/false
- `long` — dropdown/integer, `VALUES` array of **integers**, `LABELS` array of strings (parallel), `DEFAULT` integer. Never put strings in `VALUES`.
- `color` — RGBA picker, `DEFAULT` [r,g,b,a]
- `point2d` — XY pad, `DEFAULT` [x,y]; `MIN`/`MAX` must be **scalar** floats (the parser only reads scalar values — array-form `"MIN": [0.0, 0.0]` is silently ignored and bounds default to 0.0/1.0)
- `event` — momentary button (no DEFAULT, not keyframeable)
- `audio` — AudioBand read-only meter; requires `"BAND"` field: one of `"rms"`, `"bass"`, `"mid"`, `"high"`, `"beat"`, `"centroid"`. Consumes no `custom[]` slot. Only valid when `SHADER_TYPE` is `"audio"`.

**`long` example:**
```json
{"NAME": "BlendMode", "LABEL": "Blend Mode", "TYPE": "long",
 "VALUES": [0, 1, 2], "LABELS": ["Add", "Multiply", "Screen"], "DEFAULT": 0}
```

**`long` without `VALUES` is silently broken** — the dropdown renders empty and cannot be changed at runtime. Always include `VALUES`. Never write `"MIN"`/`"MAX"` alone for a `long` type.

**`audio` example (audio shaders only):**
```json
{"NAME": "bassLevel", "LABEL": "Bass", "TYPE": "audio", "BAND": "bass"}
```
This generates `#define bassLevel audioBass` — use `bassLevel` directly in the shader body.

---

Adapt the ISF INPUTS and shader body for the effect described in the argument. After writing the file, state the path and remind the user to use Shader Library → "Scan Folder" to load it.
