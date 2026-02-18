#pragma once

#include "Common.h"
#include "D3D11Renderer.h"

namespace SP {

class ShaderManager {
public:
    ShaderManager(D3D11Renderer& renderer);
    ~ShaderManager();

    // Non-copyable
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    // Shader operations
    bool LoadShaderFromFile(const std::string& filepath, ShaderPreset& outPreset);
    bool LoadShaderFromSource(const std::string& name, const std::string& source, ShaderPreset& outPreset);
    bool CompilePreset(ShaderPreset& preset);
    // Compile a preset already stored at the given index and update m_compiledShaders[index].
    bool RecompilePreset(int index);
    
    // Preset management
    int AddPreset(const ShaderPreset& preset);
    void RemovePreset(int index);
    void UpdatePreset(int index, const ShaderPreset& preset);
    ShaderPreset* GetPreset(int index);
    const std::vector<ShaderPreset>& GetPresets() const { return m_presets; }
    int GetPresetCount() const { return static_cast<int>(m_presets.size()); }

    // Active shader
    void SetActivePreset(int index);
    int GetActivePresetIndex() const { return m_activeIndex; }
    ShaderPreset* GetActivePreset();
    ID3D11PixelShader* GetActiveShader();
    
    // Set passthrough (no effect)
    void SetPassthrough();
    bool IsPassthrough() const { return m_activeIndex < 0; }

    // Hot reload
    void EnableFileWatching(bool enable);
    void CheckForChanges();

    // Directory scanning
    void ScanDirectory(const std::string& directory);

    // Get default shader template
    static std::string GetShaderTemplate();

private:
    D3D11Renderer& m_renderer;
    std::vector<ShaderPreset> m_presets;
    std::vector<ComPtr<ID3D11PixelShader>> m_compiledShaders;
    int m_activeIndex = -1;  // -1 = passthrough

    // File watching
    bool m_fileWatchingEnabled = false;
    std::unordered_map<std::string, std::filesystem::file_time_type> m_fileTimestamps;
};

} // namespace SP
