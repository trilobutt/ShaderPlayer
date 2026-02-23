# Keyframe Transport Integration Design

**Date:** 2026-02-23

## Problem

Keyframe chips seek the playhead to a keyframe's time (read direction). There is no reverse flow: no way to push the playhead's current position back into a selected keyframe's timestamp.

## Solution

Two complementary interactions:

1. **Snap to Playhead** — a "Set" button in the keyframe detail panel that immediately writes `m_app.GetPlaybackTime()` to the selected keyframe's time.
2. **Follow Mode** — a toggle button (`~` or chain icon) that keeps the selected keyframe's time in sync with every subsequent seek. Auto-disables when the selected keyframe changes or the active preset changes.
3. **Shift-drag** — holding Shift while dragging the transport slider routes the slider value to the selected keyframe's time (in addition to seeking the playhead).

Both #2 and #3 write through the same code path and can coexist.

## UI Changes

### Keyframe Detail Panel (`DrawKeyframeDetail`)

Existing time field row extended:

```
[time: 1.20 s]  [Set]  [~]
```

- **Set**: `ImGui::SmallButton("Set##kfset")` — one-click snap.
- **~ toggle**: `ImGui::SmallButton` styled gold when active. Stored as `bool m_keyframeFollowMode` in `UIManager`.

### Transport Controls (`DrawTransportControls`)

After the slider interaction block, add follow/shift routing:

```cpp
if (ImGui::SliderFloat("##timeline", &currentTime, 0.0f, duration, "%.1f s")) {
    m_app.SeekTo(currentTime);
    // Route to selected keyframe if follow mode or Shift held
    bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (shiftHeld || m_keyframeFollowMode) {
        // write currentTime to selected keyframe
    }
}
```

## Data Flow

```
User drags slider
  → SliderFloat fires with newTime
  → m_app.SeekTo(newTime)          (always — playhead moves)
  → if (Shift OR followMode) AND keyframe selected:
      kf.time = newTime
      m_app.OnParamChanged()       (re-packs uniforms, updates diamonds)
```

"Set" button:

```
User clicks [Set]
  → kf.time = m_app.GetPlaybackTime()
  → m_app.OnParamChanged()
```

## State

`UIManager` gains one field:

```cpp
bool m_keyframeFollowMode = false;
```

Reset to `false` in the same locations `m_selectedKeyframeParam` is reset to `-1` (preset change, preset activation).

## Edge Cases

- No keyframe selected: Set button absent (inside `if selected`), follow toggle greyed/disabled, Shift-drag is a no-op.
- Follow mode + Shift held simultaneously: no conflict, both trigger same code path.
- Follow mode auto-clears on preset change: consistent with existing selection-reset logic.
- Keyframe time clamped to `[0, duration]` (same as existing time field editor).
