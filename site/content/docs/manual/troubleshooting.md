---
title: Troubleshooting
nav_title: Troubleshooting
---

# Troubleshooting

The failures worth a page are the ones that do not announce themselves: a shader that
vanishes from the library without an error, a parameter that reverts to its default while
you are working, a webcam that never opens and never says why. Each has a specific check.

## The library is empty

The shader directory recorded in `config.json` has moved, been emptied, or been deleted,
and the application scans it at startup without inventing a replacement. Point it at one
again: Shader Library, **Scan Folder...**, then the `shaders` folder in the install
directory. The choice is written to `config.json` at once and reused on every later launch.
See [Getting started](/docs/manual/getting-started/).

## A shader did not appear after Scan Folder

It appeared, and it failed to compile. Look for a **red status dot** on its row in the Shader
Library and hover it: the compiler's message is the tooltip. That is the only place the
message is shown for a shader you have not opened in the editor.

The compile errors that confuse people are the ones that name a parameter rather than a line
of HLSL. `X3004: undeclared identifier 'Strength'` on a parameter that is plainly declared in
the ISF block means the block did not parse, so no `#define` was generated for it. Five
things cause that, and all five are silent by design. They are listed on
[the ISF block reference page](/docs/reference/isf-block-and-parameters/).

## A parameter's alias is undeclared and the block looks fine

This is the one worth knowing in advance, because nothing on screen points at it.
Parameters are packed into a 32-float budget in declaration order. When a parameter does not
fit, **it and every parameter after it are dropped without a warning**, and the shader then
fails to compile on the first dropped name. The message names an identifier that is right
there in your ISF block, which sends people looking for a typo that does not exist.

A colour costs four floats and aligns to a four-float boundary, a point costs two and aligns
to an even one, everything else costs one. Six colours and a handful of floats reaches the
limit faster than it sounds.

The offline validator is the fastest way to see it, because it prints the float count for
every shader and exits non-zero on overflow:

```
python tools/validate_shaders.py path/to/shader.hlsl
```

## Parameter values reset themselves

A shader file changed on disk is re-read and recompiled within half a second, and that reload
is wholesale: **the parameter values go back to the ISF defaults and the keyframe timelines
are dropped**, keeping only the keybinding. Editing a shader in an external editor while
ShaderPlayer has it loaded therefore costs you the values you had dialled in.

Editing inside ShaderPlayer and pressing F5 keeps your values, because that path saves and
restores them by name. It still drops the keyframes. Values you want permanently belong in
the shader's `DEFAULT` fields. See
[Shaders and the editor](/docs/manual/shaders-and-editing/).

## The parameters panel disagrees with the picture

Same cause. The panel does not rebuild itself after a file reload, so its widgets keep
showing the values from before while the shader is running on the defaults. Click the shader
in the library again and the panel is rebuilt from what is actually loaded.

## The webcam list is empty, or a capture never opens

Two possibilities. The dull one is that the device is unplugged or held open by another
application, which a video-conferencing tool in the background does routinely. **Search
Again** in the capture dialog re-enumerates without closing it.

The structural one: ShaderPlayer loads `avdevice-*.dll` on demand rather than linking it,
because it is the most expensive FFmpeg library to map and nothing but capture needs it.
Without that DLL beside the executable, device registration fails, every capture open fails
silently, and everything else keeps working. Check that `avdevice-*.dll` is in the same
directory as `ShaderPlayer.exe`. The installer puts the whole FFmpeg package there, so a
missing one means the installation has been damaged: reinstall over the top of it.

## The application will not launch: a DLL was not found

`avcodec-62.dll was not found` or similar means the FFmpeg runtime is no longer beside the
executable. An installed copy cannot reach that state on its own, so something has removed
the file since: an antivirus quarantine, a half-finished uninstall, or a copy of the
install directory that left part of itself behind. Run the installer again over the
existing installation, which replaces the runtime and keeps your `config.json`.

## A saved workspace does nothing

Layout restoration is atomic. A preset saved under a different set of panels, or a file whose
layout blob is corrupt, is refused wholesale and the built-in Default is applied instead,
which is why nothing appears to happen. Rearrange the window and save the workspace again.

## The first launch after editing ShaderCommon.hlsli is slow

Expected. Compiled shader bytecode is cached in `shader_cache/` keyed on the whole compiled
text, and `ShaderCommon.hlsli` is prepended to every shader, so touching it invalidates the
entire cache at once. The next launch recompiles all forty-five, which costs a few seconds,
and the one after that is back to milliseconds.

To force a clean compile, delete `shader_cache/`. There is no switch that disables the cache,
and there is nothing a stale entry can do: the key covers the complete source, so a file that
differs by one character misses.

## A screenshot shows the empty state under a running shader

A capture artefact, not a bug. The viewport is a native Direct3D surface that Qt does not
paint into, so a window-capture tool reading Qt's backing store finds whatever was last there.
Capture the screen instead of the window.

## The picture is untreated with a shader selected

The active shader failed to compile, and the renderer falls back to passthrough rather than
drawing nothing. Check the editor's status line, or the red dot on its library row, for the
message.
