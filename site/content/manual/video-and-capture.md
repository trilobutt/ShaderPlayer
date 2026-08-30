---
title: Video and Live Capture
nav_title: Video and Capture
---

# Video and Live Capture

Three ways in: a file, a DirectShow capture device, or a URL. A shader treats all three
identically, because by the time it runs the source is one RGBA texture bound at `t0`. What
differs is the transport, and the difference is real enough to be worth knowing before you
plug a camera in.

## Opening a file

**File, Open Video...** (Ctrl+O), the **Open Video** button on the empty state, or drag the
file onto the window. The dialog filters to `.mp4`, `.mov`, `.avi`, `.mkv`, `.webm` and
`.mxf`, with an All Files entry for anything else FFmpeg can demux. Dropping a file skips the
filter entirely: any file whose extension is not `.hlsl`, `.fx` or `.ps` is handed to the
decoder, and if the decoder refuses it you get a toast saying so.

An opened video lands **paused on its first frame**, not playing. Press Space or the Play
button to start it. Playback loops: at the end of the stream the decoder seeks back to zero
and keeps going, so a short clip runs indefinitely under a shader you are still writing.

The path is remembered in `config.json` and reopened on the next launch.

**Stop** returns to the first frame and leaves the transport stopped. **File, Close Video**
releases the source entirely; if a shader is active the viewport stays live and the Transport
panel switches to its generative page, and if none is active you are back at the empty state.

## Playback and audio

Space toggles play and pause from anywhere except a focused text field, where the space is
yours to type. The scrubber seeks on drag. The clock beside the transport buttons reads
elapsed and total; clicking the unit label next to it switches both to frame numbers, using
the source's own frame rate, and that preference persists.

Audio plays through WASAPI with volume and mute in the transport bar, both persisted. There
is no separate audio track selector: the first audio stream in the file is decoded, mixed to
mono for the analyser, and resampled to the output device rate if they differ. A file with no
audio track plays silently, the Audio Monitor shows its no-source page, and any
audio-reactive shader draws its idle form with every band at zero.

Seeking flushes the audio buffer, which is what keeps sound aligned with the picture after a
scrub instead of playing a second of the old position first.

## Webcams and streams

**File, Open Stream / Webcam...** opens a dialog with two halves.

The upper half lists DirectShow video devices. It enumerates on opening, and **Search Again**
re-enumerates, which is what you want after plugging a camera in with the dialog already
open. Select one and press **Open Device**.

The lower half takes a URL: RTSP, RTMP or HTTP. Type it and press **Open URL**.

Either way, a source that will not open leaves the dialog where it is with a line naming what
was tried and the three things that usually explain it: not connected, already in use by
another application, or not reachable. A camera held open by a video-conferencing app in the
background is the common one.

## Why live capture behaves differently

A live source has no beginning and no end, so several things that make sense for a file make
none for it:

- **The transport shows a LIVE badge, a wall-clock counter and a Stop Capture button**
  instead of a scrubber. There is nothing to seek to.
- **Timing comes from the wall clock, not from frame timestamps.** Capture devices start
  their clocks at arbitrary values, so the shader's `time` uniform counts seconds since the
  capture opened.
- **A frame that is not ready yet is not an error.** The decoder is non-blocking; when the
  device has nothing new, the last frame stays on screen and the loop carries on. A camera
  running at 30 fps under a 60 fps render loop is normal and shows every frame twice.
- **Stop and seek do nothing.** They fail quietly rather than being greyed out.

Capture opens playing, unlike a file.

## When no capture device appears

The device list can be empty for a mundane reason (nothing is plugged in) or a structural
one. ShaderPlayer loads `avdevice-*.dll` on demand rather than linking it, because it is the
most expensive FFmpeg library to map and nothing but capture ever needs it. When that DLL is
missing from the runtime directory, device registration fails, **every capture open fails
silently**, and the rest of the application works exactly as before. If the list is empty
with a camera definitely attached and definitely free, check that `avdevice-*.dll` sits
beside `ShaderPlayer.exe`; the [FFmpeg step](/manual/installation/) copies every DLL in the
package precisely so that this one is not left behind.

## Resolution, with and without a source

With a video or a capture open, the shader renders at the source's own dimensions and the
`videoResolution` uniform carries them.

With nothing open, there is no source resolution to inherit, so the Transport panel's
generative page decides it: a preset list (with a Custom entry that reveals a width and
height pair) whose value is written to `config.json` and used for the render target, for
`videoResolution`, and for the frame size of any recording started in that state.

The `resolution` uniform is a third thing again: it is the size of the viewport panel in
pixels, which changes when you resize the window and has no relationship to either of the
above. The [cbuffer contract](/reference/cbuffer-contract/) spells out which to use for what.
