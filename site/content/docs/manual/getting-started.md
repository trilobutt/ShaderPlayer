---
title: Getting Started
nav_title: Getting Started
---

# Getting Started

Install the download and run it. The installer carries the Visual C++ runtime, FFmpeg and
Qt with it, so there is no prerequisite to chase and nothing to configure before the first
launch.

## What the machine needs

- **Windows 10 version 2004 or newer**, 64-bit. Windows 11 included.
- **A GPU with Direct3D 11 feature level 11_0.** Anything from 2012 onward clears that bar,
  and the renderer is Direct3D 11 throughout, so there is no other platform.

## Running the installer

ShaderPlayer installs per user, into `%LOCALAPPDATA%\Programs\ShaderPlayer`. No
administrator rights, no UAC prompt, and nothing written outside your own profile.

The installer is unsigned, so Windows SmartScreen stops it the first time with **Windows
protected your PC** and a line about an unrecognised publisher. That warning is about the
absence of a code-signing certificate rather than about anything found in the file. Click
**More info**, which reveals the publisher and file name along with a **Run anyway**
button, and the installation proceeds normally.

## The first launch

The window opens on the empty state, with **Open Video** and **Open Stream / Webcam** in
the middle of it. Nothing is playing yet, and the **Shader Library** on the right is
already carrying all forty-five shipped shaders, sorted into Audio Reactive, Generative and
Video Effects: they install beside the executable and are found without being pointed at.

That first launch is the slow one, because the shaders compile as they load, which takes
roughly 3.7 seconds across all cores. Every launch afterwards reads the compiled bytecode
out of `shader_cache/` instead, at around 15 ms for the same forty-five. See
[Shaders and the editor](/docs/manual/shaders-and-editing/) for what invalidates that cache.

## Where your data lives

Everything ShaderPlayer writes sits in the install directory beside the executable:

- `config.json`, holding your presets, parameter values, keyframes, keybindings, window
  layout, noise settings and audio settings.
- `shader_cache/`, the compiled bytecode described above.
- `layouts/`, one `.ini` file per saved workspace.

Deleting `config.json` returns the application to a first run and loses nothing else. Your
shaders are files on disk and are untouched by it.

## Building from source

ShaderPlayer is GPLv3 and the source is at
[github.com/trilobutt/ShaderPlayer](https://github.com/trilobutt/ShaderPlayer). Building it
needs Visual Studio, Qt and a copy of the FFmpeg runtime, and the repository's README has
the current instructions for all three. Nothing in this manual assumes a build tree.
