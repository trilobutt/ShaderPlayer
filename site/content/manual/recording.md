---
title: Recording
nav_title: Recording
---

# Recording

Recording writes the finished picture, shader and all, to an H.264 MP4 or a ProRes MOV. It
runs on its own thread behind the render loop, and **nothing you do to the shader interrupts
it**: switch presets, edit source, recompile, randomise every parameter, drag a keyframe. The
recording keeps going and captures all of it. That is the point of recording here rather than
in a screen grabber.

## Setting up

The Recording panel (F4, or Recording, Recording Settings...) holds four things:

- **Output**, a path with a **Browse...** button beside it. Type it or pick it; the field
  shows the middle of a long path elided and the full thing as a tooltip.
- **Codec**: *H.264 (MP4)*, small and playable anywhere, or *ProRes (MOV)*, far larger and
  built to survive further editing.
- **Bitrate**, for H.264 only: 5 to 100 Mbps in steps of 5, 20 by default.
- **Profile**, for ProRes only: Proxy, LT, 422 or HQ, smallest to largest. The profile sets
  the rate, which is why the bitrate control disappears when you choose ProRes.

The four fields are locked while a recording runs. FFmpeg took its copy of them at the start,
so leaving them live would let the panel describe a file that is not the one being written.

## Starting and stopping

Press the large button at the bottom of the panel. It reads **Start Recording**, then
**Stop Recording**, and the panel arms itself visibly: a pulsing red border, a REC badge, an
elapsed clock, the destination path, and counters for frames written, frames dropped and the
rate being managed. The dock also raises itself to the front of whatever tab stack it is in,
so an armed recording cannot be hidden behind another panel.

**Recording, Start Recording** on the menu bar does exactly the same thing as the button.

**F9 does not.** The key starts a recording with the built-in defaults, writing `output.mp4`
into the working directory as H.264 at 20 Mbps, whatever the panel says. It is the fast route
when you have not set anything up, and it stops a running recording regardless of how that
recording was started. When you have chosen a path or a codec, start with the button or the
menu item.

Stopping is non-blocking: the encoder thread drains its queue, flushes, and closes the file on
its own. Do not exit the application in the second after pressing Stop.

## Size and rate

Neither is a setting, because neither has a sensible free choice:

- **With a video or a live source open**, the recording takes the source's own resolution and
  frame rate, and exactly one frame is written per decoded frame. That is why the recorded
  frame rate matches the playback frame rate and cannot be set to something else.
- **With no video open**, it takes the generative output resolution from the Transport panel
  and writes at 60 fps, one frame per rendered frame. Starting a recording in that state also
  starts playback, since a stopped generative shader produces no new frames to record. The
  60 fps is a fixed declaration rather than a measurement, so it is accurate on a 60 Hz
  display and stretches the result on a faster one.

The **dropped** counter in the status block counts frames the encoder could not accept
because its queue was full. A handful over a long take is unremarkable; a number climbing
steadily means the encoder is not keeping up, and lowering the bitrate or moving from ProRes
HQ to 422 is the first thing to try.

## What ends up in the file

Whatever the viewport shows, at the content's own resolution rather than the window's: the
shader output, the video blend if one is active, and nothing else. No user interface, no
letterbox bars, no cursor.

One thing is missing from it: there is no audio track. The encoder writes video only, so
bring the sound across from the source in your editor. The second output window and the Spout
sender take the same finished picture as the recording does, and none of the three affects
either of the others.

## During the take

Everything stays live. The obvious use is cutting between shaders on their keybindings while
the recording runs, which is why shader shortcuts exist at all. The editor stays usable too,
so a compile error mid-take costs you the treated picture for as long as the shader is broken
(the renderer falls back to passthrough) and costs the recording nothing: it keeps writing
frames throughout.

The elapsed clock is the recording's own, counted from when it armed, and it is not the
playhead. A looping video under a long recording will pass zero several times while that
clock keeps climbing.
