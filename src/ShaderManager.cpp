#include "ShaderManager.h"
#include <fstream>
#include <sstream>

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
    ComPtr<ID3D11PixelShader> shader;
    std::string error;

    if (m_renderer.CompilePixelShader(preset.source, shader, error)) {
        preset.isValid = true;
        preset.compileError.clear();

        // Find or add to compiled shaders
        int presetIndex = -1;
        for (int i = 0; i < static_cast<int>(m_presets.size()); ++i) {
            if (&m_presets[i] == &preset) {
                presetIndex = i;
                break;
            }
        }

        if (presetIndex >= 0 && presetIndex < static_cast<int>(m_compiledShaders.size())) {
            m_compiledShaders[presetIndex] = shader;
        }

        return true;
    } else {
        preset.isValid = false;
        preset.compileError = error;
        return false;
    }
}

bool ShaderManager::RecompilePreset(int index) {
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return false;

    ComPtr<ID3D11PixelShader> shader;
    std::string error;

    if (m_renderer.CompilePixelShader(m_presets[index].source, shader, error)) {
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
        m_renderer.CompilePixelShader(m_presets.back().source, shader, error);
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
    
    if (m_renderer.CompilePixelShader(preset.source, shader, error)) {
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
                // Preserve keybinding
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

} // namespace SP
