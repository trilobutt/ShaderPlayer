---
title: Shader Reference
nav_title: Overview
---

# Shader Reference

ShaderPlayer runs one HLSL pixel shader over a fullscreen triangle, and everything
that shader can read is fixed, small, and documented here. Forty-five shaders ship in
`default_shaders/`; each has a page below listing every parameter it exposes, its
default, and how much of the 32-float parameter budget it spends.

The same pages describe the contract your own shaders compile against. A file dropped
into the shader directory and picked up with Shader Library, Scan Folder is a first-class
preset: same cbuffer, same parameter widgets, same hot reload.

## Writing a shader

- [The cbuffer contract](/docs/reference/cbuffer-contract/). The declarations every shader
  carries, what each field holds, and what the compiler injects ahead of your source.
- [The ISF block and parameters](/docs/reference/isf-block-and-parameters/). Every parameter
  type, the `custom[]` slot it takes, and the `#define` alias it generates.
- [Audio bands and the spectrum](/docs/reference/audio-and-spectrum/). The six bands, the
  range their values actually occupy, and how to read the 256-bin FFT texture.
- [The noise texture](/docs/reference/noise-texture/). Perlin in red, Voronoi in green, bound
  at `t1` for every shader whether it asks for one or not.
- [ShaderCommon helpers](/docs/reference/shadercommon-helpers/). The library prepended to your
  source: tonemaps, palettes, hashes, dithers, band-limited steps.
- [Sampling and derivatives](/docs/reference/sampling-and-derivatives/). Why `Sample` and
  `SampleLevel` are the same call here, and where `fwidth` lies to you.
- [Gotchas](/docs/reference/gotchas/). The footguns worth reading before you hit them.

## Audio Reactive

Eight shaders whose subject is the sound itself. The six analyser bands and the 256-bin
spectrum texture are bound every frame, so these run with or without a video open; with
nothing playing the bands read zero and the spectrum texture is all zeroes.

- [Acoustic Geology](/docs/reference/shaders/acoustic_geology/)
- [Audio Bass Pulse](/docs/reference/shaders/audio_bass_pulse/)
- [Audio Fluid Vortex](/docs/reference/shaders/audio_fluid_vortex/)
- [Audio Spectrum](/docs/reference/shaders/audio_spectrum/)
- [Fourier Crystal Growth](/docs/reference/shaders/fourier_crystal_growth/)
- [Psychoacoustic Topography](/docs/reference/shaders/psychoacoustic_topography/)
- [Radial Burst](/docs/reference/shaders/radial_burst/)
- [Synaptic Fire Network](/docs/reference/shaders/synaptic_fire_network/)

## Generative

Fifteen shaders that make their own image and need no video open. `time` drives them,
and with nothing loaded it is wall-clock seconds, running until you press Pause. Fourteen
of the fifteen also read the audio bands and expose an Audio Amount scale over that
modulation; set it to 0 for the unmodulated pattern. Reaction Diffusion is the exception
and ignores audio entirely.

- [Arthropod Cuticle](/docs/reference/shaders/arthropod_cuticle/)
- [Diffusion-Limited Aggregation](/docs/reference/shaders/diffusion_limited_aggregation/)
- [Electromagnetic Field](/docs/reference/shaders/electromagnetic_field/)
- [Game of Life](/docs/reference/shaders/game_of_life/)
- [Hele-Shaw Fingering](/docs/reference/shaders/hele_shaw_fingering/)
- [Hyperbolic Tiling](/docs/reference/shaders/hyperbolic_tiling/)
- [Julia Set](/docs/reference/shaders/julia_set/)
- [Mandelbulb](/docs/reference/shaders/mandelbulb/)
- [Newton Fractal](/docs/reference/shaders/newton_fractal/)
- [Perlin Flow Field](/docs/reference/shaders/perlin_flow_field/)
- [Physarum Slime Mould](/docs/reference/shaders/physarum_slime_mould/)
- [Plasma](/docs/reference/shaders/plasma/)
- [Reaction Diffusion](/docs/reference/shaders/reaction_diffusion/)
- [Strange Attractor](/docs/reference/shaders/strange_attractor/)
- [Voronoi Cells](/docs/reference/shaders/voronoi_cells/)

## Video Effects

Twenty-two shaders that treat the decoded frame bound at `t0`. Six of them are
measurement instruments rather than looks: Waveform, RGB Parade, Vectorscope, False
Colour, Focus Peaking, and Zebra read the picture and draw a scope over it.

- [CRT Simulation](/docs/reference/shaders/crt_simulation/)
- [Chromatic Aberration](/docs/reference/shaders/chromatic_aberration/)
- [Colour Grading](/docs/reference/shaders/colour_grading/)
- [Datamosh Drift](/docs/reference/shaders/datamosh_drift/)
- [Domain Warp](/docs/reference/shaders/domain_warp/)
- [False Colour](/docs/reference/shaders/false_colour/)
- [Focus Peaking](/docs/reference/shaders/focus_peaking/)
- [Fourier Sculpting](/docs/reference/shaders/fourier_sculpting/)
- [Grayscale](/docs/reference/shaders/grayscale/)
- [Kaleidoscope](/docs/reference/shaders/kaleidoscope/)
- [Non-Euclidean Lens](/docs/reference/shaders/non_euclidean_lens/)
- [Oil Paint Filter](/docs/reference/shaders/oil_paint_filter/)
- [Pixel Sort](/docs/reference/shaders/pixel_sort/)
- [RGB Parade](/docs/reference/shaders/rgb_parade/)
- [Safe Areas](/docs/reference/shaders/safe_areas/)
- [Sharpen](/docs/reference/shaders/sharpen/)
- [Slit Scan](/docs/reference/shaders/slit_scan/)
- [Thermal False Colour](/docs/reference/shaders/thermal_false_colour/)
- [Vectorscope](/docs/reference/shaders/vectorscope/)
- [Vignette](/docs/reference/shaders/vignette/)
- [Waveform](/docs/reference/shaders/waveform/)
- [Zebra](/docs/reference/shaders/zebra/)
