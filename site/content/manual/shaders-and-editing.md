---
title: Shaders and the Editor
nav_title: Shaders and Editing
---

# Shaders and the Editor

Pick a shader in the library and it is applied on the next frame. Edit its source, press F5,
and the change is on screen before your finger leaves the key. That loop is the product, and
everything below is what sits behind it.

## Loading and applying

**Scan Folder...** in the Shader Library reads every `.hlsl`, `.fx` and `.ps` file in a
directory, parses each one's ISF block, and compiles the batch across all cores. Files
already in the library are skipped, so scanning the same folder twice is harmless and
scanning a second folder adds to the list rather than replacing it. The chosen folder is
written to `config.json` immediately and rescanned on the next launch.

Dropping a single `.hlsl`, `.fx` or `.ps` file onto the window loads it and makes it active
in one gesture.

Clicking a row applies that shader and opens its source in the editor. **(No Effect)** at the
top of the list, Escape from anywhere, or Shader, Reset to Passthrough all return to
passthrough, where the source is drawn untouched.

A shader can also be bound to a key of its own, which is the fast way to cut between looks
while recording. See [Workspaces and
keybindings](/manual/workspaces-and-keybindings/).

## Writing a new one

**+ New** in the library (or Shader, New Shader...) asks for a name and creates a shader from
the template, complete with the cbuffer declarations, an ISF block and a working body. It
opens in the editor immediately.

The new shader exists in the library only: **it has no file on disk until you save it**, and
a shader with no file is not written to `config.json`, so closing the application loses it.
Press Ctrl+S, which for an unsaved shader opens the Save As dialog, and pick a location
inside your shader folder.

## Compiling

Three routes, all the same compile:

- **F5** from anywhere, including while the cursor is in the editor.
- **Compile (F5)** at the top of the Editor panel.
- **Automatic**, 500 ms after the last keystroke. This is on by default; the switch and the
  delay are the `autoCompileOnSave` and `autoCompileDelayMs` keys in `config.json`.

Success shows a green `OK` beside the button. Failure shows a red `Error`, prints the
compiler's message along the bottom of the panel, and marks the reported line in the gutter.
Line numbers refer to your file: the compiler is handed a preamble ahead of your source, and
a `#line` directive resets the counter so the numbers are not inflated by it.

A shader that fails to compile stays in the library with a red status dot, and the message is
the tooltip on that dot. While a failed shader is active the renderer falls back to
passthrough, so the picture goes untreated rather than black. A shader whose error is
`X3004: undeclared identifier` on one of its own parameter names is almost always an ISF
block problem rather than a body problem; [the parser's silent
failures](/reference/isf-block-and-parameters/) lists the five ways that happens.

**Compiling rebuilds the parameter list from the ISF block.** Current values survive it: they
are saved by name across the recompile and written back to any parameter that still exists
under the same name. Keyframe timelines do not survive it. Every timeline on the shader is
dropped by a compile, so set your keys after the source is settled, not while you are still
editing it. Values and keyframes are both restored from `config.json` at the next launch.

## Saving, and what the file watcher does with it

Ctrl+S writes the editor's contents to the active shader's file. **File, Save Shader As...**
writes it somewhere new and appends `.hlsl` when you leave the extension off.

Every preset with a file on disk is watched. The check runs at most twice a second, and a
file whose modification time has changed is re-read and recompiled with no further prompt.
That is what makes an external editor work: keep a shader open in your usual editor, save,
and the picture updates.

There is a cost to know about. A reload is a wholesale replace: **the shader's parameter
values are reset to the defaults its ISF block declares, and its keyframe timelines are
dropped**, with only the keybinding preserved. The Shader Parameters panel does not rebuild
itself on a reload either, so its widgets keep showing the old values until you reselect the
shader in the library. Editing inside ShaderPlayer and pressing F5 keeps your values; editing
outside it does not. Dialled-in values you want to keep belong in the shader's `DEFAULT`
fields.

## The bytecode cache

Every compile is keyed on a hash of the full source, preamble included, and the resulting
bytecode is stored in `shader_cache/` beside the executable. A hit skips compilation
entirely. This is the difference between a cold start and a warm one: forty-five shaders cost
about 3.7 seconds to compile from cold and about 15 ms from cache.

Two consequences follow. Editing `src/ShaderCommon.hlsli` changes the preamble of every
shader in the product and so invalidates the whole cache at once, which is why the next
launch after touching that file is slow. And a cache entry is never a reason for a stale
result: the key covers the entire compiled text, so a source that differs by one character
misses. If you want to force a clean compile, delete `shader_cache/` rather than looking for
a switch to turn the cache off.

## Validating without launching

The offline validator reproduces the exact preamble the application injects, compiles with
`fxc` at the same optimisation level, reports the packed parameter float count, and exits
non-zero on any failure:

```
python tools/validate_shaders.py                  # every shader in default_shaders/
python tools/validate_shaders.py path/to/one.hlsl
python tools/validate_shaders.py --dump path/to/one.hlsl
```

`--dump` prints the combined preamble and source, which is the text the compiler actually
sees. Running `fxc` on a shader file directly instead tells you nothing useful: without the
preamble every parameter name is an undeclared identifier.
