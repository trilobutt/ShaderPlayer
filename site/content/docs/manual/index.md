---
title: The ShaderPlayer Manual
nav_title: Overview
---

# The ShaderPlayer Manual

ShaderPlayer runs one HLSL pixel shader over a picture and shows you the result at frame
rate, while you edit the shader. The picture can be a video file, a webcam, an RTSP stream,
or nothing at all: a generative or audio-reactive shader makes its own image and needs no
footage open. Recording runs underneath all of it and does not stop when you switch shaders,
edit source, or recompile.

These pages cover using the application. The
[shader reference](/docs/reference/) covers writing shaders for it: the cbuffer contract, the ISF
parameter block, and a page per shipped shader.

## In reading order

- [Getting started](/docs/manual/getting-started/). Installing the download, what the
  first launch does, and where the application keeps your settings.
- [The interface](/docs/manual/interface-tour/). Every panel, what it is for, and the key that
  opens it.
- [Video and live capture](/docs/manual/video-and-capture/). Opening a file, opening a camera or
  a stream, and why a live source behaves differently.
- [Shaders and the editor](/docs/manual/shaders-and-editing/). Applying a shader, editing it
  live, and what the compile cache and the file watcher do behind you.
- [Parameters](/docs/manual/parameters/). The parameter grid, one entry per control type, and
  the video blend section under it.
- [Keyframe animation](/docs/manual/keyframe-animation/). Animating a parameter along the
  timeline, the curve types, and the bezier editor.
- [Recording](/docs/manual/recording/). Writing H.264 or ProRes while everything else keeps
  moving.
- [Spout and the second window](/docs/manual/spout-and-video-output/). Getting the output into
  another application or onto another screen.
- [Workspaces and keybindings](/docs/manual/workspaces-and-keybindings/). Saving a layout,
  binding a shader to a key, and the keys you cannot have.
- [Troubleshooting](/docs/manual/troubleshooting/). The failures that are not shader bugs.

## The one thing worth knowing first

The viewport shows a picture whenever a video is open **or** a shader is active. Those are
independent. Open the application, load nothing, activate Plasma from the Shader Library,
and it renders and animates immediately on its own wall clock: the Transport panel grows an
output resolution control and a running clock, and Play and Pause work on it. Fifteen of the
forty-five shipped shaders are generative and need no footage ever; the eight audio-reactive
ones also draw with nothing open, though their bands read zero until something with an audio
track is playing.
