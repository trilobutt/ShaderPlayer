#pragma once

#include "Common.h"

#include <QByteArray>

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

    // Capture a QMainWindow::saveState() blob + visibility state and write to a new .ini
    // file. Returns the new preset index (>= 1), or -1 on failure.
    int SavePreset(const std::string& name, const QByteArray& state, const PanelVisibility& panels);

    // Load preset by index: fills `state` with the QMainWindow::saveState() blob (pass to
    // QMainWindow::restoreState) and `panels` with visibility flags.
    // Index 0 returns kDefaultLayoutState, which is empty until A18 captures the shipped
    // default from a hand-arranged window — the caller falls back to the programmatic
    // default layout when `state` comes back empty.
    bool LoadPreset(int index, QByteArray& state, PanelVisibility& panels);

    // Delete preset file and remove from vector. No-op for index 0 (Default).
    void DeletePreset(int index);

    // Assign keybinding. Returns false if the file could not be written. No-op for index 0.
    bool SetKeybinding(int index, int vkCode, int modifiers);

    const std::vector<WorkspacePreset>& GetPresets() const { return m_presets; }
    int GetPresetCount() const { return static_cast<int>(m_presets.size()); }

private:
    // Parse the [WorkspacePreset] header from a .ini file, including its `state=` key.
    // Returns true and fills `out` plus `stateBase64` only if the file is valid AND carries
    // a `state=` key; a file from the superseded layout format (no such key) returns false so
    // ScanDirectory skips it instead of crashing on it.
    bool ParsePresetFile(const std::string& filepath,
                         WorkspacePreset& out,
                         std::string& stateBase64) const;

    // Write a preset file from a WorkspacePreset and a base64-encoded
    // QMainWindow::saveState() blob.
    bool WritePresetFile(const WorkspacePreset& preset,
                         const std::string& stateBase64) const;

    // Sanitise a name for use as a filename (alphanumeric + underscores only).
    static std::string SanitiseName(const std::string& name);

    std::string m_layoutsDir;
    std::vector<WorkspacePreset> m_presets;  // index 0 is always Default
};

} // namespace SP
