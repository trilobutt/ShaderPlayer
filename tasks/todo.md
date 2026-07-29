# Shader Quality Pass

Lift the shipped shader set from "technically correct" to "genuinely good looking",
applying the techniques used by shader artists (Xor / XorDev, Inigo Quilez) as a
shared quality floor while preserving each shader's identity.

## Decisions already made (do not re-litigate)

- **Scope**: all shaders in `default_shaders/` **except** the six measurement scopes
  (`waveform`, `vectorscope`, `rgb_parade`, `safe_areas`, `zebra`, `focus_peaking`).
  Those are instruments, not art — leave them byte-identical. 39 files in scope.
- **Engine changes**: a shared HLSL helper library injected via
  `ShaderManager::BuildDefinesPreamble` (done — see Phases 0–1 below). No feedback
  buffer, no new render targets, no `D3D11Renderer` changes.
- **Aesthetic**: per-shader identity preserved; every shader gains the same
  technical floor. No house style imposed.
- **Parameters**: new ISF params may be added freely where they earn their place.
  Existing param names must not be renamed (config.json restores saved values by
  name; a rename silently drops the user's saved value).

## Why this is needed (measured, not assumed)

- 20 of 45 shaders end in `return float4(saturate(col), 1.0)` — hard channel
  clipping. Per Xor: each channel clips at a different input level, so the hue
  shifts as brightness rises. A tonemap curve fixes this.
- Only 5 files use `fwidth`/`ddx` at all — nearly every procedural edge in the set
  is aliased.
- Zero files dither. Every smooth gradient bands on an 8-bit display.
- No shader accumulates energy in HDR; glow is either absent or faked with `pow`.

## Research summary (source material for the techniques below)

- **Tonemaps** (Xor, mini.gmshaders.com/p/tonemaps): ACES/Narkowicz
  `(x*(2.51x+0.03))/(x*(2.43x+0.59)+0.14)` — punchy, sharp colour. Uncharted2/Hable —
  soft roll to white. Unreal `x/(x+0.155)*1.019` — brightest, gamma baked in.
  Tanh `-1+2/(1+exp(-2x))` — ACES-like with darker, stronger colour; the code-golf
  favourite because it is one call.
- **Volumetric glow** (Xor, mini.gmshaders.com/p/volumetric): the whole idiom is
  `for(...) { float d = field(pos); pos += dir*d; col += tint / d; }` then
  `col = tanh(brightness*col)`. Inverse-distance accumulation produces real
  attenuation and bloom with no post-process pass. This is the single biggest
  visual lever available to us.
- **Palettes** (IQ): `a + b*cos(TAU*(c*t + d))` — continuous, cheap, and far more
  pleasing than a piecewise lerp between three user colours.
- **Band-limiting / AA** (IQ, iquilezles.org/articles/filterableprocedurals):
  estimate the pixel footprint with `fwidth()`, replace `step()` with
  `smoothstep()` over that footprint. For cosines, scale by `sin(0.5w)/(0.5w)`.
- **Vis dev** (Xor, mini.gmshaders.com/p/visdev): restrict the palette
  deliberately; check the histogram actually reaches full range; distribute detail
  evenly (no sharp element against a flat background); use glow, soft shadow and
  vignette to direct the eye.

## Constraints and traps

- **No previous-frame texture exists.** `t0` video, `t1` noise, `t2` is the
  compositor's generative input, `t3` spectrum. Trails and accumulation must be
  faked analytically (multi-tap along the motion path), not by temporal feedback.
  Do not attempt a feedback buffer — it is out of scope.
- **`float4 custom[8]` = 32 floats, hard cap.** Params past that are silently
  dropped with a warning appended to `compileError`, and nothing surfaces that in
  the UI. Count the budget for every shader that gains params
  (`float`/`bool`/`long`/`event` = 1; `point2d` = 2, even-aligned; `color` = 4,
  4-aligned).
- **An ISF block that fails to parse compiles to nothing and the shader silently
  vanishes from the library.** After editing any ISF block, re-run the validator.
- fxc rejects a UTF-8 BOM. The shipped shaders are BOM-less UTF-8 (many contain em
  dashes and arrows in comments), and the validator preserves that; do not "fix"
  them to ASCII, and never write HLSL with `Set-Content -Encoding UTF8`.
- Never shadow HLSL intrinsics or reserved words (`frac`, `lerp`, `sample`,
  `linear`, `line`, `point`...). `atanh` does not exist in ps_5_0.
- Audio band values sit at 0.01–0.3 for real music; multipliers need to be 3–5×
  higher than intuition suggests.
- The compositor multiplies the video blend by shader alpha (`blendAmount * g.a`),
  so writing `alpha < 1` is the idiomatic way to let video through. Only visible
  when a Video Blend mode is active.

---

done-when:
- `python tools/validate_shaders.py` compiles all 45 shaders with the real
  injected preamble and reports 0 errors.
- The 6 measurement scopes are unchanged (`git diff --stat` shows no entry for
  them).
- The app builds and runs; each of the 39 touched shaders has been loaded and
  visually confirmed to render (not black, not blown out, params responsive).
- No shader exceeds the 32-float `custom[]` budget.
- Every touched shader satisfies the per-shader checklist in Phase 2.

---

## Phases 0–1 — DONE (harness and helper library are in place)

`tools/validate_shaders.py`, `src/ShaderCommon.hlsli`, the CMake embed step
(`tools/embed_hlsli.cmake`), the `BuildDefinesPreamble` injection and the rewritten
`.claude/hooks/validate-hlsl.sh` are all committed. See the **Shared HLSL Helper
Library** and **Validating Shaders** sections of `CLAUDE.md` for the API list and
the exact commands. Baseline: all 45 shaders compile clean, none over budget.

## Phase 2 — Per-shader checklist

Apply to every shader in Phases 3–6. A shader is done when each line is either
applied or consciously rejected with a one-line comment saying why.

1. **Tonemap, never clip.** Replace terminal `saturate(col)` with a tonemap.
   ACES by default; tanh for anything with accumulated glow; Unreal where maximum
   brightness matters. Add an `Exposure` float param (default 1.0) driving it.
2. **Accumulate in HDR.** Anywhere the shader draws a bright feature (particles,
   filaments, orbits, field lines), switch to `col += tint / max(dist, eps)` so
   glow falls out of the maths instead of being faked with `pow`. This is the
   change that will move the needle most.
3. **Anti-alias every procedural edge.** Every `step()`, `<`, `>` or hard `frac`
   threshold on a procedural field becomes `spAAStep`. The checkerboard in
   `perlin_flow_field.hlsl:101` is the archetype of what to fix.
   **`fwidth` is wrong wherever the argument is discontinuous or loop-carried** —
   a `frac()` cell coordinate, a grid-local `rel`, a point advected inside an
   integration loop, a data-dependent fold. There `fwidth` reports the
   discontinuity as an infinitely wide pixel and smears a grey seam along every
   boundary. Supply the footprint analytically instead and pass it to a
   `smoothstep(t-w, t+w, v)` (see `aaStepW` in `electromagnetic_field.hlsl`,
   `pxCell` in `game_of_life.hlsl`, `foot` in `perlin_flow_field.hlsl`, and the
   hyperbolic-metric footprint in `hyperbolic_tiling.hlsl`).
4. **Dither before output.** `spDither(col, input.pos.xy, 1.0/255.0)` on anything
   with a smooth gradient.
5. **Work in linear.** Palette lerps and blends happen in linear light; convert at
   the boundary. Do not lerp sRGB values and call it a gradient.
6. **Palettes.** Where a shader has user `color` params, keep them — they are the
   interface. Where it invents its own ramp (HSV rainbows especially), replace
   with `spPalette` and expose the phase/offset instead of the raw hue.
7. **Detail layering.** Add a second, finer octave so the image holds up at any
   zoom. Flat regions are the most common failure in the current set.
8. **Composition.** A subtle default vignette or radial falloff to direct the eye;
   expose it as a param, default low, never 0-or-1.
9. **Audio shaders**: smooth the band values over time (they jitter frame to
   frame) and check the 3–5× multiplier rule actually produces visible motion on
   real music, not just on a test tone.
10. **Budget + validate.** Recount the `custom[]` floats, then run the validator.

## Phase 3 — Generative fields and patterns (10) — DONE (pending Phase 7 visual check)

- [x] `plasma.hlsl`
- [x] `domain_warp.hlsl`
- [x] `voronoi_cells.hlsl`
- [x] `perlin_flow_field.hlsl`
- [x] `electromagnetic_field.hlsl`
- [x] `arthropod_cuticle.hlsl`
- [x] `hyperbolic_tiling.hlsl`
- [x] `strange_attractor.hlsl`
- [x] `game_of_life.hlsl`
- [x] `reaction_diffusion.hlsl`

All ten compile clean with the injected preamble and are inside the `custom[]`
budget (largest is `arthropod_cuticle` at 22/32). They still need the Phase 7
run-and-look pass. Two default values were retuned rather than only added:
`game_of_life.cellSz` 6 → 8 (the new rounded tiles need room to read) and
`domain_warp.ChromaSplit` stays 0 (off by default, as before).

## Phase 4 — Fractals and simulations (7) — DONE (pending Phase 7 visual check)

- [x] `julia_set.hlsl`
- [x] `newton_fractal.hlsl`
- [x] `mandelbulb.hlsl`
- [x] `physarum_slime_mould.hlsl`
- [x] `diffusion_limited_aggregation.hlsl`
- [x] `hele_shaw_fingering.hlsl`
- [x] `fourier_crystal_growth.hlsl`

All seven compile clean with the injected preamble. `newton_fractal` is at exactly
32/32 floats — six root colours plus the numeric controls fill the block, so it has
no glow parameter (Exposure drives the whole image) and nothing further can be added
to it without removing a root colour and capping the degree at 5.

Behaviour changed beyond adding parameters, in three places:
- `hele_shaw_fingering` Linear mode: the front now sits at `uv.x == frontX` rather
  than `0.5 + frontX`, which had it off the right edge for most of the range.
- `hele_shaw_fingering` Radial/Branching: the angular perturbation is sampled on the
  unit direction vector, not on `atan2`, removing the radial seam along -x.
- `physarum_slime_mould`: `sensorDist` was a dead parameter (parsed, never read); it
  now sets the chemoattractant wavelength, which at the default reproduces the old
  hardcoded 6.0.

Two correctness fixes worth re-checking under Phase 7: `fourier_crystal_growth`'s
`hexLatticeSDF` searched a 2x2 candidate block on a sheared basis and could miss the
true nearest vertex (now 3x3), and `diffusion_limited_aggregation` had a redundant
`frac()` on a wrapped noise lookup whose seam poisoned the `fwidth` beside it.

## Phase 5 — Audio reactive (8) — DONE (pending Phase 7 visual check)

- [x] `audio_spectrum.hlsl`
- [x] `audio_bass_pulse.hlsl`
- [x] `audio_fluid_vortex.hlsl`
- [x] `acoustic_geology.hlsl`
- [x] `psychoacoustic_topography.hlsl`
- [x] `radial_burst.hlsl`
- [x] `fourier_sculpting.hlsl`
- [x] `synaptic_fire_network.hlsl`

All eight compile clean with the injected preamble; largest is
`psychoacoustic_topography` / `audio_fluid_vortex` at 20/32 floats.

`fourier_sculpting` is `SHADER_TYPE: "video"`, not audio — it was misfiled into this
phase. It was treated with Phase 6 restraint (correctness, not punch) and should not be
done again in Phase 6.

Checklist items consciously rejected, with reasons in the files:
- **Item 9 (smooth the band values)** everywhere: `AudioAnalyzer` already applies an EMA
  to every band and to the spectrum bins, exposed as Audio settings → Smoothing. Doing
  it again per-shader is impossible anyway with no previous-frame texture.
- **Item 1 (tonemap)** in `fourier_sculpting`: the library tonemaps are scene-referred
  (`spTonemapACES` lifts linear 0.2 to 0.30), so on already-graded footage they are a
  grade, not a safety net. It uses a local highlight-only roll-off — identity below the
  knee, C1-continuous at it, asymptotic to 1 — instead.
- **Item 6 (spPalette)** in `acoustic_geology`: the four named rock colours are the
  shader's identity and a cosine ramp cannot join red sandstone to pale shale without
  passing through hues no rock has. Converted to linear light instead.

Defects fixed beyond the checklist, all worth re-checking under Phase 7:
- `synaptic_fire_network` called `fwidth()` inside both the axon and node loops, which
  have varying trip counts and early-outs (fxc X3595, six instances). The footprint is
  now analytic: `p` is `uv * float2(ar, 1)` so one pixel is `1/resolution.y` in both
  axes exactly.
- `psychoacoustic_topography`'s ridgeline was a hard `uv.y >= sy_top` test with the
  nearest layer overwriting. It now composites back-to-front with analytic coverage
  (pixel height plus the ridge's own screen-space slope). Its `freq > 0.6` treble
  boost was a hard vertical seam in the terrain and is now ramped over a band, and the
  eruption `bool` is now a smooth `excess`.
- `fourier_sculpting`'s `angleWidth` was parsed and shown in the UI but never read —
  the same dead-parameter case as `physarum`'s `sensorDist`. It now sets the half-width
  of a wedge/cone blur for the Directional and Notch modes.
- All convolution in `fourier_sculpting` now runs in linear light. Blurring sRGB code
  values across an edge lands well below the true mean radiance, which is why the DoG
  modes were muddy. Linearisation is gamma 2.0 (square/sqrt), not `spSrgbToLinear`: the
  widest kernel is 625 taps and the annular mode runs two of them, so a `pow()` per tap
  is not affordable. The round trip is exact when the filter is a no-op.
- `audio_fluid_vortex` used implicit-LOD `Sample` on a loop-carried, `frac()`-wrapped
  coordinate — garbage derivatives, wrong mip along every wrap seam. All fetches on
  advected positions are now `SampleLevel`.

Behaviour changed beyond adding parameters:
- `audio_spectrum` gains a peak cap. A true temporal peak-hold with decay needs
  previous-frame state, which does not exist; the cap instead tracks the peak of the
  segment's spectral neighbourhood (±1.5 segments), which is always ≥ the bar, moves
  more slowly, and falls away as the local peak migrates. Same read, from data rather
  than from state.
- `audio_fluid_vortex` and `radial_burst` lose their HSV rainbows for `spPalette` with
  a `PaletteShift` phase param. `audio_fluid_vortex`'s `brightness` is relabelled "Dye
  Density" (name unchanged, so saved values survive) now that `Exposure` exists.
- Trails and glow are analytic multi-tap, per the no-feedback constraint:
  `audio_fluid_vortex` marches on past the advected point along the streamline,
  `audio_bass_pulse` marches outward along the pixel's radial line over a highlight knee.
- `radial_burst`, `synaptic_fire_network` and `audio_spectrum` replaced their `exp()`
  glows with inverse-distance accumulation under `spTonemapTanh`. `GlowWidth` keeps its
  original sense (larger = tighter) by scaling the distance denominator.

## Phase 6 — Video effects (14)

These sit over real footage, so restraint matters more than punch. The goal is
correctness and subtlety: proper filtering, no aliasing, no clipping.

- [ ] `chromatic_aberration.hlsl` — check the ≥10px-at-1080p rule for the default
      strength.
- [ ] `colour_grading.hlsl`
- [ ] `crt_simulation.hlsl` — scanlines must be band-limited or they alias into
      moire at non-native resolutions; this is the worst offender in the group.
- [ ] `datamosh_drift.hlsl`
- [ ] `false_colour.hlsl`
- [ ] `grayscale.hlsl` — verify it uses a luma weighting in linear light.
- [ ] `kaleidoscope.hlsl` — mirror seams need AA.
- [ ] `non_euclidean_lens.hlsl`
- [ ] `oil_paint_filter.hlsl`
- [ ] `pixel_sort.hlsl`
- [ ] `sharpen.hlsl` — check for ringing / haloing at default strength.
- [ ] `slit_scan.hlsl`
- [ ] `thermal_false_colour.hlsl` — replace the ramp with a perceptually uniform
      one (inferno/turbo via `spPalette` coefficients).
- [ ] `vignette.hlsl`

## Phase 7 — Verify and close out

- [ ] `python tools/validate_shaders.py` → 0 errors, no budget overflows.
- [ ] Build:
      `cmd /c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" >nul && cmake --build build"`
- [ ] Run from the project root, Scan Folder on `default_shaders/`, and step
      through all 39 shaders. Confirm each renders, is not blown out or black, and
      that its params still do something. Check the audio ones against real music,
      not silence.
- [ ] `git diff --stat` — confirm the six scopes are absent from the diff.
- [ ] Fold anything durable learned in Phases 3–6 into `CLAUDE.md`. The helper
      library, the `#line 1` convention, the validator command and the
      `.hlsli`-sharing resolution are already documented there. Do not write a
      changelog of this pass.
- [ ] Delete this task from `tasks/todo.md` entirely.
