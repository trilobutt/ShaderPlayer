# Keybinding Improvements Design

**Date:** 2026-02-21
**Status:** Approved

## Problem Statement

The current keybinding system has three deficiencies:

1. **Poor discoverability** - the only entry point is right-click → "Set Keybinding..."
2. **Unclear capture UX** - the modal shows no live feedback on what key combination is currently held; the modifier state is invisible until the binding commits
3. **No duplicate prevention** - two presets can silently share the same binding; the library displays only the bare key (`[A]`) without modifiers

## Requirements

- Live preview of the held key combination while inside the capture modal
- Full modifier+key display in the library list (`[Ctrl+A]`, not `[A]`)
- Duplicate detection: block the commit and show which shader already uses that binding
- Additional entry point into the capture modal (inline button in library row)
- Dedicated Keybindings panel listing all shaders and their bindings in a table

## Design

### Approach

Combination of targeted modal improvements (A) and a new dedicated panel (B).

### 1. Improved Capture Modal

Replace `DrawKeybindingModal` in `UIManager.cpp`.

**Behaviour:**

- Each frame, sample `GetKeyState` for Ctrl/Shift/Alt and `ImGui::IsKeyPressed` for the candidate key
- Display current held combination as live text: `Ctrl + Shift + F5` (dim `—` when nothing is held)
- Modifier-only presses (Ctrl, Shift, Alt alone) are ignored
- On a valid key press:
  - Run `Application::IsBindingConflict(key, modifiers, excludeIndex)` against all other presets
  - **Conflict found:** turn the preview text red, show `Already bound to "<name>"` - do not commit, modal stays open
  - **No conflict:** write `shortcutKey` / `shortcutModifiers` to the preset, close modal, call `SaveConfig()`
- Escape cancels without changes
- Delete clears the binding and closes

**Entry points:**

- Right-click context menu on library row (existing)
- New small `[kb]` button rendered at the right end of each library row

### 2. Full Combo Display in Library

`Application::GetKeyName(vkCode)` is supplemented with `GetComboName(key, modifiers)` which returns strings like `"Ctrl+A"`, `"Shift+F1"`, `"Ctrl+Shift+2"`.

Library rows display `[Ctrl+A]` instead of `[A]`.

### 3. Dedicated Keybindings Panel

New `DrawKeybindingsPanel()` method in `UIManager`.

- Accessible via View menu entry and F3 toggle
- Renders a two-column `ImGui::Table` (Shader | Binding)
- Clicking a Binding cell opens the improved capture modal for that preset
- Empty bindings show `—` in dim text
- Panel is a floating window, closable via X button or F3 toggle

## Files Changed

| File | Change |
|------|--------|
| `src/UIManager.h` | Add `m_showKeybindingsPanel`, `DrawKeybindingsPanel()` declaration |
| `src/UIManager.cpp` | Rewrite `DrawKeybindingModal`, add `DrawKeybindingsPanel`, update library row rendering, add F3 toggle |
| `src/Application.h` | Add `GetComboName(key, modifiers)`, `IsBindingConflict(key, modifiers, excludeIndex)` |
| `src/Application.cpp` | Implement both new methods |

No changes to `Common.h`, `ShaderManager`, `ConfigManager`, or config format.

## Duplicate Decision

User preference: **Block + warn** - refuse to apply the binding, inform the user which shader owns it, require them to pick a different key. No auto-steal.
