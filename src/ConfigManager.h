#pragma once

#include "Common.h"
#include <nlohmann/json.hpp>

namespace SP {

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager() = default;

    // Load/Save
    bool Load(const std::string& filepath);
    bool Save(const std::string& filepath) const;
    
    // Access
    AppConfig& GetConfig() { return m_config; }
    const AppConfig& GetConfig() const { return m_config; }

    // Default config path
    static std::string GetDefaultConfigPath();

private:
    AppConfig m_config;
};

// JSON serialization
void to_json(nlohmann::json& j, const ShaderPreset& p);
void from_json(const nlohmann::json& j, ShaderPreset& p);
void to_json(nlohmann::json& j, const RecordingSettings& r);
void from_json(const nlohmann::json& j, RecordingSettings& r);
void to_json(nlohmann::json& j, const AppConfig& c);
void from_json(const nlohmann::json& j, AppConfig& c);

} // namespace SP
