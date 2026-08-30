---
title: Gotchas
nav_title: Gotchas
---

# Gotchas

Everything on this page has cost someone an hour. Read it before writing a shader rather
than after the compile error, because several of these fail silently and the ones that do
not fail with a message pointing somewhere other than the mistake.

## A parameter name is a macro, so it can shadow anything

Parameter aliases are `#define`s. A parameter named `Strength` becomes
`#define Strength custom[0].x`, and the preprocessor applies it to every occurrence of that
token in your source. Name a parameter after an HLSL intrinsic and every call to that
intrinsic in your body turns into nonsense: `frac`, `min`, `max`, `abs`, `lerp`, `step`,
`saturate`, `length`, `normalize`, `pow`. Reserved words are the same trap with stranger
errors: `line`, `point`, `triangle`, `linear`, `sample`, `matrix`, `vector`, `in`, `out`,
`inout`.

The library is safe from this (the preamble emits `ShaderCommon.hlsli` before the `#define`s,
so nothing above the definition is rewritten), which means the error lands in your own file,
on the line where you called the intrinsic. A parameter named `lerp` turns
`lerp(col.rgb, float3(1, 0, 0), 0.5)` into `custom[0](col.rgb, ...)` and reports
`error X3000: syntax error: unexpected token '('`. Recognising that shape saves the search:
a call that has always worked suddenly failing to parse is a name collision, not a compiler
bug.

`NAME` also has to be a valid identifier in the first place. Letters, digits and underscores;
no spaces, no leading digit. Put the human-readable version in `LABEL`, which has no
restrictions at all.

## `atanh` does not exist in ps_5_0

Neither does `tanh`. `tanh` comes from the helper library as `spTanh1`; `atanh` you write
yourself, and the clamp is not optional because the function goes to infinity at ±1.

```hlsl
/*{
    "SHADER_TYPE": "video",
    "DESCRIPTION": "Inverse hyperbolic tangent contrast, showing the atanh workaround.",
    "INPUTS": [
        {"NAME": "Amount", "LABEL": "Amount", "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "STEP": 0.01, "DEFAULT": 0.35}
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
    float4 custom[8];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// ps_5_0 has no atanh intrinsic: using one gives X3004, undeclared identifier.
// The identity is 0.5 * log((1+x)/(1-x)), and log() here is the natural log.
float myAtanh(float x) {
    x = clamp(x, -0.9999, 0.9999);
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

float4 main(PS_INPUT input) : SV_TARGET {
    float3 col = videoTexture.Sample(videoSampler, input.uv).rgb;

    // Signed around mid grey, then stretched: an S-curve run backwards, which
    // pushes the ends apart and leaves the middle alone.
    float3 c = clamp(col * 2.0 - 1.0, -0.99, 0.99);
    float3 s = float3(myAtanh(c.r), myAtanh(c.g), myAtanh(c.b)) * 0.35;

    col = saturate(lerp(col, s * 0.5 + 0.5, Amount));
    return float4(col, 1.0);
}
```

## The parameter block fails quietly

**A `long` without `VALUES` gives an empty dropdown that cannot be operated.** `MIN` and
`MAX` do not stand in for it: a `long` is a dropdown over a fixed list, not a ranged control,
the panel applies no numeric bounds to one, and no shipped shader declares bounds on one.
`LABELS` is parallel to `VALUES` and holds the display strings; the values themselves are
integers. A `DEFAULT` that is not present in `VALUES` leaves the combo stuck on the first
entry.

**Array-form `MIN`, `MAX` and `STEP` are ignored.** Those keys are read only when the JSON
value is a number, so `"MIN": [0.0, 0.0]` on a `point2d` is skipped and the field keeps its
default: 0.0 for `MIN`, 1.0 for `MAX`, 0.01 for `STEP`. There is no warning, and the widget
looks perfectly reasonable while covering the wrong range. Write scalars for every type.

**A parameter declared but never read still costs a slot and still renders a widget.** The
alias is generated, the value is packed, the slider moves, and nothing happens. Neither the
compiler nor the validator can see it, because an unused `#define` is not an error. When you
edit a shader, check that every declared `NAME` appears in the body.

**The budget is 32 floats across all non-audio parameters.** A `color` costs four and aligns
to a multiple of four, a `point2d` costs two and aligns to an even offset, everything else
costs one. The first parameter that does not fit stops the parse, and it and everything after
it are dropped, so the shader then fails to compile on the first dropped name. Audio bands
cost nothing.

**A block that is not valid JSON produces no parameters at all**, so every alias is missing
and the shader fails on the first name with `X3004: undeclared identifier`. The preset is
still listed in the Shader Library, marked with a red status dot and carrying the compiler's
message as its tooltip; selecting it draws the untreated picture, because a preset with no
compiled shader falls back to passthrough. That tooltip is the fastest read on a failure in
the app, and the validator is the fastest read outside it:

```
python tools/validate_shaders.py default_shaders/my_shader.hlsl
```

## Numbers that are not the size you expect

**Audio bands are small.** For ordinary music they run 0.01 to 0.3. A modulation of
`1.0 + bass` is invisible; multipliers three to five times what intuition suggests are the
working range. See [Audio bands and the spectrum](/reference/audio-and-spectrum/).

**Offset-based effects need a bigger default than they look like they should.** For a lens
formula like `offset = (uv - 0.5) * s`, `s = 0.003` moves the corner of a 1080p frame by
about four pixels, which reads as nothing. Aim for at least ten pixels at 1080p on the
default: `s` around 0.01 with a `MAX` of 0.05 or more is the baseline for chromatic
aberration, lens warp and their relatives.

**Colour defaults are 0 to 1, not 0 to 255.** `"DEFAULT": [255, 128, 0, 255]` is not orange,
it is white with an enormous overshoot in the red channel.

## Declaring what is already declared

The renderer binds `AudioConstants` at `b1` and `spectrumTexture` at `t3`, and the preamble
declares both for you as soon as one parameter is an audio band. Declaring either yourself in
that case fails with `error X3003: redefinition of 'spectrumTexture'`, pointing at a line you
did not think was a duplicate. The video and noise textures are the other way around:
declare `videoTexture`, `videoSampler`, `noiseTexture` and `noiseSampler` in every shader,
sampled or not.

The `Constants` cbuffer is uploaded as a raw copy of a C++ struct, so its layout is
positional. Renaming a field is harmless, reordering one is not, and there is no diagnostic:
the shader compiles and reads whatever now sits at that offset.

## Editing, reloading and the cache

`F5` in the editor recompiles the buffer you are looking at, and the effect appears on the
next frame. Saving the file from another editor works too: the file watcher notices, though
it does real work at most twice a second, so a save can take up to half a second to land.

Compiled bytecode is cached in `shader_cache/` beside the executable, keyed on a hash of the
full source including the preamble. A hit skips compilation entirely, which is what makes 45
presets load in milliseconds instead of seconds. Two consequences: editing
`src/ShaderCommon.hlsli` changes every preamble and so invalidates the whole cache at once,
and a shader you are certain you changed but that behaves as though you did not is never a
stale cache, because any change to any character is a different key. If you do need to force
a cold compile, delete the `shader_cache/` directory.

## Writing HLSL from PowerShell

`Set-Content -Encoding UTF8` writes a byte-order mark, and `fxc` rejects a file that starts
with one with a parse error that says nothing about encoding. Use
`[System.IO.File]::WriteAllText($path, $content, [System.Text.Encoding]::ASCII)` for any HLSL
written by a script.
