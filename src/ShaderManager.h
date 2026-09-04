#pragma once

#include "Common.h"
#include "D3D11Renderer.h"

#include <chrono>

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
    // Reads and parses ISF metadata but does NOT compile — use before AddPreset to avoid a
    // double-compile. Static and touching no manager state, so the startup path runs it on
    // a worker thread while the main thread is inside D3D11CreateDevice.
    static bool LoadShaderMetadataFromFile(const std::string& filepath, ShaderPreset& outPreset);
    bool LoadShaderFromSource(const std::string& name, const std::string& source, ShaderPreset& outPreset);
    bool CompilePreset(ShaderPreset& preset);
    // Compile a preset already stored at the given index and update m_compiledShaders[index].
    bool RecompilePreset(int index);
    
    // Preset management
    int AddPreset(const ShaderPreset& preset);
    // Bulk equivalent of AddPreset over a whole batch, compiling across all cores. Only
    // worth using for the startup load, where the batch is the entire preset list and a
    // cold cache would otherwise serialise ~45 D3DCompile calls into several seconds.
    // Presets are appended in the order given, so indices are the caller's to predict.
    void AddPresets(std::vector<ShaderPreset> presets);
    void RemovePreset(int index);
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
    // True when at least one watched file was reloaded, so the caller can rebuild the
    // panels bound to the parameter vector that reload replaced.
    bool CheckForChanges();

    // Directory scanning
    void ScanDirectory(const std::string& directory);

    // Get default shader template
    static std::string GetShaderTemplate();

    // Flatten a preset's parameter values into the shader-visible custom[] block,
    // honouring the alignment ParseISFParams assigned. AudioBand params are skipped:
    // they alias AudioConstants (b1) and consume no custom[] slot. Static and free of
    // renderer state so both frontends (the Qt app and the headless runner) pack
    // identically — a second implementation is a second set of alignment bugs.
    static void PackParamValues(const ShaderPreset& preset, float out[kCustomFloats]);

private:
    D3D11Renderer& m_renderer;
    std::vector<ShaderPreset> m_presets;
    std::vector<ComPtr<ID3D11PixelShader>> m_compiledShaders;
    int m_activeIndex = -1;  // -1 = passthrough

    // File watching
    bool m_fileWatchingEnabled = false;
    std::unordered_map<std::string, std::filesystem::file_time_type> m_fileTimestamps;
    std::chrono::steady_clock::time_point m_lastWatchCheck{};

    // outWarning receives a human-readable note when the cbuffer budget forced parameters
    // to be dropped; callers prepend it to compileError so the library's error tooltip
    // explains the undeclared identifier that follows.
    static std::vector<ShaderParam> ParseISFParams(const std::string& source,
                                                    bool* outIsGenerative = nullptr,
                                                    bool* outIsAudio      = nullptr,
                                                    std::string* outWarning = nullptr);
    // sourceName appears in the trailing `#line 1 "<name>"` directive, so fxc error
    // line numbers match the shader file on disk instead of the inflated preamble.
    static std::string BuildDefinesPreamble(const std::vector<ShaderParam>& params,
                                            const std::string& sourceName);
};

} // namespace SP
