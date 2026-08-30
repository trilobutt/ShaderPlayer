---
title: Workspaces and Keybindings
nav_title: Workspaces and Keybindings
---

# Workspaces and Keybindings

Two kinds of saved state, and they interact: a workspace stores where every panel sits, and
a workspace can be bound to a key, which puts it in the same namespace as the shader
shortcuts and the reserved keys.

## Layouts

Drag panels wherever you want them. Any panel can be moved to another edge, floated free of
the window, tabbed behind another panel, or closed. The layout is saved when you exit
normally and restored on the next launch, along with the window's size and position, so a
workspace preset is only needed when you want to keep more than one arrangement.

**View, Workspace Presets, Save Current As...** names the arrangement in front of you and
writes it to an `.ini` file in `layouts/` beside the executable. Saved workspaces then appear
in that same submenu; choosing one applies it. **Manage Workspaces...** lists them with their
shortcuts and offers **Set Keybinding...** and **Delete**, the latter removing the `.ini`
file.

A workspace stores where each panel sits **and which panels are open**, which is the part
that is easy to miss: closing four docks and saving the result gives you a preset that closes
them again. Two panels are excluded from that and are always shown: the viewport and Shader
Parameters, the latter because it has no close button and no route back if a preset hid it.

The first entry, **Default**, is built in. It cannot be renamed, rebound or deleted, and it
is what the application arranges on a first run: Parameters on the left, Library over Editor
on the right, the viewport in the centre, Transport and Recording tabbed along the bottom.

Restoring a layout is all or nothing. A preset saved under a different set of panels, or a
file that has been corrupted, is refused outright rather than applied halfway, and the Default
arrangement is used instead. A workspace that suddenly does nothing after an application
update has been refused for that reason; save it again from the arrangement you want.

## Binding a key to a shader

Right-click a shader in the Shader Library and choose **Set Keybinding...**. Hold whatever
modifiers you want and press the key. The dialog shows the combination, tells you whether it
is free, and refuses to assign one that is not.

The **(No Effect)** row at the top of the library takes a binding the same way, which gives
you a second key for passthrough alongside Escape.

Workspace shortcuts are set from Manage Workspaces, and behave identically.

What can be bound: **A to Z, 0 to 9, and F1 to F12**, alone or with Ctrl, Alt and Shift in
any combination. The numeric keypad is deliberately refused, because Windows reports those
keys differently and a binding stored from one would never fire.

**F6** opens a reference listing every reserved key, every shader shortcut and every
workspace shortcut currently assigned.

## The keys you cannot have

Fourteen combinations are reserved, and the binding dialog names the one it is refusing you
rather than just saying no.

| Key | Action |
|---|---|
| Space | Play / Pause |
| Escape | Reset to passthrough |
| F1 | Toggle Shader Editor |
| F2 | Toggle Shader Library |
| F3 | Toggle Transport |
| F4 | Toggle Recording panel |
| F5 | Compile the editor's contents |
| F6 | Keybindings reference |
| F7 | Toggle the Video Output Window |
| F8 | Toggle the Spout Output panel |
| F9 | Start / stop recording |
| Ctrl+O | Open Video |
| Ctrl+S | Save Shader |
| Ctrl+N | New Shader |

The dialog refuses only the exact combinations above, so it will happily accept Shift+F1 or a
plain `O`. Do not take it up on that. Those keys are claimed before any shader or workspace
binding is considered, and claimed whatever modifiers are held, so a shader bound to Shift+F1
still toggles the editor and never switches the shader. In practice the keys to avoid
entirely are **Space, Escape, F1 to F9, O and S**. F10, F11 and F12 are genuinely free, as is
every other letter and digit.

Two entries in the table need a footnote. **F9** starts a recording with the built-in defaults
rather than the settings in the Recording panel, which is covered in
[Recording](/manual/recording/). And **Ctrl+N** is reserved and printed against Shader, New
Shader..., but the key itself is not dispatched: the menu item works, the shortcut does not.
Use the menu, or **+ New** in the Shader Library.

## Where keys reach and where they do not

Space and every printable character belong to whatever text field has focus, so typing in the
editor or the library's filter box never triggers a shortcut. Escape is the exception, since
no text field claims it, and it is guarded specifically: pressing Escape while a field has
focus does not reset the shader out from under what you are typing.

F-keys and Ctrl chords deliberately keep working while a text field has focus, which is what
lets F5 compile and Ctrl+S save without leaving the editor. Holding a shortcut down does not
repeat it.

## Where all of this is stored

Shader shortcuts, the passthrough binding, the window geometry and the panel visibility live
in `config.json` beside the executable. Workspace layouts and their shortcuts live in the
`.ini` files under `layouts/`, not in `config.json`, which is why copying a `layouts` folder
to another machine carries the arrangements across.
