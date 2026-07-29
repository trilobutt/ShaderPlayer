#include "WorkspaceManager.h"
#include "imgui.h"
#include <charconv>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace SP {

// Hardcoded factory layout. To update, arrange the windows as desired, save a
// workspace preset, then paste that .ini file's ImGui section here (drop the
// [WorkspacePreset] header and the LastUsed lines) and mirror its visibility
// flags in the WorkspaceManager constructor below.
static const char* const kDefaultLayoutIni = R"INI(
[Window][DockSpace]
Pos=0,0
Size=3840,2126
Collapsed=0

[Window][Video]
Pos=603,29
Size=2224,1798
Collapsed=0
DockId=0x00000003,0

[Window][Shader Library]
Pos=2829,29
Size=1011,2097
Collapsed=0
DockId=0x00000002,0

[Window][Shader Parameters]
Pos=0,29
Size=601,2097
Collapsed=0
DockId=0x00000005,0

[Window][Transport]
Pos=603,1829
Size=2224,73
Collapsed=0
DockId=0x00000007,0

[Window][Shader Editor]
Pos=2829,29
Size=1011,2097
Collapsed=0
DockId=0x00000002,1

[Window][Recording Settings]
Pos=603,1904
Size=2224,222
Collapsed=0
DockId=0x00000008,0

[Window][Noise Generator]
Pos=60,60
Size=505,327
Collapsed=0

[Docking][Data]
DockSpace         ID=0xD71539A0 Window=0x3DA2F1DE Pos=0,29 Size=3840,2097 Split=X
  DockNode        ID=0x00000005 Parent=0xD71539A0 SizeRef=601,2097 Selected=0xAF40F580
  DockNode        ID=0x00000006 Parent=0xD71539A0 SizeRef=3237,2097 Split=X
    DockNode      ID=0x00000001 Parent=0x00000006 SizeRef=2224,2097 Split=Y Selected=0x954F7004
      DockNode    ID=0x00000003 Parent=0x00000001 SizeRef=3037,1798 CentralNode=1 Selected=0x954F7004
      DockNode    ID=0x00000004 Parent=0x00000001 SizeRef=3037,297 Split=Y Selected=0x04C1370C
        DockNode  ID=0x00000007 Parent=0x00000004 SizeRef=2224,73 Selected=0x04C1370C
        DockNode  ID=0x00000008 Parent=0x00000004 SizeRef=2224,222 Selected=0x00A96232
    DockNode      ID=0x00000002 Parent=0x00000006 SizeRef=1011,2097 Selected=0x5DAA59D9
)INI";

WorkspaceManager::WorkspaceManager() {
    // Index 0 is always the built-in Default preset
    WorkspacePreset defaultPreset;
    defaultPreset.name = "Default";
    defaultPreset.filepath = "";  // no file
    // Must match the panels present in kDefaultLayoutIni above — every panel with
    // a dock node in that blob has to be visible, or ImGui drops the empty node.
    defaultPreset.panels.editor      = true;
    defaultPreset.panels.library     = true;
    defaultPreset.panels.transport   = true;
    defaultPreset.panels.recording   = true;
    defaultPreset.panels.keybindings = false;
    defaultPreset.panels.noise       = false;
    defaultPreset.panels.spout       = false;
    defaultPreset.panels.audio       = false;
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

    std::error_code ec;
    std::filesystem::create_directories(m_layoutsDir, ec);
    if (ec) return false;
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

int WorkspaceManager::SavePreset(const std::string& name, const PanelVisibility& panels)
{
    // Capture current ImGui layout
    size_t iniSize = 0;
    const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    std::string imguiBlob(iniData, iniSize);

    WorkspacePreset preset;
    preset.name = name;
    preset.panels = panels;

    // Generate filepath
    std::string filename = SanitiseName(name) + ".ini";
    preset.filepath = (std::filesystem::path(m_layoutsDir) / filename).string();

    // Ensure unique filepath: append suffix if the sanitised name collides on disk
    // but is not already tracked in our vector (different name, same sanitised form).
    if (std::filesystem::exists(preset.filepath)) {
        bool inVector = false;
        for (int i = 1; i < static_cast<int>(m_presets.size()); ++i) {
            if (m_presets[i].filepath == preset.filepath) { inVector = true; break; }
        }
        if (!inVector) {
            int suffix = 2;
            while (std::filesystem::exists(preset.filepath)) {
                filename = SanitiseName(name) + "_" + std::to_string(suffix++) + ".ini";
                preset.filepath = (std::filesystem::path(m_layoutsDir) / filename).string();
            }
        }
    }

    // Preserve existing keybinding if overwriting a same-name preset
    for (int i = 1; i < static_cast<int>(m_presets.size()); ++i) {
        if (m_presets[i].filepath == preset.filepath) {
            preset.shortcutKey      = m_presets[i].shortcutKey;
            preset.shortcutModifiers = m_presets[i].shortcutModifiers;
            break;
        }
    }

    if (!WritePresetFile(preset, imguiBlob)) return -1;

    // If a preset with this filepath already exists, update it in-place
    for (int i = 1; i < static_cast<int>(m_presets.size()); ++i) {
        if (m_presets[i].filepath == preset.filepath) {
            m_presets[i] = preset;
            return i;
        }
    }

    m_presets.push_back(preset);
    return static_cast<int>(m_presets.size()) - 1;
}

bool WorkspaceManager::LoadPreset(int index, PanelVisibility& panels)
{
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return false;

    const WorkspacePreset& preset = m_presets[index];
    panels = preset.panels;

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
    if (index <= 0 || index >= static_cast<int>(m_presets.size())) return;
    std::filesystem::remove(m_presets[index].filepath);
    m_presets.erase(m_presets.begin() + index);
}

bool WorkspaceManager::SetKeybinding(int index, int vkCode, int modifiers) {
    if (index <= 0 || index >= static_cast<int>(m_presets.size())) return false;

    WorkspacePreset& preset = m_presets[index];
    preset.shortcutKey = vkCode;
    preset.shortcutModifiers = modifiers;

    WorkspacePreset dummy;
    std::string imguiBlock;
    if (!ParsePresetFile(preset.filepath, dummy, imguiBlock)) return false;
    return WritePresetFile(preset, imguiBlock);
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
            else if (key == "shortcutKey") {
                std::from_chars(val.data(), val.data() + val.size(), out.shortcutKey);
            }
            else if (key == "shortcutModifiers") {
                std::from_chars(val.data(), val.data() + val.size(), out.shortcutModifiers);
            }
            else if (key == "showEditor")           out.panels.editor      = (val == "1");
            else if (key == "showLibrary")          out.panels.library     = (val == "1");
            else if (key == "showTransport")        out.panels.transport   = (val == "1");
            else if (key == "showRecording")        out.panels.recording   = (val == "1");
            else if (key == "showKeybindingsPanel") out.panels.keybindings = (val == "1");
            else if (key == "showNoisePanel")       out.panels.noise       = (val == "1");
            else if (key == "showSpoutPanel")       out.panels.spout       = (val == "1");
            else if (key == "showAudioPanel")       out.panels.audio       = (val == "1");
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
    file << "showEditor=" << (preset.panels.editor ? 1 : 0) << '\n';
    file << "showLibrary=" << (preset.panels.library ? 1 : 0) << '\n';
    file << "showTransport=" << (preset.panels.transport ? 1 : 0) << '\n';
    file << "showRecording=" << (preset.panels.recording ? 1 : 0) << '\n';
    file << "showKeybindingsPanel=" << (preset.panels.keybindings ? 1 : 0) << '\n';
    file << "showNoisePanel=" << (preset.panels.noise ? 1 : 0) << '\n';
    file << "showSpoutPanel=" << (preset.panels.spout ? 1 : 0) << '\n';
    file << "showAudioPanel=" << (preset.panels.audio ? 1 : 0) << '\n';
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
