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

Press the large button at the bottom of the panel. With a video file open it reads **Render
Video to File**, and with a live source or nothing open it reads **Start Recording**; the two
are genuinely different operations, described below. Either way the panel arms itself visibly:
a pulsing red border, a REC badge, an elapsed clock, the destination path, and counters for
frames written, frames dropped and the rate being managed. The dock also raises itself to the
front of whatever tab stack it is in, so an armed recording cannot be hidden behind another
panel.

**Recording, Start Recording** on the menu bar and **F9** both do exactly the same thing as
the button, using the settings you configured in the panel.

Stopping is non-blocking: the encoder thread drains its queue, flushes, and closes the file on
its own. Do not exit the application in the second after pressing Stop.

## Rendering a file, versus recording a source

Which one you get depends on what is open, and the difference is worth understanding because
the first is not a capture at all.

**With a video file open, recording is a render.** Starting it takes over the transport:
the playhead rewinds to the start, playback begins, and exactly one frame is decoded per
tick rather than one per elapsed frame interval. Two things follow. The render runs as fast
as the machine manages rather than at real time, so a heavy shader makes it slower to finish
without dropping or repeating a single frame. And it ends by itself at the end of the source,
closing the file and leaving the transport stopped at the start.

While a render owns the transport, Play, Stop and the scrubber are inert and the Transport
panel greys itself out to say so. Stopping only the playback would leave the encoder open
with nothing arriving at it, so the Stop on the Recording panel is the way out. Opening
another video, opening a capture device or closing the current one all end the render first.

**With a live source or nothing open, recording is a capture.** It runs in real time, takes
one frame per rendered frame, and stops only when you stop it.

## Size and rate

Neither is a setting, because neither has a sensible free choice:

## Size and rate

Neither is a setting, because neither has a sensible free choice:

- **With a video or a live source open**, the recording takes the source's own resolution and
  frame rate, and exactly one frame is written per decoded frame. That is why the recorded
  frame rate matches the source's and cannot be set to something else. In a file render the
  wall-clock time the render takes has no bearing on the result: one decoded frame is one
  written frame whether the shader is cheap or expensive.
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

**A file render carries the source's audio.** The track is taken from the same packets the
picture comes from and muxed into the output, so a rendered file needs nothing brought across
in an editor. The two cases with no audio track are a live capture, because the video device
is opened without an audio pin, and a generative recording, because there is no source to
take sound from.

Audio during a render is written and analysed but not played, since the render is running
faster than real time and there would be nothing to listen to. An audio-reactive shader still
sees the correct bands: the analyser is fed from each video frame's own timestamp, so what
the shader reacts to belongs to the frame being written rather than to whenever the tick
happened.

The second output window and the Spout sender take the same finished picture as the recording
does, and none of the three affects either of the others.

## During the take

Everything stays live. The obvious use is cutting between shaders on their keybindings while
the recording runs, which is why shader shortcuts exist at all. The editor stays usable too,
so a compile error mid-take costs you the treated picture for as long as the shader is broken
(the renderer falls back to passthrough) and costs the recording nothing: it keeps writing
frames throughout.

The elapsed clock is the recording's own, counted from when it armed, and it is not the
playhead. In a file render the two diverge sharply, because the render is not running at real
time: a two minute source finishing in forty seconds shows forty seconds on that clock and
still writes two minutes of video.
