---
title: Keyframe Animation
nav_title: Keyframe Animation
---

# Keyframe Animation

Any parameter except an event or an audio band can be animated along the timeline. Keys are
stored in seconds against the video's own clock, so a key placed at 4.2 s lands at 4.2 s
whatever frame rate the source runs at, and a key survives a re-encode of the footage at a
different rate.

## Turning it on and placing a first key

1. Press **KF** on the parameter's row in the Shader Parameters panel. A keyframe editor
   opens in the row directly beneath it, saying there are no keyframes yet.
2. Move the playhead to where you want the value.
3. Set the parameter to that value with its ordinary control.
4. Press **+ Key**.

That records the current value at the current playhead position. Repeat at another position
with another value and the parameter now animates between the two.

Order does not matter. Keys are kept sorted by time, and adding one at a time that already
has a key overwrites it rather than producing two.

<figure class="shot shot--params">
  <a href="/static/img/keyframes.png" target="_blank" rel="noopener"><img src="/static/img/keyframes.png" alt="A parameter row with keyframing enabled: the KF button lit, three timestamp chips beneath it, and a detail block with time and value editors, a Custom Bezier interpolation combo and a curve editor with two draggable handles."></a>
  <figcaption>Three keys on one parameter, with the first selected and its outgoing segment set to Custom Bezier. The parameter's own slider is greyed while the timeline owns it. Click the picture for it full size.</figcaption>
</figure>

## The editor

Every key on the track appears as a **chip** labelled with its time. Clicking a chip seeks
the transport to that key and selects it for editing. The selected key gets four controls:

- **Time**, as a number box in seconds, or in frames when the transport clock is in frame
  mode. **To Playhead** moves the key to wherever the transport is now.
- **Value**, in the same control the parameter itself uses: a number box for a float, a
  checkbox for a bool, a dropdown for a long, a swatch for a colour, an X and Y pair for a
  point.
- **Curve**, one of **Linear**, **Ease In/Out** or **Custom Bezier**, which governs the
  travel from this key to the next one. The last key on a track has nothing after it, and the
  editor says so instead of offering a curve that would do nothing.
- **Delete**, which removes this key and leaves the rest of the track alone.

Linear runs to the next key at a constant rate. Ease In/Out leaves slowly and arrives slowly.
Custom Bezier reveals an inline curve editor with two draggable handles: drag either to shape
the timing, with the horizontal axis as time and the vertical as the value's progress from
this key to the next.

Bool and long parameters step rather than slide: the value snaps to one side or the other
instead of taking intermediate values. The curve still matters, because it decides *when* the
step happens.

## Working on the scrubber

The selected parameter's keys are drawn on the transport scrubber as diamonds in the
Parameters panel's own colour, in a lane of their own beneath the playhead. Clicking a
diamond seeks to that key and selects it, exactly as clicking its chip does.

Diamonds can also be dragged, which moves the key in time. Dragging is deliberately not the
default gesture on a scrubber, so it is armed two ways: hold **Shift** while you scrub, which
arms it for that drag alone, or press **Follow** in the transport bar, which arms it until
you press it again. Follow is disabled until a keyframe is selected, and its tooltip says so.

## Playback

While the video plays, every animated parameter is evaluated each frame and its widgets go
inert, with a tooltip explaining that the timeline is driving them and that pausing hands
control back. Pause and they are yours again.

Evaluation is not limited to playback: scrubbing a paused video also moves the animated
values, so you can drag through the timeline and watch the parameter follow.

Keyframes work with no video open too. A generative or audio-reactive shader runs on a wall
clock that starts when the shader does, and keys evaluate against that clock in exactly the
same way. There is no scrubber in that state, so place keys by setting the **Time** box
directly rather than by seeking.

## Two things that will lose your work

**Compiling drops every timeline on the shader.** F5, the Compile button, and the automatic
compile 500 ms after you stop typing all rebuild the parameter list from the ISF block, and a
rebuilt parameter has no timeline. Values are carried across by name; keyframes are not.
Finish the source, then animate it.

**An external edit does the same, and resets the values too.** A shader file changed on disk
is re-read and recompiled within half a second, wholesale. See
[Shaders and the editor](/manual/shaders-and-editing/).

Keyframes are saved to `config.json` under the parameter's name and restored at the next
launch, so a session that ends normally loses nothing. Renaming a parameter in the shader
source orphans its saved timeline, since the match is by name.
