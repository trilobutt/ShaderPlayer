#include "ShaderManager.h"
#include <array>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace SP {

ShaderManager::ShaderManager(D3D11Renderer& renderer)
    : m_renderer(renderer)
{
}

ShaderManager::~ShaderManager() = default;

bool ShaderManager::LoadShaderFromFile(const std::string& filepath, ShaderPreset& outPreset) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        outPreset.compileError = "Failed to open file: " + filepath;
        outPreset.isValid = false;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    outPreset.filepath = filepath;
    outPreset.source = buffer.str();
    
    // Extract name from filename
    std::filesystem::path path(filepath);
    outPreset.name = path.stem().string();

    return CompilePreset(outPreset);
}

bool ShaderManager::LoadShaderFromSource(const std::string& name, const std::string& source, ShaderPreset& outPreset) {
    outPreset.name = name;
    outPreset.source = source;
    outPreset.filepath.clear();
    
    return CompilePreset(outPreset);
}

bool ShaderManager::CompilePreset(ShaderPreset& preset) {
    // Preserve existing param values by name before re-parse
    std::unordered_map<std::string, std::array<float, 4>> saved;
    for (const auto& p : preset.params)
        saved[p.name] = {p.values[0], p.values[1], p.values[2], p.values[3]};

    preset.params = ParseISFParams(preset.source);

    for (auto& p : preset.params) {
        auto it = saved.find(p.name);
        if (it != saved.end())
            std::copy(it->second.begin(), it->second.end(), p.values);
    }

    std::string preamble = BuildDefinesPreamble(preset.params);
    ComPtr<ID3D11PixelShader> shader;
    std::string error;

    if (m_renderer.CompilePixelShader(preamble + preset.source, shader, error)) {
        preset.isValid = true;
        preset.compileError.clear();

        int presetIndex = -1;
        for (int i = 0; i < static_cast<int>(m_presets.size()); ++i) {
            if (&m_presets[i] == &preset) { presetIndex = i; break; }
        }
        if (presetIndex >= 0 && presetIndex < static_cast<int>(m_compiledShaders.size()))
            m_compiledShaders[presetIndex] = shader;

        return true;
    } else {
        preset.isValid = false;
        preset.compileError = error;
        return false;
    }
}

bool ShaderManager::RecompilePreset(int index) {
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return false;

    std::unordered_map<std::string, std::array<float, 4>> saved;
    for (const auto& p : m_presets[index].params)
        saved[p.name] = {p.values[0], p.values[1], p.values[2], p.values[3]};

    m_presets[index].params = ParseISFParams(m_presets[index].source);

    for (auto& p : m_presets[index].params) {
        auto it = saved.find(p.name);
        if (it != saved.end())
            std::copy(it->second.begin(), it->second.end(), p.values);
    }

    std::string preamble = BuildDefinesPreamble(m_presets[index].params);
    ComPtr<ID3D11PixelShader> shader;
    std::string error;

    if (m_renderer.CompilePixelShader(preamble + m_presets[index].source, shader, error)) {
        m_presets[index].isValid = true;
        m_presets[index].compileError.clear();
        m_compiledShaders[index] = shader;
        return true;
    } else {
        m_presets[index].isValid = false;
        m_presets[index].compileError = error;
        return false;
    }
}

int ShaderManager::AddPreset(const ShaderPreset& preset) {
    m_presets.push_back(preset);
    
    // Compile the shader
    ComPtr<ID3D11PixelShader> shader;
    std::string error;
    
    if (m_presets.back().isValid || !m_presets.back().source.empty()) {
        // Only parse if params not already set. During startup, Application::Initialize
        // calls LoadShaderFromFile (which runs CompilePreset, populating params with
        // ISF defaults), then patches param.values from savedParamValues, then calls
        // AddPreset. Skipping re-parse here preserves those restored user values.
        if (m_presets.back().params.empty()) {
            m_presets.back().params = ParseISFParams(m_presets.back().source);
        }
        std::string preamble = BuildDefinesPreamble(m_presets.back().params);
        m_renderer.CompilePixelShader(preamble + m_presets.back().source, shader, error);
        if (!error.empty()) {
            m_presets.back().isValid = false;
            m_presets.back().compileError = error;
        }
    }
    
    m_compiledShaders.push_back(shader);

    // Track file timestamp for hot reload
    if (!preset.filepath.empty() && std::filesystem::exists(preset.filepath)) {
        m_fileTimestamps[preset.filepath] = std::filesystem::last_write_time(preset.filepath);
    }

    return static_cast<int>(m_presets.size()) - 1;
}

void ShaderManager::RemovePreset(int index) {
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return;

    // Remove file timestamp tracking
    if (!m_presets[index].filepath.empty()) {
        m_fileTimestamps.erase(m_presets[index].filepath);
    }

    m_presets.erase(m_presets.begin() + index);
    m_compiledShaders.erase(m_compiledShaders.begin() + index);

    // Adjust active index
    if (m_activeIndex == index) {
        m_activeIndex = -1;
    } else if (m_activeIndex > index) {
        --m_activeIndex;
    }
}

void ShaderManager::UpdatePreset(int index, const ShaderPreset& preset) {
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return;

    std::string oldPath = m_presets[index].filepath;
    m_presets[index] = preset;

    // Recompile
    ComPtr<ID3D11PixelShader> shader;
    std::string error;
    
    m_presets[index].params = ParseISFParams(preset.source);
    std::string preamble = BuildDefinesPreamble(m_presets[index].params);

    if (m_renderer.CompilePixelShader(preamble + preset.source, shader, error)) {
        m_presets[index].isValid = true;
        m_presets[index].compileError.clear();
        m_compiledShaders[index] = shader;
    } else {
        m_presets[index].isValid = false;
        m_presets[index].compileError = error;
    }

    // Update file tracking
    if (oldPath != preset.filepath) {
        m_fileTimestamps.erase(oldPath);
    }
    if (!preset.filepath.empty() && std::filesystem::exists(preset.filepath)) {
        m_fileTimestamps[preset.filepath] = std::filesystem::last_write_time(preset.filepath);
    }
}

ShaderPreset* ShaderManager::GetPreset(int index) {
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return nullptr;
    return &m_presets[index];
}

void ShaderManager::SetActivePreset(int index) {
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        m_activeIndex = -1;
        m_renderer.SetActivePixelShader(nullptr);
        return;
    }

    m_activeIndex = index;
    m_renderer.SetActivePixelShader(m_compiledShaders[index].Get());
}

ShaderPreset* ShaderManager::GetActivePreset() {
    if (m_activeIndex < 0 || m_activeIndex >= static_cast<int>(m_presets.size())) {
        return nullptr;
    }
    return &m_presets[m_activeIndex];
}

ID3D11PixelShader* ShaderManager::GetActiveShader() {
    if (m_activeIndex < 0 || m_activeIndex >= static_cast<int>(m_compiledShaders.size())) {
        return m_renderer.GetPassthroughShader();
    }
    return m_compiledShaders[m_activeIndex].Get();
}

void ShaderManager::SetPassthrough() {
    m_activeIndex = -1;
    m_renderer.SetActivePixelShader(nullptr);
}

void ShaderManager::EnableFileWatching(bool enable) {
    m_fileWatchingEnabled = enable;
}

void ShaderManager::CheckForChanges() {
    if (!m_fileWatchingEnabled) return;

    for (int i = 0; i < static_cast<int>(m_presets.size()); ++i) {
        const std::string& filepath = m_presets[i].filepath;
        if (filepath.empty()) continue;

        if (!std::filesystem::exists(filepath)) continue;

        auto currentTime = std::filesystem::last_write_time(filepath);
        auto it = m_fileTimestamps.find(filepath);
        
        if (it != m_fileTimestamps.end() && currentTime != it->second) {
            // File changed, reload
            ShaderPreset updated;
            if (LoadShaderFromFile(filepath, updated)) {
                // Preserve keybinding; note: param values reset to ISF defaults
                // on hot-reload (UpdatePreset is a wholesale replace by design).
                updated.shortcutKey = m_presets[i].shortcutKey;
                updated.shortcutModifiers = m_presets[i].shortcutModifiers;
                UpdatePreset(i, updated);
            }
            m_fileTimestamps[filepath] = currentTime;
        }
    }
}

void ShaderManager::ScanDirectory(const std::string& directory) {
    if (!std::filesystem::exists(directory)) return;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        // Convert to lowercase
        for (char& c : ext) c = static_cast<char>(std::tolower(c));

        if (ext == ".hlsl" || ext == ".fx" || ext == ".ps") {
            // Check if already loaded
            std::string filepath = entry.path().string();
            bool alreadyLoaded = false;
            for (const auto& preset : m_presets) {
                if (preset.filepath == filepath) {
                    alreadyLoaded = true;
                    break;
                }
            }

            if (!alreadyLoaded) {
                ShaderPreset preset;
                if (LoadShaderFromFile(filepath, preset)) {
                    AddPreset(preset);
                }
            }
        }
    }
}

std::string ShaderManager::GetShaderTemplate() {
    return R"(// Shader Effect Template
// Available inputs:
//   videoTexture - The video frame as a 2D texture
//   videoSampler - Linear sampler for the video
//   time - Playback time in seconds
//   resolution - Output resolution (width, height)
//   videoResolution - Video resolution (width, height)
//   custom[0-3] - Custom float4 parameters

Texture2D videoTexture : register(t0);
SamplerState videoSampler : register(s0);

cbuffer Constants : register(b0) {
    float time;
    float padding1;
    float2 resolution;
    float2 videoResolution;
    float2 padding2;
    float4 custom[4];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv;
    
    // Sample video
    float4 color = videoTexture.Sample(videoSampler, uv);
    
    // === Add your effect here ===
    
    // Example: Simple vignette
    float2 center = uv - 0.5;
    float vignette = 1.0 - dot(center, center) * 0.5;
    color.rgb *= vignette;
    
    return color;
}
)";
}

std::vector<ShaderParam> ShaderManager::ParseISFParams(const std::string& source) {
    // Find the ISF block: /*{ ... }*/
    const std::string openTag  = "/*{";
    const std::string closeTag = "}*/";

    auto startPos = source.find(openTag);
    if (startPos == std::string::npos) return {};

    auto endPos = source.find(closeTag, startPos);
    if (endPos == std::string::npos) return {};

    // Extract just the JSON object (include the braces)
    std::string jsonText = "{" + source.substr(startPos + openTag.size(),
                                                endPos - startPos - openTag.size()) + "}";

    std::vector<ShaderParam> params;
    int offset = 0;  // Current float index into custom[16]

    try {
        nlohmann::json j = nlohmann::json::parse(jsonText);

        if (!j.contains("INPUTS") || !j["INPUTS"].is_array()) return {};

        for (const auto& input : j["INPUTS"]) {
            if (!input.contains("NAME") || !input.contains("TYPE")) continue;

            ShaderParam p;
            p.name  = input["NAME"].get<std::string>();
            p.label = input.value("LABEL", p.name);

            std::string typeStr = input["TYPE"].get<std::string>();
            if      (typeStr == "float")   p.type = ShaderParamType::Float;
            else if (typeStr == "bool")    p.type = ShaderParamType::Bool;
            else if (typeStr == "long")    p.type = ShaderParamType::Long;
            else if (typeStr == "color")   p.type = ShaderParamType::Color;
            else if (typeStr == "point2d") p.type = ShaderParamType::Point2D;
            else if (typeStr == "event")   p.type = ShaderParamType::Event;
            else continue;  // Unknown type; skip

            p.min  = input.value("MIN",  0.0f);
            p.max  = input.value("MAX",  1.0f);
            p.step = input.value("STEP", 0.01f);

            if (p.type == ShaderParamType::Long && input.contains("VALUES")) {
                for (const auto& v : input["VALUES"])
                    p.longLabels.push_back(v.get<std::string>());
            }

            // Parse DEFAULT
            if (input.contains("DEFAULT")) {
                const auto& def = input["DEFAULT"];
                if (def.is_array()) {
                    int n = std::min((int)def.size(), 4);
                    for (int i = 0; i < n; ++i)
                        p.defaultValues[i] = def[i].get<float>();
                } else if (def.is_boolean()) {
                    p.defaultValues[0] = def.get<bool>() ? 1.0f : 0.0f;
                } else if (def.is_number()) {
                    p.defaultValues[0] = def.get<float>();
                }
            }
            std::copy(p.defaultValues, p.defaultValues + 4, p.values);

            // Alignment: point2d→even, color→multiple of 4
            if (p.type == ShaderParamType::Point2D) {
                if (offset % 2 != 0) ++offset;
            } else if (p.type == ShaderParamType::Color) {
                while (offset % 4 != 0) ++offset;
            }

            // Size consumed
            int size = 1;
            if (p.type == ShaderParamType::Point2D) size = 2;
            else if (p.type == ShaderParamType::Color) size = 4;

            if (offset + size > 16) {
                // Budget exhausted; remaining INPUTS are silently dropped.
                // D3DCompile will report 'undeclared identifier' for any shader code
                // that references a dropped param name.
                break;
            }

            p.cbufferOffset = offset;
            offset += size;
            params.push_back(std::move(p));
        }
    } catch (...) {
        return {};
    }

    return params;
}

std::string ShaderManager::BuildDefinesPreamble(const std::vector<ShaderParam>& params) {
    static constexpr char comp[] = "xyzw";
    std::string preamble;

    for (const auto& p : params) {
        if (p.cbufferOffset >= 16) continue;
        int idx  = p.cbufferOffset / 4;
        int c    = p.cbufferOffset % 4;
        std::string slot = "custom[" + std::to_string(idx) + "].";

        switch (p.type) {
        case ShaderParamType::Float:
        case ShaderParamType::Event:
            preamble += "#define " + p.name + " " + slot + comp[c] + "\n";
            break;
        case ShaderParamType::Bool:
            preamble += "#define " + p.name + " (" + slot + comp[c] + " > 0.5)\n";
            break;
        case ShaderParamType::Long:
            preamble += "#define " + p.name + " int(" + slot + comp[c] + ")\n";
            break;
        case ShaderParamType::Point2D:
            // point2d is even-aligned, so c is always 0 or 2 — both comp[c+1] are in-range
            preamble += "#define " + p.name + " float2(" + slot + comp[c] +
                        ", " + slot + comp[c + 1] + ")\n";
            break;
        case ShaderParamType::Color:
            // color is 4-aligned, so c==0 always
            preamble += "#define " + p.name + " custom[" + std::to_string(idx) + "]\n";
            break;
        }
    }

    return preamble;
}

} // namespace SP
