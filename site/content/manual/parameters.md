---
title: Parameters
nav_title: Parameters
---

# Parameters

The Shader Parameters panel is built from the active shader's own declaration, so it changes
completely each time you switch shaders. Every control writes straight into the constant
buffer the shader reads: move a slider and the picture changes on the next frame, with no
apply step and no per-frame cost when nothing is moving.

A shader that declares no parameters gets a line saying so rather than an empty panel, and
with nothing active at all the panel says the picture is passing through untouched.

## A row

Five columns, left to right: the parameter's label, its control, a reset arrow, an **R**, and
a **KF**.

The **reset arrow** restores that parameter's declared default and is greyed when the value
already is the default. **R** rolls a random value inside exactly the range the control
offers, so a rolled value is always one you could have set by hand. **KF** turns keyframing
on for that parameter and opens its
[keyframe editor](/manual/keyframe-animation/) in the row beneath.

Above the grid, **Randomise All** rolls every parameter at once and **Reset to Defaults**
restores all of them. Both skip parameters currently under keyframe control, since the
timeline would overwrite the result on the next frame. Randomising also leaves colour alpha
alone deliberately: alpha is an opacity everywhere it is read, and rolling it is how an
effect becomes invisible for no visible reason.

Shaders that sample the global noise texture get one extra button in the header,
**Noise Generator...**, which opens that panel.

## The controls

**Float** is a slider with an editable number beside it. The slider snaps to the shader's
declared step; the number box accepts anything inside the range and is where you go for
precision the slider cannot offer.

**Bool** is a checkbox.

**Long** is a dropdown over the fixed list the shader declares. A `long` written with no
`VALUES` array produces an empty list, which the panel disables and explains in its tooltip
rather than showing as an empty combo that looks broken.

**Colour** is a swatch button that opens the system colour picker with the alpha channel
enabled, plus the hex value and the alpha percentage spelled out beside it. Alpha is not
decoration: a shader's output alpha scales the video blend per pixel, so it is the idiomatic
opacity control for a single element of an effect.

**Point2D** is a pair of number boxes prefixed **X** and **Y**, both bounded by the same
declared range.

**Event** is a **Trigger** button. Pressing it sets the value to 1.0 for exactly one rendered
frame, which is what a shader reads as a one-shot. Events cannot be keyframed and have no
**KF** toggle.

**Audio band** is a read-only meter showing the live value the analyser is producing. There
is nothing to set: the signal comes from whatever is playing. Audio parameters are not saved
to `config.json` and cannot be keyframed. The
[audio reference](/reference/audio-and-spectrum/) gives the six bands and the range they
actually occupy, which is smaller than most people guess.

## When a control is greyed out

Three reasons, and they look the same:

- **The timeline owns it.** A parameter with keyframes enabled is driven by them while the
  video is playing, so its whole row goes inert and every widget in it carries a tooltip
  saying to pause playback to set it by hand. Pausing hands control back.
- **It is already at its default**, which greys the reset arrow alone.
- **The parameter offers nothing to set**, which is the empty `long` dropdown, and the audio
  meters, which are read-only by nature.

## Video blend

Under a rule at the bottom of the panel, and shown only while a video or a live source is
open, is the **VIDEO BLEND** section: a **Mode** dropdown (Off, Normal, Add, Multiply,
Screen, Overlay, Soft Light, Difference, Exclusion, Darken, Lighten) and an **Amount** from 0
to 1 that appears once the mode is anything but Off.

Blend is available to every shader type, including audio-reactive ones. A waveform over
footage, a generative pattern screened onto a shot, an effect mixed halfway back toward the
untreated picture: all of it is this control. The shader's own output alpha multiplies the
amount per pixel, so a shader that writes `alpha < 1` in part of the frame lets the video
through there under any mode. With blending Off the shader draws straight to the display and
alpha does nothing.

The mode and amount are stored per shader and persist between sessions.

## A control that does nothing

A parameter declared in a shader's ISF block but never read by its body still parses, still
takes a slot in the constant buffer, and still renders a working widget that changes nothing.
Neither the compiler nor the offline validator can detect it, because an unused alias is not
an error. If a control has no effect and the shader is one you are writing, check that the
body actually mentions the name. If it is one you did not write, that is what has happened.

The full parameter declaration format, the packing rules and the `#define` alias each type
generates are on [the ISF block reference
page](/reference/isf-block-and-parameters/).
