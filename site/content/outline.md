# ShaderPlayer Site: Page Outline

The site has two document sets: the manual (B4, hand-written) and the shader
reference (B5, mostly generated from `site/content/shaders.json`, written by
`site/tools/extract_shaders.py`). This file is the specification both write
from; do not start either without it.

Each entry below states the page's path, what a reader should be able to do
after reading it, its source material (file plus section, so the writer reads
one pass rather than the whole repository), and whether it is hand-written or
generated.

---

## Manual (B4), all hand-written

### `manual/installation.md`
Reader can get from a fresh clone to a running `ShaderPlayer.exe`.
Source: `CLAUDE.md` §§ "Prerequisites", "FFmpeg Setup", "Building", "When the
cache goes wrong". Cover the FFmpeg DLL step explicitly, since it is the one manual
action a build cannot skip.

### `manual/interface-tour.md`
Reader can name every dock, know what it is for, and know the keyboard route
to open it (F1–F9).
Source: `CLAUDE.md` § "Qt UI Structure" (dock list, `RegionDock` hue
convention, the refresh contract) and § "Application API" for
`FindBindingConflict`'s reserved F-key table. One subsection per dock:
Editor, Library, Params, Transport, Recording, Noise, Spout, Audio, Video
Output Window.

### `manual/video-and-capture.md`
Reader can open a file, open a webcam or RTSP source, and understand why live
capture looks and behaves differently from file playback.
Source: `CLAUDE.md` § "Live Capture (Webcam / RTSP)" in full (device
enumeration, wall-clock timing, the LIVE badge) and § "VideoDecoder API".

### `manual/shaders-and-editing.md`
Reader can apply a shipped shader, edit it live with F5 recompile, and
understand hot reload and the bytecode cache well enough not to fight it.
Source: `CLAUDE.md` §§ "Shader Compile Path", "Shader Bytecode Cache",
`ShaderManager::CheckForChanges` note under "ShaderManager API".

### `manual/parameters.md`
Reader can read the parameter grid, use every widget type, and know why a
widget is disabled or greyed out (keyframe playback, dead parameters).
Source: `docs/shader-parameter-guide.md` (author-facing, but the widget
behaviour section applies to users too) and `CLAUDE.md` § "Shader Parameter
System" → "Value Storage and GPU Upload", "Randomiser".

### `manual/keyframe-animation.md`
Reader can enable keyframing on a parameter, place and edit keys, choose an
interpolation mode, and use the bezier editor.
Source: `CLAUDE.md` § "Shader Parameter System" → "Keyframe Animation" in
full (data model, evaluation pipeline, UI, persistence, the reposition
pattern).

### `manual/recording.md`
Reader can start and stop a recording without corrupting the output file, and
understands why recording framerate matches playback framerate.
Source: `CLAUDE.md` §§ "VideoEncoder Notes", "Render Loop (RenderFrame)" step
6, "Known Limitations" (recording framerate line).

### `manual/spout-and-video-output.md`
Reader can send output to Spout and open a second output window, and knows
the two are independent toggles.
Source: `CLAUDE.md` §§ "Spout2 Integration (SpoutOutput)",
"VideoOutputWindow.{cpp,h}" entry in the component list.

### `manual/workspaces-and-keybindings.md`
Reader can save and load a workspace layout, understands what a preset does
and does not restore, and can rebind a shortcut without creating a conflict.
Source: `CLAUDE.md` §§ "Workspace Presets" (Configuration section) in full,
"Keybindings" (Configuration section), `KeyMap.h` note under "Qt Notes".

### `manual/troubleshooting.md`
Reader can self-diagnose the failure modes that are not a shader bug: a
build/cache issue, a missing DLL, a preset that silently vanished, a stale
layout.
Source: `CLAUDE.md` § "When the cache goes wrong" in full, "Diagnosing
missing shaders" under "Cbuffer Packing Rules", `AVFMT`/`avdevice` note under
"Live Capture" (webcam open fails silently).

---

## Shader Reference (B5)

### `reference/index.md` (hand-written)
Reader lands on the reference and can get to any of the three shader groups
or any topic page in one click.
Source: `shaders.json` for the counts and grouping (`type` field), `CLAUDE.md`
§ "Shader System" for the group names. List the three sections the app
itself uses (**Audio Reactive**, **Generative**, **Video Effects**) as the
top-level grouping, each linking to its generated shader pages
(`reference/shaders/<name>.md`), in that order. This is the natural grouping
because it is the one the Shader Library panel already shows the user; a
different taxonomy here would teach a second, contradictory one.

### `reference/cbuffer-contract.md` (hand-written)
Reader can write a shader that compiles against the real pipeline on the
first try: the exact `Constants` and `AudioConstants` layouts, register
bindings, and the entry point signature.
Source: `CLAUDE.md` § "Shader System" (the full HLSL block quoted there) and
§ "Audio Data (b1 / t3)".

### `reference/isf-block-and-parameters.md` (hand-written)
Reader can write a correct ISF block for every parameter type and predict
exactly which `custom[]` slot and `#define` alias each one produces.
Source: `docs/shader-parameter-guide.md` in full (this is its natural home;
link out to it rather than duplicating), plus `CLAUDE.md` §§ "ISF JSON Block
Parsing", "#define Alias Generation", "Cbuffer Packing Rules". State the
`MIN`/`MAX`/`STEP` `is_number()` rule explicitly, since it is the one
surprise a shader author hits without warning (`docs/shader-parameter-guide.md`
§ "Rules and Gotchas" already has the one-line version; expand it here with
the worked example from `CLAUDE.md`'s packing table).

### `reference/audio-and-spectrum.md` (hand-written)
Reader can build an audio-reactive shader: which bands exist, what range
their values fall in, and how to sample the spectrum texture.
Source: `CLAUDE.md` § "Audio Data (b1 / t3)" and the "Audio band values"
line under `docs/shader-parameter-guide.md` § "Audio Parameters".

### `reference/noise-texture.md` (hand-written)
Reader can sample the global noise texture correctly and knows which channel
carries which noise type.
Source: `CLAUDE.md` § "Global Noise Texture (t1 / s1)" in full, including the
per-cell UV pattern line.

### `reference/shadercommon-helpers.md` (hand-written)
Reader knows every helper function available to every shader without opening
`ShaderCommon.hlsli`, and where each is useful.
Source: `src/ShaderCommon.hlsli` (read the file directly for signatures, since the
skeleton listing in `CLAUDE.md` § "Shared HLSL Helper Library" names the
groups but not every signature) plus that same `CLAUDE.md` section for the
preamble injection order and the `#line` directive note.

### `reference/sampling-and-derivatives.md` (hand-written)
Reader knows why `Sample`/`SampleLevel`/`SampleGrad` are identical here, when
`fwidth`/`ddx` will silently misbehave, and the analytic-footprint workaround.
Source: `CLAUDE.md` § "Sampling and Screen-Space Derivatives" in full,
including the three named worked examples (`game_of_life.hlsl`'s `pxCell`,
`crt_simulation.hlsl`'s `maskW`, `kaleidoscope.hlsl`'s angular footprint),
pulling the actual code for each from those files rather than re-describing it.

### `reference/gotchas.md` (hand-written)
Reader has a single page of every documented HLSL/build footgun so they do
not rediscover one by compile error.
Source: `CLAUDE.md` § "C++ / Dependency Gotchas" (the HLSL-relevant subset:
intrinsic shadowing, `atanh`, ISF `long` `VALUES`/`LABELS`, audio band scale,
offset-based effect strength baseline) plus `docs/shader-parameter-guide.md`
§ "Rules and Gotchas".

### `reference/shaders/<name>.md` (one per shader, generated)
Reader can decide whether a shader fits their need and knows every parameter
it exposes without opening the `.hlsl` file.
Source: `site/content/shaders.json`, one entry per shader (45 pages: 8 audio,
15 generative, 22 video, per the current extraction). Each page renders:
- Title, type badge, description (`title`, `type`, `description`).
- A parameter table: label, type, default, min/max/step (omitted or marked
  not applicable for `audio`-type parameters, `null` shown as "app default"
  rather than as a bound), and the `values`/`labels` pairs for `long`
  parameters.
- The packed `custom_floats` / 32 total and `param_count`, phrased the same
  way `validate_shaders.py` phrases it, so a reader cross-referencing a
  compile-time warning recognises the number.
- A link back to `reference/index.md`'s matching group section.

The generator (B2) must re-run `site/tools/extract_shaders.py` rather than
hand-editing `shaders.json`, and must fail its own build if the script exits
non-zero. The two must never drift, which is the whole reason the inventory
is extracted rather than transcribed by hand.
