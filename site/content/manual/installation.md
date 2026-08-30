---
title: Installation
nav_title: Installation
---

# Installation

ShaderPlayer builds from source on Windows with one manual step: the FFmpeg runtime DLLs
are roughly 220 MB and are not in the repository, so you download them once and drop them
into `third_party/ffmpeg/bin/`. Everything else the build finds or fetches for itself.

## What the machine needs

- **Windows 11.** Windows 10 may work and is untested. The renderer is Direct3D 11, so
  there is no other platform.
- **Visual Studio 2022 or later**, MSVC. Build Tools alone are enough. The build depends on
  Qt's `msvc2022_64` package, so MinGW and clang are not alternatives here.
- **Qt 6.9 or later, msvc2022_64.** `CMakePresets.json` names `C:/Qt/6.9.1/msvc2022_64`;
  where Qt lives elsewhere the configure falls back to `QT_ROOT_DIR` or `QTDIR` in the
  environment, then to the newest `Qt/6.*/msvc*_64` on the system drive.
- **CMake 3.21 or later.** The copy bundled with Visual Studio counts, along with its Ninja.
- **Perl**, which KSyntaxHighlighting requires at configure time. Git for Windows ships one
  in `usr/bin`, so a machine with Git already has it even though that directory is not on
  `PATH`.

extra-cmake-modules, KSyntaxHighlighting, miniaudio, KissFFT and the Spout2 SDK are fetched
and built by CMake. There is nothing to install for any of them.

## The FFmpeg DLLs

The headers and import libraries are committed to the repository under
`third_party/ffmpeg/` (about 3 MB). The runtime DLLs are not.

1. Download a **shared** build from [gyan.dev](https://www.gyan.dev/ffmpeg/builds/), the
   `ffmpeg-release-full-shared.7z` package.
2. Copy every DLL from its `bin` directory into `third_party/ffmpeg/bin/`:

   ```
   xcopy "C:\path\to\ffmpeg\bin\*.dll" "third_party\ffmpeg\bin\"
   ```

3. CMake discovers `third_party/ffmpeg/` on its own. No flags.

The copy happens at build time, and the build fails with a named error when that directory
holds no DLLs. That check exists because the alternative is an executable that links cleanly
and then dies at launch with `avcodec-62.dll was not found`. Adding the DLLs afterwards needs
a rebuild, not a reconfigure.

To use an FFmpeg installed elsewhere on the system, pass `-DFFMPEG_ROOT=<path>` to CMake and
leave `third_party/ffmpeg/` empty.

## Building

```
pwsh -File tools/build.ps1
```

That script is the whole build. It locates Visual Studio with `vswhere`, finds CMake, Ninja
and Qt on the machine, assembles the environment in the order that environment has to be
assembled in, configures the `windows-msvc` preset when `build/CMakeCache.txt` is missing,
and builds. On a fresh clone with the DLLs in place it is the only command you need.

`-Target shaderfx` builds the headless renderer alone, which skips Qt, moc and the Qt
deployment step entirely.

By hand, it is two commands from a shell where the MSVC environment has already been
initialised:

```
cmake --preset windows-msvc
cmake --build build
```

`windows-msvc` is Ninja and single-config, so there is no `--config` flag and the output
lands at `build/ShaderPlayer.exe`. From a plain shell with no MSVC environment the link
fails with `memcpy` unresolved; run the script instead, or open a Developer Command Prompt.

There is a `windows-msvc-debug` preset that targets a separate `build-debug/`. Use it to
step through code. **Never hand a Debug build to anyone**: it links `ucrtbased.dll` and
`msvcp140d.dll`, which the Qt deployment step does not copy and which only exist on machines
with the Windows SDK installed.

## First run

Run `build/ShaderPlayer.exe`. The window opens on the empty state, with **Open Video** and
**Open Stream / Webcam** in the middle of it, and the Shader Library on the right saying it
has no shaders loaded. That is expected: the library starts empty because the shader
directory it looks in (`shaders`, relative to the working directory, falling back to a
`shaders` folder beside the executable) is empty on a fresh clone.

The forty-five shipped shaders live in `default_shaders/`. Load them once:

1. In the **Shader Library**, press **Scan Folder**.
2. Choose the repository's `default_shaders` directory.

Every `.hlsl`, `.fx` and `.ps` file in it is read, compiled and sorted into the three
sections: Audio Reactive, Generative and Video Effects. The folder you chose is written to
`config.json` immediately, so the next launch scans it without being asked.

The first scan is the slow one. Forty-five shaders compile in roughly 3.7 seconds across all
cores from cold; afterwards the bytecode cache in `shader_cache/` reduces that to about
15 ms. See [Shaders and the editor](/manual/shaders-and-editing/) for what that cache does
and when it is invalidated.

`config.json` is written next to the executable and holds your presets, keybindings, window
layout, noise settings and audio settings. Deleting it resets the application to a first run
and loses nothing else.

## Where things go wrong

Two build failures produce an executable that links and runs, so they are worth recognising
rather than debugging from symptoms: a `CMakeCache.txt` written by something other than the
preset, and one whose compiler flags have been blanked. Both are in
[Troubleshooting](/manual/troubleshooting/), along with the fix for each, which is not the
same fix.
