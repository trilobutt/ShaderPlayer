# Workspace Presets — Design Document

**Date:** 2026-02-22
**Status:** Approved

---

## Overview

Allow users to save and restore named window layout presets (docking positions, panel visibility) with optional keybindings for instant recall. Presets are stored as `.ini` files in a `layouts/` directory next to the executable. A built-in "Default" preset resets to the factory layout without touching disk.

---

## Requirements

- **Captures:** ImGui docking layout + panel visibility (editor, library, transport, recording, keybindings panel)
- **Storage:** Per-preset `.ini` files in `layouts/` next to the executable
- **File format:** Custom ini with a `[WorkspacePreset]` metadata header followed by the raw ImGui ini blob
- **UI entry point:** View menu > Workspace Presets submenu
- **Keybindings:** Optional per-preset shortcut key, same mechanism as shader presets
- **Conflict detection:** Full scope — hardcoded keys, shader presets, and workspace presets
- **Built-in Default preset:** Hardcoded factory layout, not backed by a file, always index 0

---

## File Format

Each user-created preset is a single `.ini` file:

```ini
[WorkspacePreset]
name=Editing
shortcutKey=49
shortcutModifiers=2
showEditor=1
showLibrary=1
showTransport=1
showRecording=0
showKeybindingsPanel=0

[Window][Video]
Pos=0,19
Size=1200,800
... (verbatim ImGui ini content)
```

The `[WorkspacePreset]` header is parsed line-by-line until the first `[Window]` token. Everything from that point onward is passed verbatim to `ImGui::LoadIniSettingsFromMemory()`.

Filenames are the sanitised preset name (spaces replaced with underscores, non-alphanumeric characters stripped) with a `.ini` extension.

---

## Data Model

Add to `Common.h`:

```cpp
struct WorkspacePreset {
    std::string name;
    std::string filepath;        // absolute path; empty for the built-in Default
    int shortcutKey = 0;         // VK code; 0 = none
    int shortcutModifiers = 0;   // MOD_CONTROL, MOD_SHIFT, MOD_ALT bitmask
    bool showEditor = true;
    bool showLibrary = true;
    bool showTransport = true;
    bool showRecording = false;
    bool showKeybindingsPanel = false;
};
```

The ImGui ini blob is never stored in the struct — it is read from the file at load time and discarded immediately after `LoadIniSettingsFromMemory`.

---

## New Component: WorkspaceManager

`src/WorkspaceManager.{cpp,h}` — owned by `Application` (alongside `ShaderManager`).

### Public API

```cpp
bool Initialize(const std::string& layoutsDirectory);

// Enumerate all .ini files in the layouts directory (skips Default slot)
void ScanDirectory();

// Save current ImGui layout + provided visibility state as a new preset
// Returns the new preset index, or -1 on failure
int SavePreset(const std::string& name,
               bool showEditor, bool showLibrary, bool showTransport,
               bool showRecording, bool showKeybindingsPanel);

// Load preset by index; applies layout to ImGui and returns visibility flags
bool LoadPreset(int index,
                bool& showEditor, bool& showLibrary, bool& showTransport,
                bool& showRecording, bool& showKeybindingsPanel);

void DeletePreset(int index);          // removes file; index 0 (Default) is a no-op
bool RenamePreset(int index, const std::string& newName);  // renames file
bool SetKeybinding(int index, int vkCode, int modifiers);

const std::vector<WorkspacePreset>& GetPresets() const;
int GetPresetCount() const;
```

### Built-in Default Preset

Index 0 is always the Default preset (`filepath = ""`). `LoadPreset(0)` calls `ImGui::LoadIniSettingsFromMemory(kDefaultLayoutIni)` where `kDefaultLayoutIni` is a `static const char* const` embedded in `WorkspaceManager.cpp`. This string is captured once by arranging the desired factory layout and copying the contents of `imgui.ini`.

Default cannot be deleted or renamed. `SavePreset` always appends after index 0; user-created presets start at index 1.

---

## Conflict Detection

`Application::FindWorkspaceKeybindingConflict(int vkCode, int modifiers, int excludeIndex)` checks in order:

1. **Hardcoded unmodified keys:** Space (VK_SPACE), Escape, F1–F6, F9
2. **Hardcoded Ctrl combos:** Ctrl+O, Ctrl+S
3. **All shader presets** with a non-zero `shortcutKey`
4. **All workspace presets** with a non-zero `shortcutKey` (excluding `excludeIndex`)

Returns a human-readable conflict description ("reserved for Compile (F5)", "conflicts with shader 'Grayscale'", "conflicts with workspace 'Editing'") or an empty string if clear.

The existing `FindConflictingPreset` for shader keybindings is extended similarly to check workspace presets and hardcoded keys.

---

## UI: View Menu Extension

```
View
├── Shader Editor           F1  (checkbox)
├── Shader Library          F2  (checkbox)
├── Transport Controls      F3  (checkbox)
├── Recording Panel         F4  (checkbox)
├── Keybindings             F6  (checkbox)
├── ────────────────────────────
└── Workspace Presets      ▶
    ├── Save Current As...
    ├── ────────────────────
    ├── ● Default              (bold/bullet if active)
    ├──   Editing      Ctrl+1
    ├──   Grading      Ctrl+2
    └── Manage Workspaces...
```

"Save Current As..." opens an inline input popup for a name, then calls `WorkspaceManager::SavePreset`.

"Manage Workspaces..." opens a modal listing all user presets (not Default) with rename, delete, and keybinding-assign actions. Right-click on a preset row opens a context menu identical in structure to the shader library's right-click menu.

---

## Keyboard Dispatch

`Application::HandleKeyboardShortcuts` is extended with a workspace preset loop after the shader preset loop:

```cpp
for (int i = 1; i < m_workspaceManager->GetPresetCount(); ++i) {
    const auto& wp = m_workspaceManager->GetPresets()[i];
    if (wp.shortcutKey == 0) continue;
    // check modifier match
    if (modifiersMatch && vkCode == static_cast<UINT>(wp.shortcutKey)) {
        bool se, sl, st, sr, sk;
        m_workspaceManager->LoadPreset(i, se, sl, st, sr, sk);
        m_uiManager->ApplyVisibility(se, sl, st, sr, sk);
        m_uiManager->ShowNotification("Workspace: " + wp.name);
        return;
    }
}
```

A new `UIManager::ApplyVisibility(...)` method sets all five visibility booleans atomically to avoid partial state.

---

## Config Changes

`AppConfig` gains one field:

```cpp
std::string layoutsDirectory = "layouts";  // relative to exe, resolved at startup
```

`ConfigManager` serialises/deserialises this field. At startup, `Application::Initialize` resolves it to an absolute path (same fallback logic as `shaderDirectory`) and passes it to `WorkspaceManager::Initialize`.

The preset files themselves are not referenced in `config.json` — they are discovered by `ScanDirectory()` at startup.

---

## Files Changed

| File | Change |
|------|--------|
| `src/Common.h` | Add `WorkspacePreset` struct; add `layoutsDirectory` to `AppConfig` |
| `src/WorkspaceManager.h` | New file |
| `src/WorkspaceManager.cpp` | New file; contains `kDefaultLayoutIni` constant |
| `src/Application.h` | Add `m_workspaceManager` member; declare new methods |
| `src/Application.cpp` | Initialize WorkspaceManager; extend HandleKeyboardShortcuts; extend conflict detection |
| `src/UIManager.h` | Add `ApplyVisibility()`; add Manage Workspaces modal state |
| `src/UIManager.cpp` | Extend DrawMenuBar; add DrawManageWorkspacesModal |
| `src/ConfigManager.cpp` | Serialise/deserialise `layoutsDirectory` |
| `CMakeLists.txt` | Add WorkspaceManager.cpp to sources |

No changes to D3D11Renderer, ShaderManager, VideoDecoder, or VideoEncoder.
