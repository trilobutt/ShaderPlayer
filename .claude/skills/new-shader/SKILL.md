---
name: new-shader
description: Scaffold a new HLSL shader for ShaderPlayer with correct cbuffer layout and ISF parameter block
---

Create a new HLSL shader file in `shaders/` named after the argument provided (e.g. `/new-shader bloom` → `shaders/bloom.hlsl`).

The shader MUST use this exact cbuffer layout — deviating from it silently breaks the pipeline:

```hlsl
/*{
    "INPUTS": [
        {"NAME": "Intensity", "LABEL": "Intensity", "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 1.0}
    ]
}*/

Texture2D videoTexture : register(t0);
SamplerState videoSampler : register(s0);

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

**ISF packing rules** — enforce strictly:
- `float`, `bool`, `long`, `event`: 1 slot, no alignment
- `point2d`: 2 slots, must start at next even cbufferOffset
- `color`: 4 slots, must start at next multiple-of-4 cbufferOffset
- Maximum 16 floats total across all INPUTS — extras are silently skipped

**HLSL intrinsic shadowing** — NEVER name any variable, parameter, or ISF NAME after a HLSL built-in. Forbidden names include (but are not limited to):
`frac`, `min`, `max`, `abs`, `lerp`, `floor`, `ceil`, `round`, `clamp`, `saturate`, `sin`, `cos`, `tan`, `dot`, `cross`, `normalize`, `length`, `pow`, `sqrt`, `step`, `smoothstep`, `reflect`, `refract`, `mul`

Use descriptive names instead: `blurRadius`, `edgeStrength`, `tintColour`, etc.

**ISF type reference:**
- `float` — slider, `MIN`/`MAX`/`STEP`/`DEFAULT` (scalar)
- `bool` — checkbox, `DEFAULT` true/false
- `long` — integer slider, `MIN`/`MAX`/`DEFAULT`
- `color` — RGBA picker, `DEFAULT` [r,g,b,a]
- `point2d` — XY pad, `MIN`/`MAX`/`DEFAULT` [x,y]
- `event` — momentary button (no DEFAULT)

Adapt the ISF INPUTS and shader body for the effect described in the argument. After writing the file, state the path and remind the user to use Shader Library → "Scan Folder" to load it.
