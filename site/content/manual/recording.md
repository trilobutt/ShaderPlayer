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

The Recording panel (F4, or Recording, Recording Settings...) holds five things:

- **Output**, a path with a **Browse...** button beside it. Type it or pick it; the field
  shows the middle of a long path elided and the full thing as a tooltip.
- **Codec**: *H.264 (MP4)*, small and playable anywhere, or *ProRes (MOV)*, far larger and
  built to survive further editing.
- **Frame rate**, 1 to 240 fps and 25 by default. It sets the rate of a generative
  recording, so it greys itself out with a tooltip saying why whenever a video file is open:
  that file supplies its own rate.
- **Bitrate**, for H.264 only: 5 to 100 Mbps in steps of 5, 20 by default.
- **Profile**, for ProRes only: Proxy, LT, 422 or HQ, smallest to largest. The profile sets
  the rate, which is why the bitrate control disappears when you choose ProRes.

The fields are locked while a recording runs. FFmpeg took its copy of them at the start,
so leaving them live would let the panel describe a file that is not the one being written.

## Starting and stopping

Press the large button at the bottom of the panel. With a video file open it reads **Render
Video to File**, and with a live source or nothing open it reads **Start Recording**; the two
are genuinely different operations, described below. Either way the panel arms itself visibly:
a pulsing red border, a REC badge, an elapsed clock, the destination path, and counters for
frames written, frames dropped and the rate being managed. The dock also raises itself to the
front of whatever tab stack it is in, so an armed recording cannot be hidden behind another
panel.

<figure class="shot shot--recording">
  <a href="/static/img/recording.png" target="_blank" rel="noopener"><img src="/static/img/recording.png" alt="The Recording panel while armed: a pulsing red border around the settings, a REC badge, an elapsed clock of 00:08, the output path, a counter reading 243 frames, 0 dropped, 27.0 fps, and a Stop Rendering button."></a>
  <figcaption>The panel mid-take, on a generative render. The settings are locked, the status block carries the elapsed clock, the destination and the counters, and the button has become the way out. Click the picture for it full size.</figcaption>
</figure>

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

**With a live source open, recording is a capture.** It runs in real time, takes one frame
per rendered frame, and stops only when you stop it. A camera cannot be told to wait, which
makes this the one case that can lose a frame.

**With nothing open, a generative recording is a render as well.** The shader's clock is
advanced by exactly one frame's worth per frame written rather than by the wall clock, so the
animation in the file runs at the rate the panel declares however long each frame took to
draw. Starting it also starts playback, since a stopped generative shader produces no new
frames to record, and it has no end of its own, so it runs until you stop it. The armed
button reads **Stop Rendering** in both render cases and **Stop Recording** only for a live
capture, which is the quickest way to tell which of the two you are in.

## Size and rate

The resolution is never a setting. The rate is one in exactly one case:

- **With a video or a live source open**, the recording takes the source's own resolution and
  frame rate, and exactly one frame is written per decoded frame. That is why the recorded
  rate matches the source's and the **Frame rate** box greys itself out. In a file render the
  wall-clock time the render takes has no bearing on the result: one decoded frame is one
  written frame whether the shader is cheap or expensive.
- **With no video open**, the resolution comes from the Transport panel's output control and
  the rate comes from the panel's **Frame rate** box. The preview stops being real time for
  the duration, which is the whole trade: a shader too heavy to hit the rate takes longer
  than real time to finish instead of quietly writing a slower file, and a cheap one finishes
  early.

The **dropped** counter in the status block counts frames the encoder could not accept
because its queue was full, and only a live capture can put a number in it. Both renders wait
for queue space instead of dropping, since a dropped frame would shorten the file and pull
everything after it earlier rather than leaving a gap. A count climbing during a live capture
means the encoder is behind, and lowering the bitrate or moving from ProRes HQ to 422 is the
first thing to try.

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
