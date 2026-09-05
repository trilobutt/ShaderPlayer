---
title: The Interface
nav_title: Interface Tour
---

# The Interface

One window, one central picture, eight dockable panels around it. Every panel can be dragged
to another edge, floated, tabbed behind another, or closed, and the arrangement survives a
restart. A layout worth keeping can be saved as a
[workspace](/docs/manual/workspaces-and-keybindings/).

Each panel carries a colour of its own on two channels: the glyph in its title bar and the
hairline directly above its body. Azure is the Shader Library, violet the Editor, lime the
Parameters, amber the Transport, red the Recording panel, mint the Noise Generator, rose the
Spout Output panel, magenta the Audio Monitor. Nothing in the interface responds to hover or
focus with a colour change, so those hues are the one signal that tells a tabbed stack of
panels apart at a glance.

<figure class="shot">
  <a href="/static/img/interface.png" target="_blank" rel="noopener"><img src="/static/img/interface.png" alt="The ShaderPlayer window with a generative shader running: Shader Parameters docked on the left, the rendered picture in the centre, the Shader Library on the right, the Transport along the bottom."></a>
  <figcaption>The window with the <code>electromagnetic_field</code> shader active. Parameters on the left, the picture in the centre, the Shader Library and Shader Editor tabbed together on the right, the Transport along the bottom. The Parameters dock has been widened from its shipped width so the R and KF columns are not clipped. Click the picture for it full size.</figcaption>
</figure>

## The centre

The central area holds either the **viewport** or the **empty state**, and it switches
between them on one condition: a video is open, or a shader is active. Either is enough.
With neither, you get the empty state, a large ShaderPlayer wordmark over two buttons
(Open Video, Open Stream / Webcam) and a note that you can drag and drop a file onto the
window.

The viewport is a Direct3D surface with its own swap chain, letterboxed to the content's
aspect ratio with black bars. It is the only part of the window Qt does not paint, which has
one visible consequence: a screenshot taken with a window-capture tool that reads Qt's
backing store shows stale pixels there rather than the rendered frame. Capture the screen,
not the window.

<figure class="shot">
  <a href="/static/img/empty-state.png" target="_blank" rel="noopener"><img src="/static/img/empty-state.png" alt="The same window with no video open and no shader active: the centre shows a large ShaderPlayer wordmark over Open Video and Open Stream buttons."></a>
  <figcaption>The empty state, which is what the centre shows when there is no video open and no shader active. The Transport says the same thing in its own words, and Play and Stop are greyed. Click the picture for it full size.</figcaption>
</figure>

## Shader Library (F2)

The list of every loaded shader, in three sections that match the shader's declared type:
**AUDIO REACTIVE**, **GENERATIVE**, **VIDEO EFFECTS**, each with its count. Above the tree
sit **Scan Folder...**, which loads every `.hlsl`, `.fx` and `.ps` file in a directory, and
**+ New**, which writes a new shader from the template and opens it in the editor. A filter
box narrows the list by name.

Clicking a row activates that shader and loads its source into the editor. The first row,
above the sections, is **(No Effect)**: the passthrough that shows the source untouched.
Escape selects it from anywhere.

Each row carries a status dot. A red dot means the shader failed to compile, and **hovering
it shows the compiler's message as a tooltip**. That is the only place the message appears
for a shader you have not opened in the editor. Right-clicking a row offers **Set
Keybinding...** and **Remove**.

<figure class="shot shot--library">
  <a href="/static/img/shader-library.png" target="_blank" rel="noopener"><img src="/static/img/shader-library.png" alt="The Shader Library panel: Scan Folder and New buttons, a filter box, the (No Effect) row, then AUDIO REACTIVE and GENERATIVE sections with their counts and shader names."></a>
  <figcaption>The Shader Library, with the active shader highlighted. Each section header carries its own count, and the first row above them is the passthrough Escape returns to. Click the picture for it full size.</figcaption>
</figure>

## Shader Editor (F1)

The source of the active shader, with HLSL syntax highlighting, a line-number gutter, and
completion over the HLSL intrinsics and types, the uniforms and
[helper functions](/docs/reference/shadercommon-helpers/) the preamble injects, and the current
shader's own parameter names.
**Compile (F5)** compiles what is in the editor into the active shader; the result appears
as `OK` or `Error` beside the button, and a failure prints the compiler message along the
bottom of the panel and marks the offending line in the gutter. Editing also triggers an
automatic compile 500 ms after you stop typing.

Ctrl+S writes the editor's contents back to the shader's file on disk. See
[Shaders and the editor](/docs/manual/shaders-and-editing/) for what that does to the parameter
values, which is not nothing.

## Shader Parameters

The controls the active shader declares, one row each, with a reset arrow, an **R**
randomiser and a **KF** keyframe toggle at the end of every row. **Randomise All** and
**Reset to Defaults** sit at the top. Under the parameters, separated by a rule, is the
**VIDEO BLEND** section: eleven modes and an amount, which decide how the shader's output
meets the video underneath it. [Parameters](/docs/manual/parameters/) covers all of it.

This is the one dock with no close button and no F-key. It cannot be closed, because no
workspace preset records its visibility and there would be no route back.

## Transport (F3)

Play, Stop, a clock, volume and mute, and above them a track that changes with what is open:

- **A video file** gets a scrubber with the playhead, keyframe diamonds for the selected
  parameter, an elapsed and duration clock, and a units button that switches the clock
  between seconds and frame numbers.
- **A live capture** gets a red LIVE badge, a wall-clock elapsed counter and a **Stop
  Capture** button. There is nothing to scrub: the source has no end.
- **A shader with no video open** gets an output resolution control (a preset list, plus a
  width and height pair when you choose Custom) and a wall clock counting how long the
  shader has been running.
- **Nothing open and no shader** gets a line saying so.

Play and Stop are enabled in the first three cases and greyed in the fourth.

## Recording (F4)

Output path with a **Browse...** button, codec (H.264 in MP4, or ProRes in MOV), and either
a bitrate in Mbps or a ProRes profile depending on which codec is selected. The large button
at the bottom starts and stops. While armed, the panel grows a pulsing red border, a REC
badge, an elapsed clock, the destination, and counters for frames written and frames dropped.
It also raises itself to the front of whatever tab stack it sits in, since a recording that
has just started is the one thing that must not be hidden. See
[Recording](/docs/manual/recording/).

## Noise Generator

Every shader is handed a noise texture whether it asks for one or not: Perlin in the red
channel, Voronoi in the green. This panel owns its two settings, **Scale** and **Size**
(256, 512 or 1024 square, 512 by default), and shows a live preview of each channel read
back off the GPU rather than re-simulated, so the preview cannot drift from what the shaders
sample. **Regenerate** rebuilds it. The authoring side is on
[the noise texture reference page](/docs/reference/noise-texture/).

<figure class="shot shot--noise">
  <a href="/static/img/noise-generator.png" target="_blank" rel="noopener"><img src="/static/img/noise-generator.png" alt="The Noise Generator panel showing the red-channel Perlin preview and the green-channel Voronoi preview side by side, above a Scale slider, a Size combo and a Regenerate button."></a>
  <figcaption>The Noise Generator, tabbed alongside the Transport and Recording panels. Both previews are read back off the GPU, so they show the texture the shaders are actually sampling. Click the picture for it full size.</figcaption>
</figure>

## Spout Output (F8)

A checkbox to share every rendered frame as a Spout sender, a sender name that receivers
pick the stream out of their source list by, and a status block reading SENDING, WAITING,
UNAVAILABLE or OFF. See [Spout and the second window](/docs/manual/spout-and-video-output/).

## Audio Monitor

Live meters for the six analyser bands (RMS, Bass, Mid, High, Beat, and the spectral
centroid), a 256-bin spectrum display, and three settings that change how the analyser
behaves: **Beat Sensitivity**, **Beat Decay** and **Smoothing**. The meters show exactly the
numbers an audio-reactive shader reads, so this panel is how you find out whether a shader
that is not moving has a bug or simply has no signal. The
[audio reference](/docs/reference/audio-and-spectrum/) gives the ranges.

## Video Output Window (F7)

Not a dock: a second top-level window on the same GPU device, showing the same rendered
frame. Useful for a projector or a second monitor. F7 toggles it, and so does View, Video
Output Window.

## The menus

**File** opens a video (Ctrl+O), opens a stream or webcam, closes the video, saves the shader
(Ctrl+S) or saves it under a new name, and exits.

**View** toggles each dock (Shader Editor F1, Shader Library F2, Transport Controls F3,
Recording Panel F4, Noise Generator, Spout Output F8, Audio Monitor), opens the Keybindings
reference (F6), toggles the Video Output Window (F7), and holds the Workspace Presets
submenu.

**Shader** creates a new shader, compiles (F5), and resets to passthrough (Escape).

**Recording** starts and stops recording (F9) and shows or hides the Recording panel.

The keys printed against menu items are shown rather than bound there: every key in the
product is dispatched from one place, so a menu accelerator is a label, not a second route
to the action. The full table, including which keys you may not bind to your own shaders, is
in [Workspaces and keybindings](/docs/manual/workspaces-and-keybindings/).
