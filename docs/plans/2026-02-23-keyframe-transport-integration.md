# Keyframe Transport Integration Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add snap-to-playhead ("Set" button) and follow-mode toggle ("~") to the keyframe detail panel, plus Shift-drag on the transport slider to reposition a selected keyframe's timestamp.

**Architecture:** All changes are confined to `UIManager.h` (one new field) and `UIManager.cpp` (three edit sites: detail panel, transport slider, reset locations). No new classes, no new files, no Application API changes.

**Tech Stack:** ImGui (Win32/D3D11 backend), C++20, existing `KeyframeTimeline` / `ShaderParam` types from `Common.h`.

---

### Task 1: Add `m_keyframeFollowMode` field to UIManager

**Files:**
- Modify: `src/UIManager.h:122-124`

**Step 1: Add the field**

In `UIManager.h`, in the "Keyframe editing state" block (lines 122–124), add `m_keyframeFollowMode` after the existing two fields:

```cpp
    // Keyframe editing state
    int m_selectedKeyframeParam = -1;   // index into preset->params, or -1
    int m_selectedKeyframeIndex = -1;   // index into timeline->keyframes, or -1
    bool m_keyframeFollowMode   = false; // transport seek also moves selected keyframe
```

**Step 2: Verify it compiles**

```bash
cmake --build build --config Debug 2>&1 | tail -5
```

Expected: no errors.

**Step 3: Commit**

```bash
git add src/UIManager.h
git commit -m "feat: add m_keyframeFollowMode field to UIManager"
```

---

### Task 2: "Set" button and "~" follow-mode toggle in keyframe detail panel

**Files:**
- Modify: `src/UIManager.cpp:571-587` (inside `DrawKeyframeDetail`, time-input block)

**Context:** The time input row currently ends after `ImGui::InputFloat("Time##kftime", ...)` at line 574. We insert two `SmallButton`s on the same row after it, still inside `DrawKeyframeDetail`.

**Step 1: Replace the time-input block**

Find this block (lines 571–587):

```cpp
    // Time input
    ImGui::SetNextItemWidth(100.0f);
    float editTime = kf.time;
    if (ImGui::InputFloat("Time##kftime", &editTime, 0.1f, 1.0f, "%.2f s")) {
        if (editTime != kf.time) {
            // Re-sort: remove and re-insert to maintain order
            Keyframe copy = kf;
            copy.time = editTime;
            timeline.RemoveKeyframe(keyframeIndex);
            int newIdx = timeline.AddKeyframe(copy);
            m_selectedKeyframeIndex = newIdx;
            // Early return since keyframeIndex is now invalid
            ImGui::PopID();
            ImGui::Unindent(16.0f);
            return;
        }
    }
```

Replace with:

```cpp
    // Time input
    ImGui::SetNextItemWidth(100.0f);
    float editTime = kf.time;
    if (ImGui::InputFloat("Time##kftime", &editTime, 0.1f, 1.0f, "%.2f s")) {
        if (editTime != kf.time) {
            // Re-sort: remove and re-insert to maintain order
            Keyframe copy = kf;
            copy.time = editTime;
            timeline.RemoveKeyframe(keyframeIndex);
            int newIdx = timeline.AddKeyframe(copy);
            m_selectedKeyframeIndex = newIdx;
            // Early return since keyframeIndex is now invalid
            ImGui::PopID();
            ImGui::Unindent(16.0f);
            return;
        }
    }

    // Snap-to-playhead button
    ImGui::SameLine();
    if (ImGui::SmallButton("Set##kfset")) {
        float playhead = static_cast<float>(m_app.GetPlaybackTime());
        Keyframe copy = kf;
        copy.time = playhead;
        timeline.RemoveKeyframe(keyframeIndex);
        int newIdx = timeline.AddKeyframe(copy);
        m_selectedKeyframeIndex = newIdx;
        anyChanged = true;
        ImGui::PopID();
        ImGui::Unindent(16.0f);
        return;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap keyframe to current playhead position");

    // Follow-mode toggle — disabled when no video open (nothing to follow)
    ImGui::SameLine();
    bool videoOpen = m_app.GetDecoder().IsOpen();
    if (!videoOpen) ImGui::BeginDisabled();
    if (m_keyframeFollowMode)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.6f, 0.1f, 1.0f));
    if (ImGui::SmallButton("~##kffollow"))
        m_keyframeFollowMode = !m_keyframeFollowMode;
    if (m_keyframeFollowMode)
        ImGui::PopStyleColor();
    if (!videoOpen) ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(m_keyframeFollowMode
            ? "Follow mode ON: seeking also moves this keyframe"
            : "Follow mode OFF: click to link this keyframe to the transport");
```

**Step 2: Build and verify**

```bash
cmake --build build --config Debug 2>&1 | tail -5
```

Expected: no errors.

**Step 3: Manual smoke test**

- Load a video, load a shader with a float param, enable KF, add a keyframe
- Select the keyframe chip — detail panel appears
- Click "Set" — keyframe time jumps to current playhead position, chip label updates
- Click "~" — button turns gold; tooltip says "Follow mode ON"
- Click "~" again — button returns to default; tooltip says "Follow mode OFF"

**Step 4: Commit**

```bash
git add src/UIManager.cpp
git commit -m "feat: add Set and follow-mode toggle to keyframe detail panel"
```

---

### Task 3: Transport slider routing (Shift-drag + follow mode)

**Files:**
- Modify: `src/UIManager.cpp:850-852` (inside `DrawTransportControls`, timeline slider block)

**Context:** The slider block currently is:

```cpp
            ImGui::SetNextItemWidth(400);
            if (ImGui::SliderFloat("##timeline", &currentTime, 0.0f, duration, "%.1f s")) {
                m_app.SeekTo(currentTime);
            }
```

**Step 1: Replace slider block**

```cpp
            ImGui::SetNextItemWidth(400);
            if (ImGui::SliderFloat("##timeline", &currentTime, 0.0f, duration, "%.1f s")) {
                m_app.SeekTo(currentTime);

                // Route to selected keyframe when follow mode active or Shift held
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if ((m_keyframeFollowMode || shiftHeld) && m_selectedKeyframeParam >= 0 && m_selectedKeyframeIndex >= 0) {
                    ShaderPreset* activePreset = m_app.GetShaderManager().GetActivePreset();
                    if (activePreset &&
                        m_selectedKeyframeParam < static_cast<int>(activePreset->params.size())) {
                        auto& kfParam = activePreset->params[m_selectedKeyframeParam];
                        if (kfParam.timeline && kfParam.timeline->enabled &&
                            m_selectedKeyframeIndex < static_cast<int>(kfParam.timeline->keyframes.size())) {
                            // Re-sort by removing and reinserting at new time
                            Keyframe copy = kfParam.timeline->keyframes[m_selectedKeyframeIndex];
                            copy.time = currentTime;
                            kfParam.timeline->RemoveKeyframe(m_selectedKeyframeIndex);
                            m_selectedKeyframeIndex = kfParam.timeline->AddKeyframe(copy);
                            m_app.OnParamChanged();
                        }
                    }
                }
            }
```

**Step 2: Build**

```bash
cmake --build build --config Debug 2>&1 | tail -5
```

Expected: no errors.

**Step 3: Manual smoke test — Shift-drag**

- Select a keyframe chip (playhead jumps to it)
- Hold Shift and drag the transport slider
- Keyframe chip label updates in real time; diamond marker moves along the bar
- Release Shift; drag the slider normally — keyframe does NOT move

**Step 4: Manual smoke test — follow mode**

- Enable follow mode ("~" turns gold)
- Drag the transport slider without holding Shift
- Keyframe moves with the playhead
- Disable follow mode; drag — keyframe stays put

**Step 5: Commit**

```bash
git add src/UIManager.cpp
git commit -m "feat: transport slider routes to selected keyframe on Shift-drag or follow mode"
```

---

### Task 4: Reset follow mode on selection change and preset change

**Files:**
- Modify: `src/UIManager.cpp` — three sites where `m_selectedKeyframeParam = -1`

**Context:** `m_selectedKeyframeParam` is cleared in three places:
- Line 389–392: stale index guard in `DrawShaderParameters`
- Line 731–733: "Delete Keyframe" button in `DrawKeyframeDetail`
- Line 781–783: preset activated via `Selectable` in `DrawShaderLibrary`

**Step 1: Add `m_keyframeFollowMode = false;` at each reset site**

Site 1 (line ~390, stale index guard):
```cpp
        m_selectedKeyframeParam = -1;
        m_selectedKeyframeIndex = -1;
        m_keyframeFollowMode    = false;
```

Site 2 (line ~731, Delete Keyframe button):
```cpp
        m_selectedKeyframeIndex = -1;
        m_selectedKeyframeParam = -1;
        m_keyframeFollowMode    = false;
```

Site 3 (line ~781, preset activated):
```cpp
                m_selectedKeyframeParam = -1;
                m_selectedKeyframeIndex = -1;
                m_keyframeFollowMode    = false;
```

**Step 2: Build**

```bash
cmake --build build --config Debug 2>&1 | tail -5
```

Expected: no errors.

**Step 3: Manual smoke test**

- Enable follow mode on a keyframe
- Click a different preset — "~" button should no longer be gold on any new keyframe
- Enable follow mode, delete the keyframe — follow mode cleared
- Enable follow mode, load a param with > index (trigger stale guard by removing a param from the shader) — follow mode cleared

**Step 4: Commit**

```bash
git add src/UIManager.cpp
git commit -m "fix: clear keyframe follow mode on selection reset and preset change"
```
