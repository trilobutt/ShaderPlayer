# Workspace Presets Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Allow users to save, load, and recall named window layout presets (docking positions + panel visibility) with optional keybindings.

**Architecture:** New `WorkspaceManager` class owned by `Application` manages per-preset `.ini` files in a `layouts/` directory. ImGui layout blobs are captured via `SaveIniSettingsToMemory` / restored via `LoadIniSettingsFromMemory`. A built-in "Default" preset uses a hardcoded constant. Keybindings share the same dispatch loop as shader presets and use a unified conflict-detection method.

**Tech Stack:** C++20, ImGui docking branch, nlohmann/json, Win32 VK codes.

---

## Prerequisite Reading

Before starting:
- Read `src/Common.h` — understand `AppConfig`, `ShaderPreset`
- Read `src/Application.h` + `Application.cpp` lines 239–311 (`HandleKeyboardShortcuts`, `IsBindingConflict`)
- Read `src/UIManager.cpp` lines 143–210 (`DrawMenuBar`) and lines 631–750 (`DrawKeybindingModal`)
- Read `src/ConfigManager.cpp` — understand how `AppConfig` serialises
- Read `docs/plans/2026-02-22-workspace-presets-design.md` — full design reference

Build command (run from repo root):
```
cmake --build build --config Debug 2>&1 | tail -20
```
The build succeeds when the last line contains `ShaderPlayer.vcxproj -> ... ShaderPlayer.exe` with no errors.

---

## Task 1: Add WorkspacePreset to Common.h and AppConfig

**Files:**
- Modify: `src/Common.h`

**Step 1: Add `WorkspacePreset` struct**

In `src/Common.h`, after the closing `}` of `struct ShaderPreset` (around line 81), add:

```cpp
struct WorkspacePreset {
    std::string name;
    std::string filepath;        // absolute path to .ini file; empty = built-in Default
    int shortcutKey = 0;         // VK code; 0 = none
    int shortcutModifiers = 0;   // MOD_CONTROL | MOD_SHIFT | MOD_ALT bitmask
    bool showEditor = true;
    bool showLibrary = true;
    bool showTransport = true;
    bool showRecording = false;
    bool showKeybindingsPanel = false;
};
```

**Step 2: Add `layoutsDirectory` to `AppConfig`**

In `struct AppConfig` (around line 96), add one field after `shaderDirectory`:

```cpp
std::string layoutsDirectory = "layouts";
```

**Step 3: Build to verify no compile errors**

```
cmake --build build --config Debug 2>&1 | tail -10
```
Expected: build succeeds (no errors, no new warnings about WorkspacePreset).

**Step 4: Commit**

```bash
git add src/Common.h
git commit -m "feat: add WorkspacePreset struct and layoutsDirectory to AppConfig"
```

---

## Task 2: Create WorkspaceManager header

**Files:**
- Create: `src/WorkspaceManager.h`

**Step 1: Write the header**

```cpp
#pragma once

#include "Common.h"

namespace SP {

class WorkspaceManager {
public:
    WorkspaceManager();
    ~WorkspaceManager() = default;

    WorkspaceManager(const WorkspaceManager&) = delete;
    WorkspaceManager& operator=(const WorkspaceManager&) = delete;

    // Must be called once before any other method.
    // Creates the layouts directory if it doesn't exist.
    bool Initialize(const std::string& layoutsDirectory);

    // Scan the layouts directory and populate m_presets (index 0 is always Default).
    void ScanDirectory();

    // Capture current ImGui layout + visibility state and write to a new .ini file.
    // Returns the new preset index (>= 1), or -1 on failure.
    int SavePreset(const std::string& name,
                   bool showEditor, bool showLibrary, bool showTransport,
                   bool showRecording, bool showKeybindingsPanel);

    // Load preset by index: apply ImGui layout and return visibility flags.
    // Index 0 loads the hardcoded Default layout.
    bool LoadPreset(int index,
                    bool& showEditor, bool& showLibrary, bool& showTransport,
                    bool& showRecording, bool& showKeybindingsPanel);

    // Delete preset file and remove from vector. No-op for index 0 (Default).
    void DeletePreset(int index);

    // Rename preset: renames the .ini file and updates the struct. No-op for index 0.
    bool RenamePreset(int index, const std::string& newName);

    // Assign keybinding. No-op for index 0.
    void SetKeybinding(int index, int vkCode, int modifiers);

    const std::vector<WorkspacePreset>& GetPresets() const { return m_presets; }
    int GetPresetCount() const { return static_cast<int>(m_presets.size()); }

private:
    // Parse header section from a .ini file.
    // Returns true and fills `out` if the file is valid; leaves imguiBlock pointing
    // to the start of the ImGui ini content within fileContents.
    bool ParsePresetFile(const std::string& filepath,
                         WorkspacePreset& out,
                         std::string& imguiBlock) const;

    // Write a preset file from a WorkspacePreset and an ImGui ini blob.
    bool WritePresetFile(const WorkspacePreset& preset,
                         const std::string& imguiBlob) const;

    // Sanitise a name for use as a filename (alphanumeric + underscores only).
    static std::string SanitiseName(const std::string& name);

    std::string m_layoutsDir;
    std::vector<WorkspacePreset> m_presets;  // index 0 is always Default
};

} // namespace SP
```

**Step 2: Build to verify header compiles**

```
cmake --build build --config Debug 2>&1 | tail -10
```
Expected: build succeeds (WorkspaceManager.h is included nowhere yet, but header guards must be clean).

**Step 3: Commit**

```bash
git add src/WorkspaceManager.h
git commit -m "feat: add WorkspaceManager header"
```

---

## Task 3: Add WorkspaceManager to CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

**Step 1: Add source file to the executable target**

In `CMakeLists.txt`, find the `add_executable(ShaderPlayer WIN32 ...)` block (around line 95). Add `src/WorkspaceManager.cpp` to the source list, after `src/ConfigManager.cpp`:

```cmake
    src/WorkspaceManager.cpp
```

**Step 2: Build — expected to fail with "file not found"**

```
cmake --build build --config Debug 2>&1 | tail -5
```
Expected: CMake error about missing `WorkspaceManager.cpp`. This is correct — the file doesn't exist yet.

**Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add WorkspaceManager.cpp to executable sources"
```

---

## Task 4: Implement WorkspaceManager

**Files:**
- Create: `src/WorkspaceManager.cpp`

**Step 1: Write the implementation**

```cpp
#include "WorkspaceManager.h"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace SP {

// Hardcoded factory layout. To update: arrange the windows as desired,
// then copy the contents of imgui.ini (written to CWD by ImGui automatically)
// and paste it here, replacing everything between the quotes.
// IMPORTANT: Use raw string literal to avoid escaping issues.
static const char* const kDefaultLayoutIni = R"(
[Window][DockSpace]
Pos=0,0
Size=1280,720
Collapsed=0

[Window][Video]
Pos=0,19
Size=780,640
Collapsed=0
DockId=0x00000001,0

[Window][Shader Editor]
Pos=782,19
Size=498,450
Collapsed=0
DockId=0x00000002,0

[Window][Shader Library]
Pos=782,471
Size=498,208
Collapsed=0
DockId=0x00000003,0

[Window][Transport]
Pos=0,661
Size=780,59
Collapsed=0
DockId=0x00000004,0

[Docking][Data]
DockSpace     ID=0x7B8B77F5 Window=0x4647B76E Pos=0,19 Size=1280,701 Split=X Selected=0x995B0CF8
  DockNode    ID=0x00000001 Parent=0x7B8B77F5 SizeRef=780,701 Split=Y Selected=0x995B0CF8
    DockNode  ID=0x00000004 Parent=0x00000001 SizeRef=780,59 HiddenTabBar=1 Selected=0xF1B6D904
    DockNode  ID=0x00000005 Parent=0x00000001 SizeRef=780,640 CentralNode=1 HiddenTabBar=1 Selected=0x995B0CF8
  DockNode    ID=0x00000002 Parent=0x7B8B77F5 SizeRef=498,701 Split=Y Selected=0xA9E9B638
    DockNode  ID=0x00000006 Parent=0x00000002 SizeRef=498,450 HiddenTabBar=1 Selected=0xA9E9B638
    DockNode  ID=0x00000003 Parent=0x00000002 SizeRef=498,208 HiddenTabBar=1 Selected=0x1E3B62AB
)";
// NOTE: The above is a placeholder. Replace with actual imgui.ini content after
// running the app once and arranging windows in the desired factory layout.

WorkspaceManager::WorkspaceManager() {
    // Index 0 is always the built-in Default preset
    WorkspacePreset defaultPreset;
    defaultPreset.name = "Default";
    defaultPreset.filepath = "";  // no file
    m_presets.push_back(std::move(defaultPreset));
}

bool WorkspaceManager::Initialize(const std::string& layoutsDirectory) {
    m_layoutsDir = layoutsDirectory;

    // Resolve to absolute path if relative
    if (!std::filesystem::path(m_layoutsDir).is_absolute()) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        m_layoutsDir = (std::filesystem::path(exePath).parent_path() / m_layoutsDir).string();
    }

    std::filesystem::create_directories(m_layoutsDir);
    ScanDirectory();
    return true;
}

void WorkspaceManager::ScanDirectory() {
    // Keep index 0 (Default); clear user presets (indices 1+)
    if (m_presets.size() > 1)
        m_presets.erase(m_presets.begin() + 1, m_presets.end());

    if (!std::filesystem::exists(m_layoutsDir)) return;

    for (const auto& entry : std::filesystem::directory_iterator(m_layoutsDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ini") continue;

        WorkspacePreset preset;
        std::string imguiBlock;
        if (ParsePresetFile(entry.path().string(), preset, imguiBlock)) {
            preset.filepath = entry.path().string();
            m_presets.push_back(std::move(preset));
        }
    }
}

int WorkspaceManager::SavePreset(const std::string& name,
                                  bool showEditor, bool showLibrary,
                                  bool showTransport, bool showRecording,
                                  bool showKeybindingsPanel)
{
    // Capture current ImGui layout
    size_t iniSize = 0;
    const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    std::string imguiBlob(iniData, iniSize);

    WorkspacePreset preset;
    preset.name = name;
    preset.showEditor = showEditor;
    preset.showLibrary = showLibrary;
    preset.showTransport = showTransport;
    preset.showRecording = showRecording;
    preset.showKeybindingsPanel = showKeybindingsPanel;

    // Generate filepath
    std::string filename = SanitiseName(name) + ".ini";
    preset.filepath = (std::filesystem::path(m_layoutsDir) / filename).string();

    if (!WritePresetFile(preset, imguiBlob)) return -1;

    m_presets.push_back(preset);
    return static_cast<int>(m_presets.size()) - 1;
}

bool WorkspaceManager::LoadPreset(int index,
                                   bool& showEditor, bool& showLibrary,
                                   bool& showTransport, bool& showRecording,
                                   bool& showKeybindingsPanel)
{
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return false;

    const WorkspacePreset& preset = m_presets[index];

    // Apply visibility
    showEditor = preset.showEditor;
    showLibrary = preset.showLibrary;
    showTransport = preset.showTransport;
    showRecording = preset.showRecording;
    showKeybindingsPanel = preset.showKeybindingsPanel;

    if (index == 0) {
        // Built-in Default
        ImGui::LoadIniSettingsFromMemory(kDefaultLayoutIni);
        return true;
    }

    WorkspacePreset dummy;
    std::string imguiBlock;
    if (!ParsePresetFile(preset.filepath, dummy, imguiBlock)) return false;

    ImGui::LoadIniSettingsFromMemory(imguiBlock.c_str(), imguiBlock.size());
    return true;
}

void WorkspaceManager::DeletePreset(int index) {
    if (index <= 0 || index >= static_cast<int>(m_presets.size())) return;  // 0 = Default, protected
    std::filesystem::remove(m_presets[index].filepath);
    m_presets.erase(m_presets.begin() + index);
}

bool WorkspaceManager::RenamePreset(int index, const std::string& newName) {
    if (index <= 0 || index >= static_cast<int>(m_presets.size())) return false;

    WorkspacePreset& preset = m_presets[index];
    std::string newFilename = SanitiseName(newName) + ".ini";
    std::string newPath = (std::filesystem::path(m_layoutsDir) / newFilename).string();

    try {
        std::filesystem::rename(preset.filepath, newPath);
    } catch (...) {
        return false;
    }

    // Rewrite file with updated name in header
    preset.name = newName;
    preset.filepath = newPath;

    WorkspacePreset dummy;
    std::string imguiBlock;
    if (!ParsePresetFile(preset.filepath, dummy, imguiBlock)) return false;
    WritePresetFile(preset, imguiBlock);

    return true;
}

void WorkspaceManager::SetKeybinding(int index, int vkCode, int modifiers) {
    if (index <= 0 || index >= static_cast<int>(m_presets.size())) return;

    WorkspacePreset& preset = m_presets[index];
    preset.shortcutKey = vkCode;
    preset.shortcutModifiers = modifiers;

    // Rewrite file with updated keybinding in header
    WorkspacePreset dummy;
    std::string imguiBlock;
    if (!ParsePresetFile(preset.filepath, dummy, imguiBlock)) return;
    WritePresetFile(preset, imguiBlock);
}

bool WorkspaceManager::ParsePresetFile(const std::string& filepath,
                                        WorkspacePreset& out,
                                        std::string& imguiBlock) const
{
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::string line;
    bool inHeader = false;
    bool foundHeader = false;
    std::string imguiAccum;

    while (std::getline(file, line)) {
        // Strip trailing \r for Windows line endings
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line == "[WorkspacePreset]") {
            inHeader = true;
            foundHeader = true;
            continue;
        }

        if (inHeader) {
            // A new section header that isn't ours signals the end of the metadata
            if (!line.empty() && line[0] == '[') {
                inHeader = false;
                imguiAccum += line + '\n';
                continue;
            }
            // Parse key=value
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);

            if (key == "name")               out.name = val;
            else if (key == "shortcutKey")   out.shortcutKey = std::stoi(val);
            else if (key == "shortcutModifiers") out.shortcutModifiers = std::stoi(val);
            else if (key == "showEditor")         out.showEditor         = (val == "1");
            else if (key == "showLibrary")        out.showLibrary        = (val == "1");
            else if (key == "showTransport")      out.showTransport      = (val == "1");
            else if (key == "showRecording")      out.showRecording      = (val == "1");
            else if (key == "showKeybindingsPanel") out.showKeybindingsPanel = (val == "1");
        } else {
            imguiAccum += line + '\n';
        }
    }

    if (!foundHeader) return false;
    imguiBlock = std::move(imguiAccum);
    return true;
}

bool WorkspaceManager::WritePresetFile(const WorkspacePreset& preset,
                                        const std::string& imguiBlob) const
{
    std::ofstream file(preset.filepath);
    if (!file.is_open()) return false;

    file << "[WorkspacePreset]\n";
    file << "name=" << preset.name << '\n';
    file << "shortcutKey=" << preset.shortcutKey << '\n';
    file << "shortcutModifiers=" << preset.shortcutModifiers << '\n';
    file << "showEditor=" << (preset.showEditor ? 1 : 0) << '\n';
    file << "showLibrary=" << (preset.showLibrary ? 1 : 0) << '\n';
    file << "showTransport=" << (preset.showTransport ? 1 : 0) << '\n';
    file << "showRecording=" << (preset.showRecording ? 1 : 0) << '\n';
    file << "showKeybindingsPanel=" << (preset.showKeybindingsPanel ? 1 : 0) << '\n';
    file << '\n';
    file << imguiBlob;

    return file.good();
}

std::string WorkspaceManager::SanitiseName(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) result += c;
        else if (c == ' ' || c == '_' || c == '-') result += '_';
    }
    if (result.empty()) result = "preset";
    return result;
}

} // namespace SP
```

**Step 2: Build to verify it compiles**

```
cmake --build build --config Debug 2>&1 | tail -15
```
Expected: build succeeds. If there are `GetModuleFileNameA` errors, ensure `#include "Common.h"` is at the top (it includes `<windows.h>`).

**Step 3: Commit**

```bash
git add src/WorkspaceManager.cpp
git commit -m "feat: implement WorkspaceManager (save/load/delete/rename workspace presets)"
```

---

## Task 5: Update ConfigManager to serialise layoutsDirectory

**Files:**
- Modify: `src/ConfigManager.cpp`

**Step 1: Add `layoutsDirectory` to `to_json`**

In `ConfigManager.cpp`, find `void to_json(nlohmann::json& j, const AppConfig& c)` (around line 69). Add `layoutsDirectory` to the json object:

```cpp
{"layoutsDirectory", c.layoutsDirectory},
```

Add it after the `"shaderDirectory"` entry.

**Step 2: Add `layoutsDirectory` to `from_json`**

In `void from_json(const nlohmann::json& j, AppConfig& c)` (around line 85), add after the `shaderDirectory` line:

```cpp
if (j.contains("layoutsDirectory")) j.at("layoutsDirectory").get_to(c.layoutsDirectory);
```

**Step 3: Build and verify**

```
cmake --build build --config Debug 2>&1 | tail -10
```
Expected: build succeeds.

**Step 4: Commit**

```bash
git add src/ConfigManager.cpp
git commit -m "feat: serialise layoutsDirectory in config.json"
```

---

## Task 6: Wire WorkspaceManager into Application

**Files:**
- Modify: `src/Application.h`
- Modify: `src/Application.cpp`

**Step 1: Update `Application.h`**

Add `#include "WorkspaceManager.h"` near the top (after the existing includes).

Add the member to the private section (after `ConfigManager m_configManager;`):
```cpp
std::unique_ptr<WorkspaceManager> m_workspaceManager;
```

Replace the existing `IsBindingConflict` declaration with a new full-scope method:
```cpp
// Returns a human-readable conflict description, or empty string if free.
// excludeShaderIdx: shader preset index to skip (-1 = check all)
// excludeWorkspaceIdx: workspace preset index to skip (-1 = check all)
std::string FindBindingConflict(int vkCode, int modifiers,
                                 int excludeShaderIdx,
                                 int excludeWorkspaceIdx) const;
```

Add public accessors:
```cpp
WorkspaceManager& GetWorkspaceManager() { return *m_workspaceManager; }
```

Add public methods for workspace operations:
```cpp
void LoadWorkspacePreset(int index);
```

**Step 2: Update `Application::Initialize` in `Application.cpp`**

After the `m_shaderManager->ScanDirectory(...)` call (around line 77), add workspace manager initialisation:

```cpp
// Initialise workspace manager
m_workspaceManager = std::make_unique<WorkspaceManager>();
{
    auto& layoutsDir = m_configManager.GetConfig().layoutsDirectory;
    // Same fallback logic as shaderDirectory
    if (!std::filesystem::path(layoutsDir).is_absolute() &&
        !std::filesystem::exists(layoutsDir)) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        layoutsDir = (std::filesystem::path(exePath).parent_path() / "layouts").string();
    }
    m_workspaceManager->Initialize(layoutsDir);
}
```

**Step 3: Add `LoadWorkspacePreset` implementation**

Add this method before `Application::Shutdown()`:

```cpp
void Application::LoadWorkspacePreset(int index) {
    bool se, sl, st, sr, sk;
    if (m_workspaceManager->LoadPreset(index, se, sl, st, sr, sk)) {
        m_uiManager->ApplyVisibility(se, sl, st, sr, sk);
        const std::string& name = m_workspaceManager->GetPresets()[index].name;
        m_uiManager->ShowNotification("Workspace: " + name);
    }
}
```

**Step 4: Extend `HandleKeyboardShortcuts` to dispatch workspace keybindings**

In `Application::HandleKeyboardShortcuts` (around line 293), after the shader preset loop, add:

```cpp
// Check workspace preset keybindings (skip index 0 = Default, it has no shortcut)
for (int i = 1; i < m_workspaceManager->GetPresetCount(); ++i) {
    const WorkspacePreset& wp = m_workspaceManager->GetPresets()[i];
    if (wp.shortcutKey == 0) continue;

    bool modifiersMatch = true;
    if ((wp.shortcutModifiers & MOD_CONTROL) && !ctrl) modifiersMatch = false;
    if ((wp.shortcutModifiers & MOD_SHIFT)   && !shift) modifiersMatch = false;
    if ((wp.shortcutModifiers & MOD_ALT)     && !alt)   modifiersMatch = false;

    if (modifiersMatch && vkCode == static_cast<UINT>(wp.shortcutKey)) {
        LoadWorkspacePreset(i);
        return;
    }
}
```

**Step 5: Replace `IsBindingConflict` with `FindBindingConflict`**

Remove the old `IsBindingConflict` implementation entirely and replace with:

```cpp
std::string Application::FindBindingConflict(int vkCode, int modifiers,
                                              int excludeShaderIdx,
                                              int excludeWorkspaceIdx) const
{
    if (vkCode == 0) return {};

    // Hardcoded unmodified keys
    if (modifiers == 0) {
        switch (vkCode) {
        case VK_SPACE:  return "reserved for Play/Pause (Space)";
        case VK_ESCAPE: return "reserved for Reset to Passthrough (Escape)";
        case VK_F1:     return "reserved for Toggle Editor (F1)";
        case VK_F2:     return "reserved for Toggle Library (F2)";
        case VK_F3:     return "reserved for Toggle Transport (F3)";
        case VK_F4:     return "reserved for Toggle Recording (F4)";
        case VK_F5:     return "reserved for Compile (F5)";
        case VK_F6:     return "reserved for Toggle Keybindings (F6)";
        case VK_F9:     return "reserved for Start/Stop Recording (F9)";
        }
    }
    // Hardcoded Ctrl combos
    if (modifiers == MOD_CONTROL) {
        if (vkCode == 'O') return "reserved for Open Video (Ctrl+O)";
        if (vkCode == 'S') return "reserved for Save Shader (Ctrl+S)";
    }

    // Shader presets
    const auto& shaderPresets = m_shaderManager->GetPresets();
    for (int i = 0; i < static_cast<int>(shaderPresets.size()); ++i) {
        if (i == excludeShaderIdx) continue;
        const ShaderPreset& p = shaderPresets[i];
        if (p.shortcutKey == 0) continue;
        if (p.shortcutKey == vkCode && p.shortcutModifiers == modifiers)
            return "conflicts with shader \"" + p.name + "\"";
    }

    // Workspace presets
    if (m_workspaceManager) {
        const auto& wps = m_workspaceManager->GetPresets();
        for (int i = 0; i < static_cast<int>(wps.size()); ++i) {
            if (i == excludeWorkspaceIdx) continue;
            if (wps[i].shortcutKey == 0) continue;
            if (wps[i].shortcutKey == vkCode && wps[i].shortcutModifiers == modifiers)
                return "conflicts with workspace \"" + wps[i].name + "\"";
        }
    }

    return {};
}
```

**Step 6: Update the existing `IsBindingConflict` call site in `UIManager.cpp`**

In `UIManager.cpp` `DrawKeybindingModal` (around line 732), replace:
```cpp
int conflict = m_app.IsBindingConflict(triggerKey, mods, m_keybindingPresetIndex);
if (conflict >= 0) {
    const ShaderPreset* other = m_app.GetShaderManager().GetPreset(conflict);
    std::string otherName = other ? other->name : "another shader";
    m_keybindingConflictMsg = "Already bound to \"" + otherName +
                              "\" — choose a different key.";
} else {
```
with:
```cpp
std::string conflict = m_app.FindBindingConflict(triggerKey, mods,
                                                  m_keybindingPresetIndex, -1);
if (!conflict.empty()) {
    m_keybindingConflictMsg = conflict + " — choose a different key.";
} else {
```

**Step 7: Build to verify**

```
cmake --build build --config Debug 2>&1 | tail -15
```
Expected: build succeeds. If you get "IsBindingConflict undeclared", the old declaration in Application.h wasn't removed — double-check step 1.

**Step 8: Commit**

```bash
git add src/Application.h src/Application.cpp src/UIManager.cpp
git commit -m "feat: wire WorkspaceManager into Application; full-scope conflict detection"
```

---

## Task 7: Add ApplyVisibility to UIManager

**Files:**
- Modify: `src/UIManager.h`
- Modify: `src/UIManager.cpp`

**Step 1: Declare `ApplyVisibility` in `UIManager.h`**

In the public section, after the `ToggleKeybindingsPanel` method (around line 47):

```cpp
// Set all panel visibility flags atomically (used by workspace preset load).
void ApplyVisibility(bool showEditor, bool showLibrary, bool showTransport,
                     bool showRecording, bool showKeybindingsPanel);
```

**Step 2: Implement `ApplyVisibility` in `UIManager.cpp`**

Add this method after `UIManager::Render()` (around line 141):

```cpp
void UIManager::ApplyVisibility(bool showEditor, bool showLibrary, bool showTransport,
                                 bool showRecording, bool showKeybindingsPanel)
{
    m_showEditor           = showEditor;
    m_showLibrary          = showLibrary;
    m_showTransport        = showTransport;
    m_showRecording        = showRecording;
    m_showKeybindingsPanel = showKeybindingsPanel;
}
```

**Step 3: Build to verify**

```
cmake --build build --config Debug 2>&1 | tail -10
```
Expected: build succeeds.

**Step 4: Commit**

```bash
git add src/UIManager.h src/UIManager.cpp
git commit -m "feat: add UIManager::ApplyVisibility for workspace preset load"
```

---

## Task 8: Add Workspace Presets submenu to View menu

**Files:**
- Modify: `src/UIManager.h`
- Modify: `src/UIManager.cpp`

**Step 1: Add modal state to `UIManager.h`**

In the private section, after `m_showNewShaderModal` (around line 70), add:

```cpp
bool m_showManageWorkspacesModal = false;
bool m_showSaveWorkspacePopup = false;
char m_saveWorkspaceName[256] = "";

// State for workspace keybinding modal
bool m_showWorkspaceKeybindingModal = false;
int m_workspaceKeybindingIndex = -1;
std::string m_workspaceKeybindingConflictMsg;
```

Add a private method declaration:
```cpp
void DrawManageWorkspacesModal();
void DrawWorkspaceKeybindingModal();
```

**Step 2: Extend `DrawMenuBar` — add workspace submenu after View menu items**

In `UIManager::DrawMenuBar` (around line 163), find the `if (ImGui::BeginMenu("View"))` block. After the existing `ImGui::MenuItem` calls for Keybindings, add:

```cpp
ImGui::Separator();
if (ImGui::BeginMenu("Workspace Presets")) {
    auto& wm = m_app.GetWorkspaceManager();

    if (ImGui::MenuItem("Save Current As...")) {
        m_showSaveWorkspacePopup = true;
        memset(m_saveWorkspaceName, 0, sizeof(m_saveWorkspaceName));
    }

    ImGui::Separator();

    // List all presets (index 0 = Default first)
    const auto& presets = wm.GetPresets();
    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
        const auto& wp = presets[i];
        std::string label = wp.name;
        if (wp.shortcutKey != 0) {
            label += "  " + m_app.GetComboName(wp.shortcutKey, wp.shortcutModifiers);
        }
        if (ImGui::MenuItem(label.c_str())) {
            m_app.LoadWorkspacePreset(i);
        }
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Manage Workspaces...")) {
        m_showManageWorkspacesModal = true;
    }

    ImGui::EndMenu();
}
```

**Step 3: Wire up the save popup and modal rendering in `Render()`**

In `UIManager::Render()` (around line 126), after `if (m_showNewShaderModal)`, add:

```cpp
if (m_showManageWorkspacesModal) {
    DrawManageWorkspacesModal();
}
if (m_showWorkspaceKeybindingModal) {
    DrawWorkspaceKeybindingModal();
}

// "Save Current As" popup (inline, not a modal)
if (m_showSaveWorkspacePopup) {
    ImGui::OpenPopup("Save Workspace As");
    m_showSaveWorkspacePopup = false;
}
if (ImGui::BeginPopup("Save Workspace As")) {
    ImGui::Text("Workspace name:");
    ImGui::SetNextItemWidth(260.0f);
    bool submit = ImGui::InputText("##wsname", m_saveWorkspaceName,
                                   sizeof(m_saveWorkspaceName),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Save") || submit) {
        if (m_saveWorkspaceName[0] != '\0') {
            m_app.GetWorkspaceManager().SavePreset(
                m_saveWorkspaceName,
                m_showEditor, m_showLibrary, m_showTransport,
                m_showRecording, m_showKeybindingsPanel);
            m_app.ShowNotification("Workspace saved: " + std::string(m_saveWorkspaceName));
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
```

Note: `m_app.ShowNotification` doesn't exist — use `m_app.GetUI().ShowNotification(...)`. Actually, `m_app` IS the Application, and `ShowNotification` is on UIManager (`*this`). Use `ShowNotification(...)` directly.

**Step 4: Build to verify (will fail until DrawManageWorkspacesModal is stubbed)**

Add a stub in `UIManager.cpp`:
```cpp
void UIManager::DrawManageWorkspacesModal() {
    // Stubbed — implemented in Task 9
}
void UIManager::DrawWorkspaceKeybindingModal() {
    // Stubbed — implemented in Task 9
}
```

```
cmake --build build --config Debug 2>&1 | tail -15
```
Expected: build succeeds.

**Step 5: Commit**

```bash
git add src/UIManager.h src/UIManager.cpp
git commit -m "feat: add View > Workspace Presets submenu with save popup and load list"
```

---

## Task 9: Implement Manage Workspaces modal and keybinding modal

**Files:**
- Modify: `src/UIManager.cpp`

**Step 1: Replace the stub `DrawManageWorkspacesModal`**

Replace the stub with the full implementation. Add after `DrawShaderParameters()`:

```cpp
void UIManager::DrawManageWorkspacesModal() {
    ImGui::OpenPopup("Manage Workspaces");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500.0f, 360.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Manage Workspaces", &m_showManageWorkspacesModal)) {
        auto& wm = m_app.GetWorkspaceManager();
        const auto& presets = wm.GetPresets();

        ImGui::TextDisabled("Right-click a workspace to rename, delete, or set a keybinding.");
        ImGui::Spacing();

        if (ImGui::BeginTable("##wspresets", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY, ImVec2(0, 260.0f)))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Keybinding", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
                const auto& wp = presets[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s%s", wp.name.c_str(), i == 0 ? " (built-in)" : "");
                ImGui::TableSetColumnIndex(1);
                if (wp.shortcutKey != 0)
                    ImGui::Text("%s", m_app.GetComboName(wp.shortcutKey, wp.shortcutModifiers).c_str());
                else
                    ImGui::TextDisabled("—");

                if (i > 0) {  // Default is not editable
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::BeginPopupContextItem(("##wsctx" + std::to_string(i)).c_str())) {
                        if (ImGui::MenuItem("Set Keybinding...")) {
                            m_workspaceKeybindingIndex = i;
                            m_workspaceKeybindingConflictMsg.clear();
                            m_showWorkspaceKeybindingModal = true;
                        }
                        if (ImGui::MenuItem("Delete")) {
                            wm.DeletePreset(i);
                            ImGui::EndPopup();
                            break;  // vector modified; exit loop
                        }
                        ImGui::EndPopup();
                    }
                }
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(100, 0))) {
            m_showManageWorkspacesModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopupModal();
    }
}
```

**Step 2: Replace the stub `DrawWorkspaceKeybindingModal`**

This follows the same pattern as `DrawKeybindingModal` for shaders. Replace the stub:

```cpp
void UIManager::DrawWorkspaceKeybindingModal() {
    ImGui::OpenPopup("Set Workspace Keybinding");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(340.0f, 200.0f), ImGuiCond_Appearing);

    static bool s_wasOpen = false;
    static int  s_prevTrigger = 0;
    static bool s_prevEsc = false;
    static bool s_prevDel = false;

    bool open = true;
    if (ImGui::BeginPopupModal("Set Workspace Keybinding", &open)) {
        if (!s_wasOpen) { s_prevTrigger = 0; s_wasOpen = true; }

        auto& wm = m_app.GetWorkspaceManager();
        if (m_workspaceKeybindingIndex < 0 ||
            m_workspaceKeybindingIndex >= wm.GetPresetCount())
        {
            m_showWorkspaceKeybindingModal = false;
            s_wasOpen = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopupModal();
            return;
        }

        const WorkspacePreset& wp = wm.GetPresets()[m_workspaceKeybindingIndex];
        ImGui::Text("Workspace: %s", wp.name.c_str());
        ImGui::TextDisabled("Press a key combination. Delete = clear. Escape = cancel.");
        ImGui::Spacing();

        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        bool alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;

        // Scan for a non-modifier key press
        int triggerKey = 0;
        for (int vk = 0x30; vk <= 0x5A; ++vk) {
            if (GetKeyState(vk) & 0x8000) { triggerKey = vk; break; }
        }
        if (triggerKey == 0) {
            for (int vk = VK_F1; vk <= VK_F12; ++vk) {
                if (GetKeyState(vk) & 0x8000) { triggerKey = vk; break; }
            }
        }

        if (triggerKey != s_prevTrigger) m_workspaceKeybindingConflictMsg.clear();

        if (triggerKey != 0 || ctrl || shift || alt) {
            std::string preview;
            if (ctrl)       preview += "Ctrl+";
            if (alt)        preview += "Alt+";
            if (shift)      preview += "Shift+";
            if (triggerKey) preview += m_app.GetKeyName(triggerKey);
            else            preview += "...";

            if (!m_workspaceKeybindingConflictMsg.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", preview.c_str());
            else
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", preview.c_str());
        }

        if (!m_workspaceKeybindingConflictMsg.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                               m_workspaceKeybindingConflictMsg.c_str());
        }

        ImGui::Spacing();
        bool escDown = (GetKeyState(VK_ESCAPE) & 0x8000) != 0;
        bool delDown = (GetKeyState(VK_DELETE) & 0x8000) != 0;

        if (escDown && !s_prevEsc) {
            m_workspaceKeybindingConflictMsg.clear();
            m_showWorkspaceKeybindingModal = false;
            s_wasOpen = false;
            ImGui::CloseCurrentPopup();
        } else if (delDown && !s_prevDel) {
            wm.SetKeybinding(m_workspaceKeybindingIndex, 0, 0);
            m_workspaceKeybindingConflictMsg.clear();
            m_showWorkspaceKeybindingModal = false;
            s_wasOpen = false;
            ImGui::CloseCurrentPopup();
        } else if (triggerKey != 0 && triggerKey != s_prevTrigger) {
            int mods = 0;
            if (ctrl)  mods |= MOD_CONTROL;
            if (alt)   mods |= MOD_ALT;
            if (shift) mods |= MOD_SHIFT;

            std::string conflict = m_app.FindBindingConflict(
                triggerKey, mods, -1, m_workspaceKeybindingIndex);
            if (!conflict.empty()) {
                m_workspaceKeybindingConflictMsg = conflict + " — choose a different key.";
            } else {
                wm.SetKeybinding(m_workspaceKeybindingIndex, triggerKey, mods);
                m_workspaceKeybindingConflictMsg.clear();
                m_showWorkspaceKeybindingModal = false;
                s_wasOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }

        s_prevTrigger = triggerKey;
        s_prevEsc = escDown;
        s_prevDel = delDown;

        ImGui::EndPopupModal();
    }

    if (!open) {
        m_showWorkspaceKeybindingModal = false;
        s_wasOpen = false;
    }
}
```

**Step 3: Build to verify**

```
cmake --build build --config Debug 2>&1 | tail -15
```
Expected: build succeeds.

**Step 4: Commit**

```bash
git add src/UIManager.cpp
git commit -m "feat: implement Manage Workspaces modal and workspace keybinding modal"
```

---

## Task 10: Capture the factory default layout

This step requires running the app. The `kDefaultLayoutIni` constant in `WorkspaceManager.cpp` is currently a placeholder that may not match your actual window layout.

**Step 1: Run the app and arrange windows**

```
build\Debug\ShaderPlayer.exe
```

Arrange the windows into the desired factory layout (Video panel large on the left, Editor and Library stacked on the right, Transport at the bottom).

**Step 2: Copy the imgui.ini content**

ImGui auto-saves the layout to `imgui.ini` in the working directory. Open it with a text editor and copy the full contents.

**Step 3: Replace the placeholder constant**

In `src/WorkspaceManager.cpp`, replace the contents of `kDefaultLayoutIni` (the raw string between `R"(` and `)"`) with the actual imgui.ini content you copied.

**Step 4: Build and verify**

```
cmake --build build --config Debug 2>&1 | tail -10
```
Expected: build succeeds.

**Step 5: Test the Default preset**

Run the app, move windows around into a different layout, then go to View > Workspace Presets > Default. Windows should snap back to the factory layout.

**Step 6: Commit**

```bash
git add src/WorkspaceManager.cpp
git commit -m "feat: capture factory default layout into WorkspaceManager constant"
```

---

## Task 11: Manual end-to-end test

**Run:** `build\Debug\ShaderPlayer.exe`

**Test checklist:**

- [ ] View > Workspace Presets menu appears with "Save Current As...", "Default", "Manage Workspaces..."
- [ ] Click Default → layout resets to factory arrangement
- [ ] Rearrange windows, then View > Workspace Presets > Save Current As... → enter name "Editing" → saved
- [ ] "Editing" appears in the list; click it → layout restores
- [ ] After restart, "Editing" still appears (file persists in `layouts/`)
- [ ] Manage Workspaces → right-click "Editing" → Set Keybinding → press Ctrl+1 → assigned
- [ ] Press Ctrl+1 → workspace loads, notification shows "Workspace: Editing"
- [ ] Try assigning F5 to a workspace → conflict message "reserved for Compile (F5)"
- [ ] Try assigning a key already used by a shader preset → conflict message shown
- [ ] Manage Workspaces → Delete "Editing" → it disappears from the list

**Step: Final commit (if any fixes needed)**

```bash
git add -p  # stage only intentional changes
git commit -m "fix: workspace preset end-to-end fixes"
```

---

## Summary of Files Changed

| File | Change |
|------|--------|
| `src/Common.h` | `WorkspacePreset` struct; `layoutsDirectory` in `AppConfig` |
| `src/WorkspaceManager.h` | New header |
| `src/WorkspaceManager.cpp` | New implementation |
| `src/Application.h` | `m_workspaceManager` member; `FindBindingConflict`; `LoadWorkspacePreset` |
| `src/Application.cpp` | Init WorkspaceManager; extend keyboard dispatch; replace conflict detection |
| `src/UIManager.h` | `ApplyVisibility`; modal state; `DrawManageWorkspacesModal`; `DrawWorkspaceKeybindingModal` |
| `src/UIManager.cpp` | Extend DrawMenuBar; implement all new draw methods; update conflict call site |
| `src/ConfigManager.cpp` | Serialise `layoutsDirectory` |
| `CMakeLists.txt` | Add `WorkspaceManager.cpp` |
