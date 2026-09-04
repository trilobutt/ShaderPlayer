#include "WorkspaceManager.h"
#include <charconv>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace SP {

// Hardcoded factory layout, as a base64 QMainWindow::saveState() blob: Parameters left,
// Library over Editor right, Viewport centre, Transport and Recording along the bottom.
// Captured from a hand-arranged window, so it is opaque by nature — to change it, arrange
// the window, save a workspace preset, and paste that .ini file's `state=` value here.
//
// The panel flags on the built-in Default in the constructor below MUST match the docks
// this blob contains. Qt deletes an empty dock area and redistributes its space, so a
// panel hidden here but marked visible there lands somewhere arbitrary.
static const char* const kDefaultLayoutState =
    "AAAA/wAAAAH9AAAAAwAAAAAAAAG8AAADDfwCAAAAAfsAAAAUAGQAbwBjAGsAUABhAHIAYQBt"
    "AHMBAAAAHgAAAw0AAADeAP///wAAAAEAAAGQAAADDfwCAAAAAfwAAAAeAAADDQAAAUMBAAAd"
    "+gAAAAABAAAAAvsAAAAWAGQAbwBjAGsATABpAGIAcgBhAHIAeQEAAAXwAAABkAAAAN0A////"
    "+wAAABQAZABvAGMAawBFAGQAaQB0AG8AcgEAAAAA/////wAAALwA////AAAAAwAAB4AAAAFi"
    "/AEAAAAB/AAAAAAAAAeAAAABTgD////6AAAAAAEAAAAF+wAAABoAZABvAGMAawBUAHIAYQBu"
    "AHMAcABvAHIAdAEAAAAA/////wAAAU4A////+wAAABoAZABvAGMAawBSAGUAYwBvAHIAZABp"
    "AG4AZwEAAAAA/////wAAAPsA////+wAAABIAZABvAGMAawBOAG8AaQBzAGUAAAAAAP////8A"
    "AADuAP////sAAAASAGQAbwBjAGsAUwBwAG8AdQB0AAAAAAD/////AAABBwD////7AAAAEgBk"
    "AG8AYwBrAEEAdQBkAGkAbwAAAAAA/////wAAAOcA////AAAEHAAAAw0AAAAEAAAABAAAAAgA"
    "AAAI/AAAAAA=";

WorkspaceManager::WorkspaceManager() {
    // Index 0 is always the built-in Default preset
    WorkspacePreset defaultPreset;
    defaultPreset.name = "Default";
    defaultPreset.filepath = "";  // no file
    defaultPreset.panels.editor      = true;
    defaultPreset.panels.library     = true;
    defaultPreset.panels.transport   = true;
    defaultPreset.panels.recording   = false;
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

    std::error_code ec;
    if (!std::filesystem::is_directory(m_layoutsDir, ec) || ec) return;

    for (const auto& entry : std::filesystem::directory_iterator(m_layoutsDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ini") continue;

        WorkspacePreset preset;
        std::string stateBase64;
        if (ParsePresetFile(entry.path().string(), preset, stateBase64)) {
            preset.filepath = entry.path().string();
            m_presets.push_back(std::move(preset));
        }
    }
}

int WorkspaceManager::SavePreset(const std::string& name, const QByteArray& state,
                                  const PanelVisibility& panels)
{
    const std::string stateBase64 = state.toBase64().toStdString();

    WorkspacePreset preset;
    preset.name = name;
    preset.panels = panels;

    // Generate filepath
    std::string filename = SanitiseName(name) + ".ini";
    preset.filepath = (std::filesystem::path(m_layoutsDir) / filename).string();

    // Ensure unique filepath: append suffix if the sanitised name collides on disk
    // but is not already tracked in our vector (different name, same sanitised form).
    std::error_code ec;
    if (std::filesystem::exists(preset.filepath, ec)) {
        bool inVector = false;
        for (int i = 1; i < static_cast<int>(m_presets.size()); ++i) {
            if (m_presets[i].filepath == preset.filepath) { inVector = true; break; }
        }
        if (!inVector) {
            int suffix = 2;
            while (std::filesystem::exists(preset.filepath, ec)) {
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

    if (!WritePresetFile(preset, stateBase64)) return -1;

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

bool WorkspaceManager::LoadPreset(int index, QByteArray& state, PanelVisibility& panels)
{
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return false;

    const WorkspacePreset& preset = m_presets[index];
    panels = preset.panels;

    if (index == 0) {
        // Built-in Default: the captured factory layout above. MainWindow still falls
        // back to its programmatic layout if this ever decodes to nothing.
        state = QByteArray::fromBase64(QByteArray(kDefaultLayoutState));
        return true;
    }

    WorkspacePreset dummy;
    std::string stateBase64;
    if (!ParsePresetFile(preset.filepath, dummy, stateBase64)) return false;

    state = QByteArray::fromBase64(QByteArray::fromStdString(stateBase64));
    return true;
}

void WorkspaceManager::DeletePreset(int index) {
    if (index <= 0 || index >= static_cast<int>(m_presets.size())) return;
    // A read-only or locked file must not throw out of a menu handler; the preset leaves
    // the list either way, and a stale .ini is picked up again by the next scan.
    std::error_code ec;
    std::filesystem::remove(m_presets[index].filepath, ec);
    m_presets.erase(m_presets.begin() + index);
}

bool WorkspaceManager::SetKeybinding(int index, int vkCode, int modifiers) {
    if (index <= 0 || index >= static_cast<int>(m_presets.size())) return false;

    WorkspacePreset& preset = m_presets[index];
    preset.shortcutKey = vkCode;
    preset.shortcutModifiers = modifiers;

    WorkspacePreset dummy;
    std::string stateBase64;
    if (!ParsePresetFile(preset.filepath, dummy, stateBase64)) return false;
    return WritePresetFile(preset, stateBase64);
}

bool WorkspaceManager::ParsePresetFile(const std::string& filepath,
                                        WorkspacePreset& out,
                                        std::string& stateBase64) const
{
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::string line;
    bool inHeader = false;
    bool foundHeader = false;
    bool foundState = false;

    while (std::getline(file, line)) {
        // Strip trailing \r for Windows line endings
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line == "[WorkspacePreset]") {
            inHeader = true;
            foundHeader = true;
            continue;
        }

        // Everything in this format lives inside the one [WorkspacePreset] section; a
        // further section header (only ever seen on a stale legacy-format file) ends it.
        if (!inHeader) continue;
        if (!line.empty() && line[0] == '[') break;
        if (line.empty()) continue;

        // Split on the FIRST '=' only: substr(eq + 1) keeps everything after it, including
        // any further '=' characters, so base64 padding in a `state=` value round-trips.
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "name")                      out.name = val;
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
        else if (key == "showNoisePanel")       out.panels.noise       = (val == "1");
        else if (key == "showSpoutPanel")       out.panels.spout       = (val == "1");
        else if (key == "showAudioPanel")       out.panels.audio       = (val == "1");
        else if (key == "state") { stateBase64 = val; foundState = true; }
    }

    // A file missing `state=` is a preset from the superseded layout format (or is otherwise
    // unusable); ScanDirectory relies on this returning false to skip it rather than crash.
    if (!foundHeader || !foundState) return false;
    return true;
}

bool WorkspaceManager::WritePresetFile(const WorkspacePreset& preset,
                                        const std::string& stateBase64) const
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
    file << "showNoisePanel=" << (preset.panels.noise ? 1 : 0) << '\n';
    file << "showSpoutPanel=" << (preset.panels.spout ? 1 : 0) << '\n';
    file << "showAudioPanel=" << (preset.panels.audio ? 1 : 0) << '\n';
    file << "state=" << stateBase64 << '\n';

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
