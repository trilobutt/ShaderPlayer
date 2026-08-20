#include "ShaderManager.h"
#include "ShaderCommonEmbedded.h"  // generated from src/ShaderCommon.hlsli by CMake
#include "FrameProfiler.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <sstream>
#include <thread>
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

bool ShaderManager::LoadShaderMetadataFromFile(const std::string& filepath, ShaderPreset& outPreset) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        outPreset.compileError = "Failed to open file: " + filepath;
        outPreset.isValid = false;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    outPreset.filepath = filepath;
    outPreset.source   = buffer.str();
    outPreset.name     = std::filesystem::path(filepath).stem().string();
    // Parse ISF so default param values are available for the caller to override before AddPreset.
    outPreset.params   = ParseISFParams(outPreset.source, &outPreset.isGenerative, &outPreset.isAudio);
    return true;
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

    preset.params = ParseISFParams(preset.source, &preset.isGenerative, &preset.isAudio);

    for (auto& p : preset.params) {
        auto it = saved.find(p.name);
        if (it != saved.end())
            std::copy(it->second.begin(), it->second.end(), p.values);
    }

    std::string preamble = BuildDefinesPreamble(preset.params, preset.name);
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

    m_presets[index].params = ParseISFParams(m_presets[index].source,
                                              &m_presets[index].isGenerative,
                                              &m_presets[index].isAudio);

    for (auto& p : m_presets[index].params) {
        auto it = saved.find(p.name);
        if (it != saved.end())
            std::copy(it->second.begin(), it->second.end(), p.values);
    }

    std::string preamble = BuildDefinesPreamble(m_presets[index].params, m_presets[index].name);
    ComPtr<ID3D11PixelShader> shader;
    std::string error;

    if (m_renderer.CompilePixelShader(preamble + m_presets[index].source, shader, error)) {
        m_presets[index].isValid = true;
        m_presets[index].compileError.clear();
        m_compiledShaders[index] = shader;
        // The renderer holds its own reference to the previous shader object, so it
        // would keep drawing the stale one until the preset was re-selected. Push the
        // new shader through immediately when the recompiled preset is the active one.
        if (index == m_activeIndex)
            m_renderer.SetActivePixelShader(shader.Get());
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
        // calls LoadShaderMetadataFromFile (which parses ISF without compiling) then
        // patches param.values from savedParamValues before calling AddPreset.
        // Skipping re-parse here preserves those restored user values.
        if (m_presets.back().params.empty()) {
            m_presets.back().params = ParseISFParams(m_presets.back().source,
                                                      &m_presets.back().isGenerative,
                                                      &m_presets.back().isAudio);
        }
        std::string preamble = BuildDefinesPreamble(m_presets.back().params, m_presets.back().name);
        if (m_renderer.CompilePixelShader(preamble + m_presets.back().source, shader, error)) {
            m_presets.back().isValid = true;
            m_presets.back().compileError.clear();
        } else {
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

void ShaderManager::AddPresets(std::vector<ShaderPreset> presets) {
    if (presets.empty()) return;

    // Parse anything the caller did not already parse. Same rule as AddPreset: a preset
    // arriving with params already filled has had user values patched into them, and a
    // re-parse would throw those away.
    for (auto& p : presets) {
        if (p.params.empty() && !p.source.empty())
            p.params = ParseISFParams(p.source, &p.isGenerative, &p.isAudio);
    }

    // Compile across all cores. D3D11 devices are free-threaded, and D3DCompile in
    // d3dcompiler_47 is safe to call concurrently; the per-shader cache blob is written
    // through a temp file and renamed, so two threads landing on the same hash cannot
    // read a partial file. Nothing here touches m_presets, which is why the results are
    // collected into a side vector and merged below on this thread alone.
    struct Result {
        ComPtr<ID3D11PixelShader> shader;
        std::string error;
        bool ok = false;
        bool compiled = false;   // false = nothing to compile (empty source)
    };
    std::vector<Result> results(presets.size());

    // Claimed one at a time rather than in fixed bands: shader compile times vary by an
    // order of magnitude across this set, and a static split leaves most threads idle
    // behind whichever band drew the expensive ones.
    std::atomic<size_t> next{0};
    const auto compileLoop = [this, &presets, &results, &next] {
        for (;;) {
            const size_t i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= presets.size()) return;

            ShaderPreset& p = presets[i];
            if (!p.isValid && p.source.empty()) continue;
            results[i].compiled = true;
            const std::string preamble = BuildDefinesPreamble(p.params, p.name);
            results[i].ok = m_renderer.CompilePixelShader(preamble + p.source,
                                                          results[i].shader,
                                                          results[i].error);
        }
    };

    unsigned workers = std::thread::hardware_concurrency();
    if (workers == 0) workers = 1;
    workers = (std::min)(workers, static_cast<unsigned>(presets.size()));

    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (unsigned i = 1; i < workers; ++i) threads.emplace_back(compileLoop);
    compileLoop();   // this thread works too, rather than idling in join()
    for (auto& t : threads) t.join();

    // Merge. The two vectors stay in lockstep exactly as AddPreset keeps them.
    m_presets.reserve(m_presets.size() + presets.size());
    m_compiledShaders.reserve(m_compiledShaders.size() + presets.size());
    for (size_t i = 0; i < presets.size(); ++i) {
        ShaderPreset& p = presets[i];
        if (results[i].compiled) {
            p.isValid = results[i].ok;
            if (results[i].ok) p.compileError.clear();
            else               p.compileError = results[i].error;
        }

        if (!p.filepath.empty()) {
            std::error_code ec;
            const auto stamp = std::filesystem::last_write_time(p.filepath, ec);
            if (!ec) m_fileTimestamps[p.filepath] = stamp;
        }

        m_presets.push_back(std::move(p));
        m_compiledShaders.push_back(std::move(results[i].shader));
    }
}

void ShaderManager::RemovePreset(int index) {
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return;

    // Remove file timestamp tracking
    if (!m_presets[index].filepath.empty()) {
        m_fileTimestamps.erase(m_presets[index].filepath);
    }

    m_presets.erase(m_presets.begin() + index);
    m_compiledShaders.erase(m_compiledShaders.begin() + index);

    // Adjust active index. Removing the active preset must go through SetPassthrough:
    // the renderer holds a *raw* ID3D11PixelShader* handed to it by SetActivePreset, and
    // the erase above released the only ComPtr keeping it alive. Clearing m_activeIndex
    // alone leaves the renderer drawing with a freed shader until the next activation.
    if (m_activeIndex == index) {
        SetPassthrough();
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
    
    m_presets[index].params = ParseISFParams(preset.source,
                                              &m_presets[index].isGenerative,
                                              &m_presets[index].isAudio);
    std::string preamble = BuildDefinesPreamble(m_presets[index].params, m_presets[index].name);

    if (m_renderer.CompilePixelShader(preamble + preset.source, shader, error)) {
        m_presets[index].isValid = true;
        m_presets[index].compileError.clear();
        m_compiledShaders[index] = shader;
        // Same as RecompilePreset: hot-reloading the active shader must reach the GPU
        // now, otherwise the renderer's held reference keeps the pre-edit version live.
        if (index == m_activeIndex)
            m_renderer.SetActivePixelShader(shader.Get());
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
    SP_PROFILE(kCheckForChanges);

    // Two filesystem calls per preset, and the preset list runs to 45. Polling that at
    // frame rate costs ~1.7 ms of every frame to answer a question no editor can change
    // the answer to more than twice a second.
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastWatchCheck < std::chrono::milliseconds(500)) return;
    m_lastWatchCheck = now;

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

    // Read and parse every new file first, then hand the whole batch to AddPresets, which
    // compiles them across all cores. Reading here rather than through LoadShaderFromFile
    // also drops a compile per shader: that path compiled into a local preset whose
    // bytecode AddPreset then threw away and recompiled.
    std::vector<ShaderPreset> batch;

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
                // An unreadable file is still added, carrying its compileError, exactly as
                // a file that reads but fails to compile is.
                LoadShaderMetadataFromFile(filepath, preset);
                batch.push_back(std::move(preset));
            }
        }
    }

    AddPresets(std::move(batch));
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
    float4 custom[8];
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

std::vector<ShaderParam> ShaderManager::ParseISFParams(const std::string& source,
                                                         bool* outIsGenerative,
                                                         bool* outIsAudio) {
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
    int offset = 0;  // Current float index into custom[]

    try {
        nlohmann::json j = nlohmann::json::parse(jsonText);

        const std::string shaderType = j.value("SHADER_TYPE", std::string{});
        if (outIsGenerative) *outIsGenerative = (shaderType == "generative");
        if (outIsAudio)      *outIsAudio      = (shaderType == "audio");

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
            else if (typeStr == "audio") {
                // AudioBand params alias to AudioConstants (b1) — consume no custom[] slot.
                p.type       = ShaderParamType::AudioBand;
                p.audioBand  = input.value("BAND", std::string{"rms"});
                p.cbufferOffset = -1;
                params.push_back(std::move(p));
                continue;  // Skip cbuffer alignment/offset logic below.
            } else continue;  // Unknown type; skip

            if (input.contains("MIN")  && input["MIN"].is_number())  p.min  = input["MIN"].get<float>();
            if (input.contains("MAX")  && input["MAX"].is_number())  p.max  = input["MAX"].get<float>();
            if (input.contains("STEP") && input["STEP"].is_number()) p.step = input["STEP"].get<float>();

            if (p.type == ShaderParamType::Long && input.contains("VALUES")) {
                for (const auto& v : input["VALUES"])
                    p.longValues.push_back(v.is_number_integer() ? v.get<int>() : static_cast<int>(v.get<float>()));
                // Use LABELS for display strings if present, otherwise stringify the VALUES
                if (input.contains("LABELS")) {
                    for (const auto& lbl : input["LABELS"])
                        p.longLabels.push_back(lbl.get<std::string>());
                } else {
                    for (int iv : p.longValues)
                        p.longLabels.push_back(std::to_string(iv));
                }
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

            if (offset + size > kCustomFloats) {
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

/*static*/ void ShaderManager::PackParamValues(const ShaderPreset& preset, float out[kCustomFloats]) {
    std::fill(out, out + kCustomFloats, 0.0f);
    for (const auto& p : preset.params) {
        if (p.type == ShaderParamType::AudioBand) continue;  // Lives in b1, not custom[]
        const int off = p.cbufferOffset;
        switch (p.type) {
        case ShaderParamType::Float:
        case ShaderParamType::Bool:
        case ShaderParamType::Long:
        case ShaderParamType::Event:
            if (off < kCustomFloats)       out[off] = p.values[0];
            break;
        case ShaderParamType::Point2D:
            if (off + 1 < kCustomFloats) { out[off] = p.values[0]; out[off + 1] = p.values[1]; }
            break;
        case ShaderParamType::Color:
            if (off + 3 < kCustomFloats) {
                out[off] = p.values[0]; out[off + 1] = p.values[1];
                out[off + 2] = p.values[2]; out[off + 3] = p.values[3];
            }
            break;
        }
    }
}

std::string ShaderManager::BuildDefinesPreamble(const std::vector<ShaderParam>& params,
                                                const std::string& sourceName) {
    static constexpr char comp[] = "xyzw";
    std::string preamble;

    // Shared helper library first — the shader body may use it, and so may nothing
    // in the preamble itself, so its position only has to precede the source.
    preamble += kShaderCommonHLSL;
    preamble += "\n";

    // If any AudioBand param is present, prepend the AudioConstants cbuffer declaration
    // and the spectrum texture so the shader doesn't have to declare them manually.
    bool hasAudio = false;
    for (const auto& p : params) {
        if (p.type == ShaderParamType::AudioBand) { hasAudio = true; break; }
    }
    if (hasAudio) {
        preamble +=
            "cbuffer AudioConstants : register(b1) {\n"
            "    float audioRms; float audioBass; float audioMid; float audioHigh;\n"
            "    float audioBeat; float audioSpectralCentroid; float audioPad0; float audioPad1;\n"
            "};\n"
            "Texture2D spectrumTexture : register(t3);\n";
    }

    for (const auto& p : params) {
        if (p.type == ShaderParamType::AudioBand) {
            // Map band name to the corresponding AudioConstants field.
            static const std::unordered_map<std::string, std::string> bandMap = {
                {"rms",      "audioRms"},
                {"bass",     "audioBass"},
                {"mid",      "audioMid"},
                {"high",     "audioHigh"},
                {"beat",     "audioBeat"},
                {"centroid", "audioSpectralCentroid"},
            };
            auto it = bandMap.find(p.audioBand);
            if (it != bandMap.end())
                preamble += "#define " + p.name + " " + it->second + "\n";
            continue;
        }

        if (p.cbufferOffset < 0 || p.cbufferOffset >= kCustomFloats) continue;
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
        case ShaderParamType::AudioBand:
            break;  // Already handled above.
        }
    }

    // Reset the line counter so fxc's error line numbers refer to the file on
    // disk rather than to preamble-inflated positions. Quotes and backslashes
    // would terminate or escape the HLSL string literal, so drop them.
    std::string name = sourceName.empty() ? std::string("shader") : sourceName;
    name.erase(std::remove_if(name.begin(), name.end(),
                              [](char ch) { return ch == '"' || ch == '\\'; }),
               name.end());
    preamble += "#line 1 \"" + name + "\"\n";

    return preamble;
}

} // namespace SP
