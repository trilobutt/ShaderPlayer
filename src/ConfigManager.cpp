#include "ConfigManager.h"
#include <algorithm>
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
    // Save keyframe timelines keyed by param name
    nlohmann::json kfObj = nlohmann::json::object();
    for (const auto& param : p.params) {
        if (!param.timeline || param.timeline->keyframes.empty()) continue;
        const auto& tl = *param.timeline;
        nlohmann::json tlJson;
        tlJson["enabled"] = tl.enabled;
        nlohmann::json keys = nlohmann::json::array();
        for (const auto& kf : tl.keyframes) {
            nlohmann::json kfJson;
            kfJson["time"] = kf.time;
            int count = 1;
            if (param.type == ShaderParamType::Point2D) count = 2;
            else if (param.type == ShaderParamType::Color) count = 4;
            nlohmann::json vals = nlohmann::json::array();
            for (int i = 0; i < count; ++i) vals.push_back(kf.values[i]);
            kfJson["values"] = vals;
            const char* modeStr = "linear";
            if (kf.mode == InterpolationMode::EaseInOut) modeStr = "easeInOut";
            else if (kf.mode == InterpolationMode::CubicBezier) modeStr = "cubicBezier";
            kfJson["mode"] = modeStr;
            kfJson["handles"] = nlohmann::json{
                {"outX", kf.handles.outX}, {"outY", kf.handles.outY},
                {"inX", kf.handles.inX},   {"inY", kf.handles.inY}
            };
            keys.push_back(kfJson);
        }
        tlJson["keys"] = keys;
        kfObj[param.name] = tlJson;
    }
    if (!kfObj.empty()) {
        j["keyframes"] = kfObj;
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
    if (j.contains("keyframes") && j["keyframes"].is_object()) {
        for (auto& [name, tlJson] : j["keyframes"].items()) {
            KeyframeTimeline tl;
            tl.enabled = tlJson.value("enabled", false);
            if (tlJson.contains("keys") && tlJson["keys"].is_array()) {
                for (const auto& kfJson : tlJson["keys"]) {
                    Keyframe kf;
                    kf.time = kfJson.value("time", 0.0f);
                    if (kfJson.contains("values") && kfJson["values"].is_array()) {
                        int i = 0;
                        for (const auto& v : kfJson["values"]) {
                            if (i < 4) kf.values[i++] = v.get<float>();
                        }
                    }
                    std::string modeStr = kfJson.value("mode", "linear");
                    if (modeStr == "easeInOut") kf.mode = InterpolationMode::EaseInOut;
                    else if (modeStr == "cubicBezier") kf.mode = InterpolationMode::CubicBezier;
                    else kf.mode = InterpolationMode::Linear;
                    if (kfJson.contains("handles")) {
                        const auto& h = kfJson["handles"];
                        kf.handles.outX = h.value("outX", 0.33f);
                        kf.handles.outY = h.value("outY", 0.0f);
                        kf.handles.inX  = h.value("inX",  0.67f);
                        kf.handles.inY  = h.value("inY",  1.0f);
                    }
                    tl.keyframes.push_back(kf);
                }
            }
            // Sort by time just in case
            std::sort(tl.keyframes.begin(), tl.keyframes.end(),
                [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
            p.savedKeyframes[name] = std::move(tl);
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
        {"layoutsDirectory", c.layoutsDirectory},
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
    if (j.contains("layoutsDirectory")) j.at("layoutsDirectory").get_to(c.layoutsDirectory);
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
