---
title: Spout and the Second Window
nav_title: Spout and Video Output
---

# Spout and the Second Window

Two ways to get the finished picture somewhere other than the viewport: share it with
another application on the machine over Spout, or put it in a plain second window you can
drag onto a projector. They are independent toggles and either, both or neither can be on.
Both carry exactly what the viewport shows, at the content's own resolution, with no
interface over it.

## Spout

Spout shares a GPU texture between applications on the same machine with no capture card,
no encode and no round trip through the CPU. Resolume, MadMapper, OBS with the Spout2
plugin, and SpoutCam all read a sender directly.

Open the **Spout Output** panel (F8, or View, Spout Output) and tick **Send via Spout**. It
is off by default and the setting persists.

**Sender name** is what receivers pick the stream out of their source lists by, `ShaderPlayer`
unless you change it. The typed name is applied when you press Enter or click away from the
field, not on every keystroke.

The status block reads one of four things:

- **SENDING**. Live, and receivers on this machine can pick it up under the name shown.
- **WAITING**. On, but no frame has been shared yet. The sender registers with the system on
  the first frame it sends.
- **UNAVAILABLE**. Spout could not start on this machine, so nothing is being shared. The
  checkbox puts itself back rather than pretending otherwise.
- **OFF**. Nothing is leaving the machine.

One warning is worth reading if it appears. When the name you asked for is already registered
by another application, Spout renames this stream by appending a suffix, and the panel says
so and prints the name it actually got. A receiver looking for the name in the field above
would never find the stream, so use the one in the warning.

**Get SpoutCam (virtual webcam)...** opens the SpoutCam releases page in your browser.
SpoutCam turns a Spout sender into a webcam device, which is how you get ShaderPlayer's
output into an application that has no Spout support but does have a camera picker.

## The second output window

**F7**, or View, Video Output Window, opens a plain resizable window titled ShaderPlayer,
Video Output, showing the same rendered frame. It opens at 1280 by 720 and can be dragged to
another display and maximised there.

It draws on the same GPU device as the main window, so there is no copy between adapters and
no measurable cost to having it open.

One difference from the viewport: **the second window stretches the picture to fill itself
rather than letterboxing it**. Size it to the content's aspect ratio, or accept the
distortion. Closing it with its own close button is the same as pressing F7 again; it does
not close the application.

## Which to use

Spout when the destination is another application on the same machine and you want it
composited, keyed or restreamed there. The second window when the destination is a screen: a
projector, a second monitor, a capture card fed from a display output.

Neither interacts with [recording](/docs/manual/recording/). All three take the same finished
picture, and turning any of them on or off mid-take changes nothing about the others.
