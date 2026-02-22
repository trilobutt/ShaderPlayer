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
