---
title: ShaderPlayer
---

<!--# note
  The product page's words. Each marker comment below opens a named slot, and
  everything up to the next marker is that slot's copy; templates/product.html
  claims them by name and supplies the layout around them. Markdown passes an
  HTML comment through untouched, which is what lets the marker survive the
  render. Rename a slot here and the template stops finding it, so the two
  files change together.

  Slots, in the order the page prints them: note (discarded), hero, sources,
  editing, parameters, output, shaders, pricing, requirements, licence. The
  screenshots, the price figure, and the buttons live in the template; only
  sentences live here.
-->

<!--# hero -->

# ShaderPlayer

A real-time HLSL shader player for Windows: video files, webcams, and network
streams run through a Direct3D 11 pixel shader you can rewrite while it is still
playing.

<!--# sources -->

## Anything FFmpeg decodes, plus a webcam and a stream

The pipeline takes one frame, binds it at `t0`, and draws it through your pixel
shader. Where the frame comes from is the part that varies: an MP4, MOV, MKV, or
ProRes file, a DirectShow webcam, an RTSP, RTMP, or HTTP stream, or nothing at
all, because a generative shader makes its own picture and runs with no source
open.

Audio decodes alongside the picture and plays through WASAPI, with volume and
mute in the transport bar. A video blend mode composites a generative or
audio-reactive shader over live footage at an amount you set, and the shader's
own output alpha decides which pixels the video shows through.

<!--# editing -->

## Recompile while it is still playing

F5 compiles what is in the editor and the next frame is drawn with it. Playback
does not stop, a recording in progress does not stop, and the parameter values,
keyframes, blend setting, and keybinding all survive the swap. Leave the editor
idle for 500 ms after a change and it compiles on its own; save the file in
Notepad++ or VS Code instead and the file watcher picks it up within half a
second.

Compiled bytecode is cached on disk against an FNV-1a hash of the full source,
generated preamble included. Loading forty-five presets from cold costs about
3.7 seconds. Warm, the same load costs about 15 milliseconds.

<!--# parameters -->

## The shader declares its own controls

A JSON block at the top of the file lists a shader's inputs, and ShaderPlayer
builds the panel from it: a slider for a float, a dropdown for an enumeration,
an RGBA picker for a colour, a draggable pad for a 2D point, a button for a
one-frame event, and a read-only meter for an audio band. The body reads each
one by name, so nothing indexes into a constant buffer by hand. Thirty-two
floats are available across all of a shader's parameters.

Any of them can be keyframed against playback time, with linear, ease in and
out, or a cubic bezier you shape by dragging two handles in the panel. Keys show
as diamonds on the transport scrubber and can be dragged there. Values, curves,
and keys are written to `config.json` and restored on the next launch.

<!--# output -->

## Record the preview, or render it frame by frame

Recording writes the processed picture to H.264 or H.265 in an MP4, or ProRes in
a MOV, and muxes the source file's audio alongside it. Start a recording with a
file open and playback rewinds to zero and becomes a render: exactly one frame
written per decoded frame, so a shader too heavy for real time costs wall-clock
time rather than dropped frames. A generative shader records at the frame rate
you set in the Recording panel, 25 by default, with the shader's own clock
stepping to match.

Two live outputs run at the same time as the preview. F7 opens a second
resizable window on the same D3D11 device, for a projector or a second monitor.
F8 publishes each frame as a Spout2 sender, which Resolume, MadMapper, and
TouchDesigner read directly.

<!--# shaders -->

## Forty-five shaders, all of them source

Eight audio-reactive, fifteen generative, and twenty-two video effects ship with
the player, and every one is a plain `.hlsl` file you can open and change. The
video effects cover the broadcast scopes (waveform, vectorscope, RGB parade,
zebra, focus peaking, and safe-area guides), a CRT emulation with barrel
distortion and a phosphor mask, and a Kuwahara structure-tensor oil-paint
filter. The generative set runs from Conway's Game of Life to a Poincaré disc
tiling of any valid Schläfli symbol and a ray-marched Mandelbulb.

The audio-reactive shaders read a live FFT: RMS, bass, mid, high, beat, and
spectral centroid arrive as uniforms, alongside a 256-bin spectrum texture.
Every shipped shader has a [reference page](/docs/reference/) listing its
parameters with their types, defaults, and ranges.

<!--# pricing -->

## What twenty dollars gets you

There is no subscription, no renewal, and no licence server to check in with.
Buy it, download the installer, and run it.

- The Windows installer, with the Qt and FFmpeg runtimes already inside it
- All forty-five shaders as editable HLSL source
- Every 1.x update at no further cost

The current build is version 1.0.0, a 92 MB installer for 64-bit Windows.

<!--# requirements -->

## What it needs to run

Windows 10 version 2004 or later, 64-bit, with a Direct3D 11 GPU. Shaders
compile at Shader Model 5.0, which every feature level 11_0 card provides.

<!--# licence -->

## GPLv3, and the source is public

ShaderPlayer is licensed under the GNU General Public License version 3, and the
whole source tree sits on
[GitHub](https://github.com/trilobutt/ShaderPlayer). The GPL grants you the
right to study it, change it, and pass it on, and buying a copy takes none of
that away.

What twenty dollars buys is the built and packaged installer, so that running
ShaderPlayer costs you neither a Visual Studio install, a Qt SDK, an FFmpeg
build, nor the afternoon of getting all three to agree. It also pays for the
time that goes into the next version.
