# Keybinding Improvements Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make shader keybindings easier to discover and assign, display full modifier+key combos, and block duplicate bindings with a clear warning.

**Architecture:** All changes are confined to `Application.{cpp,h}` (two new helpers) and `UIManager.{cpp,h}` (improved modal, new panel, updated library row rendering). Config format and all other components are untouched.

**Tech Stack:** C++20, ImGui (docking branch), Win32 VK codes, Direct3D 11

---

## Context / Current State

- `ShaderPreset` (Common.h:53) stores `shortcutKey` (VK code) and `shortcutModifiers` (MOD_CONTROL / MOD_SHIFT / MOD_ALT bitmask).
- `Application::GetKeyName(vkCode)` returns just the bare key name ("A", "F1"). No modifier support.
- `UIManager::DrawKeybindingModal()` (UIManager.cpp:488–522): opened when `m_showKeybindingModal == true`, iterates all 256 VK codes via `ImGui::IsKeyPressed`. No live preview, no duplicate check, displays only bare key in library rows.
- F3 is already bound to Transport Controls in the View menu — use **F6** for the new Keybindings panel.
- No automated test harness exists (graphics app). Each task has a **manual verification** step instead of a test runner.

---

### Task 1: Add `GetComboName` and `IsBindingConflict` to Application

**Files:**
- Modify: `src/Application.h` (after line 66)
- Modify: `src/Application.cpp` (after `GetKeyName`, before closing `}`)

**Step 1: Add declarations to Application.h**

Insert after the existing `GetKeyName` declaration (line 66):

```cpp
    // Returns a human-readable combo string, e.g. "Ctrl+Shift+F1"
    std::string GetComboName(int vkCode, int modifiers) const;

    // Returns the index of any preset that already uses this combo,
    // excluding `excludeIndex` (-1 to check all). Returns -1 if free.
    int IsBindingConflict(int vkCode, int modifiers, int excludeIndex) const;
```

**Step 2: Implement in Application.cpp**

Add after `GetKeyName` (currently ends at line 607):

```cpp
std::string Application::GetComboName(int vkCode, int modifiers) const {
    std::string result;
    if (modifiers & MOD_CONTROL) result += "Ctrl+";
    if (modifiers & MOD_ALT)     result += "Alt+";
    if (modifiers & MOD_SHIFT)   result += "Shift+";
    result += GetKeyName(vkCode);
    return result;
}

int Application::IsBindingConflict(int vkCode, int modifiers, int excludeIndex) const {
    const int count = m_shaderManager->GetPresetCount();
    for (int i = 0; i < count; ++i) {
        if (i == excludeIndex) continue;
        const ShaderPreset* p = m_shaderManager->GetPreset(i);
        if (!p || p->shortcutKey == 0) continue;
        if (p->shortcutKey == vkCode && p->shortcutModifiers == modifiers)
            return i;
    }
    return -1;
}
```

**Step 3: Build to confirm no compile errors**

```bash
cmake --build build --config Release 2>&1 | tail -20
```

Expected: clean build, no errors or warnings about the new methods.

**Step 4: Commit**

```bash
git add src/Application.h src/Application.cpp
git commit -m "feat: add GetComboName and IsBindingConflict helpers"
```

---

### Task 2: Update library row display to show full combo

**Files:**
- Modify: `src/UIManager.cpp` lines 357–361

**Step 1: Replace the TextDisabled call**

Current code (lines 358–360):
```cpp
            if (preset->shortcutKey != 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", m_app.GetKeyName(preset->shortcutKey).c_str());
            }
```

Replace with:
```cpp
            if (preset->shortcutKey != 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]",
                    m_app.GetComboName(preset->shortcutKey, preset->shortcutModifiers).c_str());
            }
```

**Step 2: Build**

```bash
cmake --build build --config Release 2>&1 | tail -20
```

**Step 3: Manual verification**

Run the app. Open Shader Library (F2). Assign a binding with modifiers to any shader (right-click → "Set Keybinding..."). Confirm the library row now shows e.g. `[Ctrl+A]` rather than `[A]`.

**Step 4: Commit**

```bash
git add src/UIManager.cpp
git commit -m "fix: display full modifier+key combo in shader library rows"
```

---

### Task 3: Add inline `[kb]` button to each library row

**Files:**
- Modify: `src/UIManager.cpp` — inside `DrawShaderLibrary`, the per-row block (lines 327–363)

**Step 1: Add a small bind button at the right of each row**

In the per-row block, insert a `[kb]` button. Place it just after the `ImGui::Selectable` and before the context menu block. The button must be right-aligned (use `ImGui::SameLine` with an explicit offset calculated from the available width).

Add this block immediately after the `ImGui::Selectable` call (after line 343):

```cpp
            // Right-aligned [kb] button
            float availWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(availWidth - 26.0f);
            if (ImGui::SmallButton("kb")) {
                m_keybindingPresetIndex = i;
                m_showKeybindingModal = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Set keybinding for '%s'", preset->name.c_str());
```

> Note: `ImGui::SameLine(availWidth - 26.0f)` positions the button relative to the window's content region. Adjust `26.0f` if the button is clipped or misaligned (depends on font size).

**Step 2: Build and verify**

```bash
cmake --build build --config Release 2>&1 | tail -20
```

Run the app. Each library row should now have a small `kb` button on the right. Clicking it opens the keybinding modal. Tooltip appears on hover.

**Step 3: Commit**

```bash
git add src/UIManager.cpp
git commit -m "feat: add inline kb button to shader library rows"
```

---

### Task 4: Rewrite the capture modal with live preview and duplicate blocking

**Files:**
- Modify: `src/UIManager.h` — add `m_keybindingConflictMsg` member
- Modify: `src/UIManager.cpp` — replace `DrawKeybindingModal` (lines 488–522)

**Step 1: Add conflict message string to UIManager.h**

In the private section, after `int m_keybindingPresetIndex = -1;` (line 64), add:

```cpp
    std::string m_keybindingConflictMsg;
```

Also clear it when the modal opens. In the two places that set `m_showKeybindingModal = true` (context menu, line 349, and the new [kb] button from Task 3), add:

```cpp
m_keybindingConflictMsg.clear();
```

**Step 2: Replace DrawKeybindingModal in UIManager.cpp**

Replace the entire function (lines 488–522) with:

```cpp
void UIManager::DrawKeybindingModal() {
    ImGui::OpenPopup("Set Keybinding");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Set Keybinding", &m_showKeybindingModal,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        auto* preset = m_app.GetShaderManager().GetPreset(m_keybindingPresetIndex);
        if (!preset) {
            m_showKeybindingModal = false;
            ImGui::EndPopup();
            return;
        }

        ImGui::Text("Setting keybinding for: %s", preset->name.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("Hold modifiers and press a key   |   Esc = cancel   |   Del = clear");
        ImGui::Spacing();

        // --- Live preview ---
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        bool alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;

        // Build preview string from currently held modifiers + any valid key
        int heldMods = 0;
        if (ctrl)  heldMods |= MOD_CONTROL;
        if (alt)   heldMods |= MOD_ALT;
        if (shift) heldMods |= MOD_SHIFT;

        // Detect the trigger key (first valid non-modifier key that is down)
        int triggerKey = 0;
        for (int k = 0; k < 256; ++k) {
            if (k == VK_CONTROL || k == VK_SHIFT || k == VK_MENU ||
                k == VK_LCONTROL || k == VK_RCONTROL ||
                k == VK_LSHIFT   || k == VK_RSHIFT   ||
                k == VK_LMENU    || k == VK_RMENU)
                continue;
            if (!(GetKeyState(k) & 0x8000)) continue;
            // Accept: A-Z, 0-9, F1-F12
            if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9') ||
                (k >= VK_F1 && k <= VK_F12)) {
                triggerKey = k;
                break;
            }
        }

        // Display live preview
        if (triggerKey != 0 || ctrl || shift || alt) {
            std::string preview;
            if (ctrl)        preview += "Ctrl+";
            if (alt)         preview += "Alt+";
            if (shift)       preview += "Shift+";
            if (triggerKey)  preview += m_app.GetKeyName(triggerKey);
            else             preview += "...";

            if (!m_keybindingConflictMsg.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", preview.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", preview.c_str());
            }
        } else {
            ImGui::TextDisabled("—");
        }

        // Conflict warning
        if (!m_keybindingConflictMsg.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                               m_keybindingConflictMsg.c_str());
        }

        ImGui::Spacing();

        // --- Key press handling ---
        // Use ImGui key scan so we only react on the frame the key is first pressed.
        for (int k = 1; k < 256; ++k) {
            if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(k))) continue;

            if (k == VK_ESCAPE) {
                m_keybindingConflictMsg.clear();
                m_showKeybindingModal = false;
                break;
            }

            if (k == VK_DELETE) {
                preset->shortcutKey = 0;
                preset->shortcutModifiers = 0;
                m_keybindingConflictMsg.clear();
                m_showKeybindingModal = false;
                m_app.SaveConfig();
                break;
            }

            // Skip pure modifier keys
            if (k == VK_CONTROL || k == VK_SHIFT || k == VK_MENU ||
                k == VK_LCONTROL || k == VK_RCONTROL ||
                k == VK_LSHIFT   || k == VK_RSHIFT   ||
                k == VK_LMENU    || k == VK_RMENU)
                continue;

            // Accept: A-Z, 0-9, F1-F12
            if (!((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9') ||
                  (k >= VK_F1 && k <= VK_F12)))
                continue;

            int mods = 0;
            if (ctrl)  mods |= MOD_CONTROL;
            if (alt)   mods |= MOD_ALT;
            if (shift) mods |= MOD_SHIFT;

            // Duplicate check
            int conflict = m_app.IsBindingConflict(k, mods, m_keybindingPresetIndex);
            if (conflict >= 0) {
                const ShaderPreset* other = m_app.GetShaderManager().GetPreset(conflict);
                std::string otherName = other ? other->name : "another shader";
                m_keybindingConflictMsg = "Already bound to \"" + otherName +
                                          "\" — choose a different key.";
                break;  // Don't close; stay open so user can try again
            }

            // Commit
            preset->shortcutKey = k;
            preset->shortcutModifiers = mods;
            m_keybindingConflictMsg.clear();
            m_showKeybindingModal = false;
            m_app.SaveConfig();
            break;
        }

        ImGui::EndPopup();
    }
}
```

**Step 3: Build**

```bash
cmake --build build --config Release 2>&1 | tail -20
```

**Step 4: Manual verification**

Run the app. Open Shader Library → right-click a shader → "Set Keybinding...".
- Hold Ctrl — the live preview should show `Ctrl+...`.
- Press `A` — the live preview shows `Ctrl+A` and closes if unique.
- Try assigning the same combo to a second shader — a red warning should appear and the modal should remain open.
- Press Escape — modal closes without changes.
- Press Delete — binding is cleared.

**Step 5: Commit**

```bash
git add src/UIManager.h src/UIManager.cpp
git commit -m "feat: rewrite keybinding modal with live preview and duplicate blocking"
```

---

### Task 5: Add the Keybindings panel (F6)

**Files:**
- Modify: `src/UIManager.h` — add `m_showKeybindingsPanel`, `DrawKeybindingsPanel()`
- Modify: `src/UIManager.cpp` — implement `DrawKeybindingsPanel`, wire up in `Render()` and `DrawMenuBar()`

**Step 1: Update UIManager.h**

Add declaration after `DrawNewShaderModal()` (line 53):
```cpp
    void DrawKeybindingsPanel();
```

Add member after `m_showNewShaderModal` (line 63):
```cpp
    bool m_showKeybindingsPanel = false;
```

**Step 2: Wire into Render() in UIManager.cpp**

After the `if (m_showNewShaderModal)` block (line 122), add:
```cpp
    if (m_showKeybindingsPanel) {
        DrawKeybindingsPanel();
    }
```

**Step 3: Add View menu entry in DrawMenuBar()**

In `DrawMenuBar`, the View menu currently ends with "Recording Panel" (line 159). Add after it:
```cpp
            ImGui::MenuItem("Keybindings", "F6", &m_showKeybindingsPanel);
```

**Step 4: Handle F6 in Application::HandleKeyboardShortcuts**

Open `src/Application.cpp` and find `HandleKeyboardShortcuts`. Add a case for F6 that calls `m_uiManager->ToggleKeybindingsPanel()` — but since that method doesn't exist yet, instead expose the toggle via a simple public method on UIManager.

Add to `UIManager.h` public section:
```cpp
    void ToggleKeybindingsPanel() { m_showKeybindingsPanel = !m_showKeybindingsPanel; }
```

In `Application.cpp` inside `HandleKeyboardShortcuts` (near the F1/F2 handling), add:
```cpp
    case VK_F6:
        m_uiManager->ToggleKeybindingsPanel();
        break;
```

**Step 5: Implement DrawKeybindingsPanel()**

Add to `UIManager.cpp` after `DrawNewShaderModal`:

```cpp
void UIManager::DrawKeybindingsPanel() {
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Keybindings", &m_showKeybindingsPanel)) {
        auto& manager = m_app.GetShaderManager();
        const int count = manager.GetPresetCount();

        if (count == 0) {
            ImGui::TextDisabled("No shaders loaded.");
        } else if (ImGui::BeginTable("kb_table", 2,
                       ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                       ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Shader", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < count; ++i) {
                const ShaderPreset* preset = manager.GetPreset(i);
                if (!preset) continue;

                ImGui::TableNextRow();
                ImGui::PushID(i);

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(preset->name.c_str());

                ImGui::TableSetColumnIndex(1);
                if (preset->shortcutKey != 0) {
                    std::string combo = m_app.GetComboName(
                        preset->shortcutKey, preset->shortcutModifiers);
                    if (ImGui::Selectable(combo.c_str(), false,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        m_keybindingPresetIndex = i;
                        m_keybindingConflictMsg.clear();
                        m_showKeybindingModal = true;
                    }
                } else {
                    if (ImGui::Selectable("—", false,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        m_keybindingPresetIndex = i;
                        m_keybindingConflictMsg.clear();
                        m_showKeybindingModal = true;
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click to set keybinding");

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Click a row to assign a binding. Right-click a shader in the Library to do the same.");
    }
    ImGui::End();
}
```

**Step 6: Build**

```bash
cmake --build build --config Release 2>&1 | tail -20
```

**Step 7: Manual verification**

- Press F6 or use View → Keybindings. The panel opens.
- All loaded shaders appear with their current bindings (or `—`).
- Clicking a binding cell opens the improved modal from Task 4.
- F6 toggles the panel closed.

**Step 8: Commit**

```bash
git add src/UIManager.h src/UIManager.cpp src/Application.cpp
git commit -m "feat: add Keybindings panel (F6) with table view of all shader bindings"
```

---

### Task 6: Check for F6 in HandleKeyboardShortcuts (verify wiring)

**Files:**
- Read: `src/Application.cpp` — find `HandleKeyboardShortcuts`

**Step 1: Confirm F6 was added correctly in Task 5**

Search for `VK_F6` in Application.cpp. If it's missing, add it now following the same pattern as F1/F2/F3.

**Step 2: Final build and smoke test**

```bash
cmake --build build --config Release 2>&1 | tail -5
```

Run `build/Release/ShaderPlayer.exe` from the project root.

Checklist:
- [ ] Library rows show full combo e.g. `[Ctrl+1]`
- [ ] `[kb]` button appears on each row; clicking it opens the modal
- [ ] Modal shows live preview as keys are held
- [ ] Assigning a duplicate shows a red warning and keeps the modal open
- [ ] Valid unique binding closes the modal and saves
- [ ] Escape cancels, Delete clears
- [ ] F6 / View → Keybindings opens the panel
- [ ] Clicking a row in the panel opens the modal
- [ ] Right-click context menu still works

**Step 3: Commit if any fixups were needed**

```bash
git add -p
git commit -m "fix: keybinding wiring and final smoke test fixups"
```
