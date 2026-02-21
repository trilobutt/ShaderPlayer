#include "ConfigManager.h"
#include <fstream>

namespace SP {

// JSON serialization implementations
void to_json(nlohmann::json& j, const ShaderPreset& p) {
    j = nlohmann::json{
        {"name",              p.name},
        {"filepath",          p.filepath},
        {"shortcutKey",       p.shortcutKey},
        {"shortcutModifiers", p.shortcutModifiers}
    };
    // Save current param values keyed by name
    if (!p.params.empty()) {
        nlohmann::json paramVals = nlohmann::json::object();
        for (const auto& param : p.params) {
            nlohmann::json vals = nlohmann::json::array();
            int count = 1;
            if (param.type == ShaderParamType::Point2D) count = 2;
            else if (param.type == ShaderParamType::Color) count = 4;
            for (int i = 0; i < count; ++i) vals.push_back(param.values[i]);
            paramVals[param.name] = vals;
        }
        j["paramValues"] = paramVals;
    }
}

void from_json(const nlohmann::json& j, ShaderPreset& p) {
    if (j.contains("name"))              j.at("name").get_to(p.name);
    if (j.contains("filepath"))          j.at("filepath").get_to(p.filepath);
    if (j.contains("shortcutKey"))       j.at("shortcutKey").get_to(p.shortcutKey);
    if (j.contains("shortcutModifiers")) j.at("shortcutModifiers").get_to(p.shortcutModifiers);
    if (j.contains("paramValues") && j["paramValues"].is_object()) {
        for (auto& [name, vals] : j["paramValues"].items()) {
            std::vector<float> v;
            if (vals.is_array()) {
                for (const auto& f : vals) v.push_back(f.get<float>());
            }
            p.savedParamValues[name] = std::move(v);
        }
    }
}

void to_json(nlohmann::json& j, const RecordingSettings& r) {
    j = nlohmann::json{
        {"outputPath", r.outputPath},
        {"width", r.width},
        {"height", r.height},
        {"bitrate", r.bitrate},
        {"fps", r.fps},
        {"codec", r.codec},
        {"preset", r.preset},
        {"proresProfile", r.proresProfile}
    };
}

void from_json(const nlohmann::json& j, RecordingSettings& r) {
    if (j.contains("outputPath")) j.at("outputPath").get_to(r.outputPath);
    if (j.contains("width")) j.at("width").get_to(r.width);
    if (j.contains("height")) j.at("height").get_to(r.height);
    if (j.contains("bitrate")) j.at("bitrate").get_to(r.bitrate);
    if (j.contains("fps")) j.at("fps").get_to(r.fps);
    if (j.contains("codec")) j.at("codec").get_to(r.codec);
    if (j.contains("preset")) j.at("preset").get_to(r.preset);
    if (j.contains("proresProfile")) j.at("proresProfile").get_to(r.proresProfile);
}

void to_json(nlohmann::json& j, const AppConfig& c) {
    j = nlohmann::json{
        {"shaderPresets", c.shaderPresets},
        {"recordingDefaults", c.recordingDefaults},
        {"autoCompileOnSave", c.autoCompileOnSave},
        {"autoCompileDelayMs", c.autoCompileDelayMs},
        {"lastOpenedVideo", c.lastOpenedVideo},
        {"shaderDirectory", c.shaderDirectory},
        {"editorPanelWidth", c.editorPanelWidth},
        {"libraryPanelHeight", c.libraryPanelHeight},
        {"showEditor", c.showEditor},
        {"showLibrary", c.showLibrary},
        {"showTransport", c.showTransport}
    };
}

void from_json(const nlohmann::json& j, AppConfig& c) {
    if (j.contains("shaderPresets")) j.at("shaderPresets").get_to(c.shaderPresets);
    if (j.contains("recordingDefaults")) j.at("recordingDefaults").get_to(c.recordingDefaults);
    if (j.contains("autoCompileOnSave")) j.at("autoCompileOnSave").get_to(c.autoCompileOnSave);
    if (j.contains("autoCompileDelayMs")) j.at("autoCompileDelayMs").get_to(c.autoCompileDelayMs);
    if (j.contains("lastOpenedVideo")) j.at("lastOpenedVideo").get_to(c.lastOpenedVideo);
    if (j.contains("shaderDirectory")) j.at("shaderDirectory").get_to(c.shaderDirectory);
    if (j.contains("editorPanelWidth")) j.at("editorPanelWidth").get_to(c.editorPanelWidth);
    if (j.contains("libraryPanelHeight")) j.at("libraryPanelHeight").get_to(c.libraryPanelHeight);
    if (j.contains("showEditor")) j.at("showEditor").get_to(c.showEditor);
    if (j.contains("showLibrary")) j.at("showLibrary").get_to(c.showLibrary);
    if (j.contains("showTransport")) j.at("showTransport").get_to(c.showTransport);
}

ConfigManager::ConfigManager() = default;

bool ConfigManager::Load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;
        m_config = j.get<AppConfig>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ConfigManager::Save(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    try {
        nlohmann::json j = m_config;
        file << j.dump(2);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string ConfigManager::GetDefaultConfigPath() {
    // Get executable directory
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    
    std::string exePath(path);
    size_t lastSlash = exePath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        exePath = exePath.substr(0, lastSlash + 1);
    }
    
    return exePath + "config.json";
}

} // namespace SP
