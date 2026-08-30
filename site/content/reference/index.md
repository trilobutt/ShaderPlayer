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

- [The cbuffer contract](/reference/cbuffer-contract/). The declarations every shader
  carries, what each field holds, and what the compiler injects ahead of your source.
- [The ISF block and parameters](/reference/isf-block-and-parameters/). Every parameter
  type, the `custom[]` slot it takes, and the `#define` alias it generates.
- [Audio bands and the spectrum](/reference/audio-and-spectrum/). The six bands, the
  range their values actually occupy, and how to read the 256-bin FFT texture.
- [The noise texture](/reference/noise-texture/). Perlin in red, Voronoi in green, bound
  at `t1` for every shader whether it asks for one or not.
- [ShaderCommon helpers](/reference/shadercommon-helpers/). The library prepended to your
  source: tonemaps, palettes, hashes, dithers, band-limited steps.
- [Sampling and derivatives](/reference/sampling-and-derivatives/). Why `Sample` and
  `SampleLevel` are the same call here, and where `fwidth` lies to you.
- [Gotchas](/reference/gotchas/). The footguns worth reading before you hit them.

## Audio Reactive

Eight shaders whose subject is the sound itself. The six analyser bands and the 256-bin
spectrum texture are bound every frame, so these run with or without a video open; with
nothing playing the bands read zero and the spectrum texture is all zeroes.

- [Acoustic Geology](/reference/shaders/acoustic_geology/)
- [Audio Bass Pulse](/reference/shaders/audio_bass_pulse/)
- [Audio Fluid Vortex](/reference/shaders/audio_fluid_vortex/)
- [Audio Spectrum](/reference/shaders/audio_spectrum/)
- [Fourier Crystal Growth](/reference/shaders/fourier_crystal_growth/)
- [Psychoacoustic Topography](/reference/shaders/psychoacoustic_topography/)
- [Radial Burst](/reference/shaders/radial_burst/)
- [Synaptic Fire Network](/reference/shaders/synaptic_fire_network/)

## Generative

Fifteen shaders that make their own image and need no video open. `time` drives them,
and with nothing loaded it is wall-clock seconds, running until you press Pause. Fourteen
of the fifteen also read the audio bands and expose an Audio Amount scale over that
modulation; set it to 0 for the unmodulated pattern. Reaction Diffusion is the exception
and ignores audio entirely.

- [Arthropod Cuticle](/reference/shaders/arthropod_cuticle/)
- [Diffusion-Limited Aggregation](/reference/shaders/diffusion_limited_aggregation/)
- [Electromagnetic Field](/reference/shaders/electromagnetic_field/)
- [Game of Life](/reference/shaders/game_of_life/)
- [Hele-Shaw Fingering](/reference/shaders/hele_shaw_fingering/)
- [Hyperbolic Tiling](/reference/shaders/hyperbolic_tiling/)
- [Julia Set](/reference/shaders/julia_set/)
- [Mandelbulb](/reference/shaders/mandelbulb/)
- [Newton Fractal](/reference/shaders/newton_fractal/)
- [Perlin Flow Field](/reference/shaders/perlin_flow_field/)
- [Physarum Slime Mould](/reference/shaders/physarum_slime_mould/)
- [Plasma](/reference/shaders/plasma/)
- [Reaction Diffusion](/reference/shaders/reaction_diffusion/)
- [Strange Attractor](/reference/shaders/strange_attractor/)
- [Voronoi Cells](/reference/shaders/voronoi_cells/)

## Video Effects

Twenty-two shaders that treat the decoded frame bound at `t0`. Six of them are
measurement instruments rather than looks: Waveform, RGB Parade, Vectorscope, False
Colour, Focus Peaking, and Zebra read the picture and draw a scope over it.

- [CRT Simulation](/reference/shaders/crt_simulation/)
- [Chromatic Aberration](/reference/shaders/chromatic_aberration/)
- [Colour Grading](/reference/shaders/colour_grading/)
- [Datamosh Drift](/reference/shaders/datamosh_drift/)
- [Domain Warp](/reference/shaders/domain_warp/)
- [False Colour](/reference/shaders/false_colour/)
- [Focus Peaking](/reference/shaders/focus_peaking/)
- [Fourier Sculpting](/reference/shaders/fourier_sculpting/)
- [Grayscale](/reference/shaders/grayscale/)
- [Kaleidoscope](/reference/shaders/kaleidoscope/)
- [Non-Euclidean Lens](/reference/shaders/non_euclidean_lens/)
- [Oil Paint Filter](/reference/shaders/oil_paint_filter/)
- [Pixel Sort](/reference/shaders/pixel_sort/)
- [RGB Parade](/reference/shaders/rgb_parade/)
- [Safe Areas](/reference/shaders/safe_areas/)
- [Sharpen](/reference/shaders/sharpen/)
- [Slit Scan](/reference/shaders/slit_scan/)
- [Thermal False Colour](/reference/shaders/thermal_false_colour/)
- [Vectorscope](/reference/shaders/vectorscope/)
- [Vignette](/reference/shaders/vignette/)
- [Waveform](/reference/shaders/waveform/)
- [Zebra](/reference/shaders/zebra/)
