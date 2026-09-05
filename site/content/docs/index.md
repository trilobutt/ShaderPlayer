---
title: ShaderPlayer Documentation
nav_title: Documentation
---

# ShaderPlayer Documentation

ShaderPlayer runs an HLSL pixel shader over decoded video in real time on
Windows 11, with the editor, the parameter panel, and the recorder all live
while the picture plays. Installation is a download and a double-click: the
build carries its own Qt and FFmpeg runtimes, so there is nothing to compile
and nothing to install beside it.

<figure class="shot">
  <a href="/static/img/interface.png" target="_blank" rel="noopener"><img src="/static/img/interface.png" alt="The ShaderPlayer window running a generative shader: parameters docked on the left, the rendered picture in the centre, the shader library on the right, transport controls along the bottom."></a>
  <figcaption>A generative shader running, with all seventeen of its parameters live beside it. Click the picture for it full size.</figcaption>
</figure>

Two document sets live here. The manual covers the interface and every
workflow the player has: opening a video or a webcam, editing a shader while
it runs, keyframing a parameter against the timeline, and recording the result
to an MP4 or a ProRes MOV. The shader reference covers the cbuffer contract
your own HLSL compiles against, the ISF parameter system behind every widget
in the panel, and each of the forty-five shipped shaders with its full
parameter table.
