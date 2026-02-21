# Shader Parameters Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add ISF-style JSON parameter annotations to HLSL shaders, with auto-generated `#define` aliases and ImGui controls embedded in the Shader Editor panel.

**Architecture:** Parse `/*{ "INPUTS": [...] }*/` block comments from shader source; generate `#define` preamble mapping param names to `custom[N].x` slots; store param values in `ShaderPreset::params`; render per-type ImGui widgets that call `Application::OnParamChanged()` on change; persist only values (not metadata) in `config.json`.

**Tech Stack:** C++20, D3D11 HLSL (`custom[16]` cbuffer slot already wired), ImGui, nlohmann/json (already present).

**Build command:** `cmake --build build --config Release` — no automated tests; verify with build success + manual smoke test described per task.

---

## Task 1: Data model — `ShaderParam` struct and `ShaderPreset` additions

**Files:**
- Modify: `src/Common.h`

**Step 1: Add `ShaderParamType` enum and `ShaderParam` struct**

In `src/Common.h`, after the `#include` block and before `struct VideoFrame`, add:

```cpp
enum class ShaderParamType { Float, Bool, Long, Color, Point2D, Event };

struct ShaderParam {
    std::string name;               // HLSL identifier; used for #define alias
    std::string label;              // ImGui display label (defaults to name)
    ShaderParamType type = ShaderParamType::Float;
    float values[4]        = {};    // Current values: float/bool/long/event→[0],
                                    //   color→RGBA, point2d→XY
    float defaultValues[4] = {};    // Restored on "Reset to defaults"
    float min  = 0.0f;
    float max  = 1.0f;
    float step = 0.01f;
    std::vector<std::string> longLabels; // Dropdown labels for type=Long
    int cbufferOffset = 0;          // Float index into custom[16]; set at parse time
};
```

**Step 2: Add fields to `ShaderPreset`**

In `struct ShaderPreset`, add after `compileError`:

```cpp
std::vector<ShaderParam> params;
// Persistence bridge: saved values keyed by param name, restored after re-parse.
// Format: { "PixelSize": [8.0], "Tint": [1.0, 0.8, 0.6, 1.0] }
std::unordered_map<std::string, std::vector<float>> savedParamValues;
```

**Step 3: Build**

```bash
cmake --build build --config Release
```

Expected: build succeeds (no new .cpp changes yet, just struct additions).

**Step 4: Commit**

```bash
git add src/Common.h
git commit -m "feat: add ShaderParam struct and params fields to ShaderPreset"
```

---

## Task 2: ISF parser and `#define` preamble builder in ShaderManager

**Files:**
- Modify: `src/ShaderManager.h`
- Modify: `src/ShaderManager.cpp`

**Step 1: Declare the two new static methods in `ShaderManager.h`**

In the `private:` section (after the file-watching fields), add:

```cpp
static std::vector<ShaderParam> ParseISFParams(const std::string& source);
static std::string BuildDefinesPreamble(const std::vector<ShaderParam>& params);
```

Also add `#include <nlohmann/json.hpp>` at the top of `ShaderManager.h` (before the SP namespace, after the existing includes).

**Step 2: Implement `ParseISFParams` in `ShaderManager.cpp`**

Add this after the `#include` block, before `namespace SP {` opens, to pull in json:

```cpp
#include <nlohmann/json.hpp>
```

Then inside `namespace SP`, add the implementation:

```cpp
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

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonText);
    } catch (...) {
        return {};
    }

    if (!j.contains("INPUTS") || !j["INPUTS"].is_array()) return {};

    std::vector<ShaderParam> params;
    int offset = 0;  // Current float index into custom[16]

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
            // No room; stop processing further params
            break;
        }

        p.cbufferOffset = offset;
        offset += size;
        params.push_back(std::move(p));
    }

    return params;
}
```

**Step 3: Implement `BuildDefinesPreamble` in `ShaderManager.cpp`**

Add immediately after `ParseISFParams`:

```cpp
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
```

**Step 4: Build**

```bash
cmake --build build --config Release
```

Expected: build succeeds (functions declared and implemented but not yet called).

**Step 5: Commit**

```bash
git add src/ShaderManager.h src/ShaderManager.cpp
git commit -m "feat: add ISF param parser and #define preamble builder to ShaderManager"
```

---

## Task 3: Wire ISF parsing into all compile paths

**Files:**
- Modify: `src/ShaderManager.cpp`

Every path that calls `D3DCompile` (via `m_renderer.CompilePixelShader`) must: (a) parse ISF params, (b) preserve existing values by name, (c) prepend `#define` preamble.

**Step 1: Update `CompilePreset`**

Replace the existing `CompilePreset` implementation with:

```cpp
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
```

**Step 2: Update `RecompilePreset`**

Replace the existing `RecompilePreset` implementation with:

```cpp
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
```

**Step 3: Update `AddPreset`**

In `AddPreset`, replace the `CompilePixelShader` call section with:

```cpp
if (m_presets.back().isValid || !m_presets.back().source.empty()) {
    // Only parse if params not already set (e.g. by a prior CompilePreset call)
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
```

**Step 4: Update `UpdatePreset`**

In `UpdatePreset`, replace the `CompilePixelShader` call section with:

```cpp
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
```

**Step 5: Build**

```bash
cmake --build build --config Release
```

Expected: build succeeds. Shaders without ISF blocks compile identically to before (empty preamble = empty string prepended).

**Step 6: Commit**

```bash
git add src/ShaderManager.cpp
git commit -m "feat: wire ISF parse and #define preamble into all shader compile paths"
```

---

## Task 4: `OnParamChanged` and event reset in Application

**Files:**
- Modify: `src/Application.h`
- Modify: `src/Application.cpp`

**Step 1: Add declarations to `Application.h`**

In the `public:` section, after `IsBindingConflict`, add:

```cpp
// Called by UIManager after any shader parameter widget changes value.
void OnParamChanged();
```

In the `private:` section, after `HandleKeyboardShortcuts`, add:

```cpp
static void PackParamValues(const ShaderPreset& preset, float out[16]);
```

In the private member variables, after `m_playbackTime`, add:

```cpp
bool m_eventResetPending = false;
```

**Step 2: Implement `PackParamValues` in `Application.cpp`**

Add before `Application::RenderFrame`:

```cpp
/*static*/ void Application::PackParamValues(const ShaderPreset& preset, float out[16]) {
    std::fill(out, out + 16, 0.0f);
    for (const auto& p : preset.params) {
        if (p.cbufferOffset >= 16) continue;
        switch (p.type) {
        case ShaderParamType::Float:
        case ShaderParamType::Bool:
        case ShaderParamType::Long:
        case ShaderParamType::Event:
            out[p.cbufferOffset] = p.values[0];
            break;
        case ShaderParamType::Point2D:
            out[p.cbufferOffset]     = p.values[0];
            out[p.cbufferOffset + 1] = p.values[1];
            break;
        case ShaderParamType::Color:
            out[p.cbufferOffset]     = p.values[0];
            out[p.cbufferOffset + 1] = p.values[1];
            out[p.cbufferOffset + 2] = p.values[2];
            out[p.cbufferOffset + 3] = p.values[3];
            break;
        }
    }
}
```

**Step 3: Implement `OnParamChanged` in `Application.cpp`**

Add immediately after `PackParamValues`:

```cpp
void Application::OnParamChanged() {
    ShaderPreset* preset = m_shaderManager->GetActivePreset();
    if (!preset) return;

    float packed[16] = {};
    PackParamValues(*preset, packed);
    m_renderer.SetCustomUniforms(packed, 16);

    for (const auto& p : preset->params) {
        if (p.type == ShaderParamType::Event && p.values[0] > 0.5f) {
            m_eventResetPending = true;
            break;
        }
    }
}
```

**Step 4: Add event reset in `RenderFrame`**

In `RenderFrame`, after `m_uiManager->EndFrame()` and before the recording block, add:

```cpp
// Reset event params after they have been visible for one frame
if (m_eventResetPending) {
    m_eventResetPending = false;
    ShaderPreset* preset = m_shaderManager->GetActivePreset();
    if (preset) {
        for (auto& p : preset->params) {
            if (p.type == ShaderParamType::Event)
                p.values[0] = 0.0f;
        }
        float packed[16] = {};
        PackParamValues(*preset, packed);
        m_renderer.SetCustomUniforms(packed, 16);
    }
}
```

**Step 5: Upload params when activating a preset**

`SetActivePreset` in ShaderManager doesn't know about param values. Application must upload them whenever a preset becomes active. Find all call-sites in `Application.cpp` where `m_shaderManager->SetActivePreset(...)` is called and add `OnParamChanged()` immediately after each one. (Search for `SetActivePreset` — there are typically 2–3 call-sites: keybinding handler, library click, compile path.)

Also call `OnParamChanged()` after the initial preset-load loop in `Initialize`, after all presets are added and the first preset (if any) is active.

**Step 6: Build**

```bash
cmake --build build --config Release
```

Expected: build succeeds.

**Step 7: Commit**

```bash
git add src/Application.h src/Application.cpp
git commit -m "feat: add OnParamChanged, PackParamValues, and event reset to Application"
```

---

## Task 5: Persistence — save and restore param values

**Files:**
- Modify: `src/ConfigManager.cpp`
- Modify: `src/Application.cpp`

**Step 1: Serialise `savedParamValues` in `to_json` for `ShaderPreset`**

In `ConfigManager.cpp`, update `to_json(nlohmann::json& j, const ShaderPreset& p)`:

```cpp
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
```

**Step 2: Deserialise `savedParamValues` in `from_json` for `ShaderPreset`**

Update `from_json(const nlohmann::json& j, ShaderPreset& p)`:

```cpp
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
```

**Step 3: Restore param values after preset load in `Application::Initialize`**

In `Application.cpp`, update the preset-loading loop (around line 42–51). After `LoadShaderFromFile` succeeds and before `AddPreset`, restore saved values from `configPreset.savedParamValues`:

```cpp
for (auto& configPreset : m_configManager.GetConfig().shaderPresets) {
    if (!configPreset.filepath.empty()) {
        ShaderPreset loadedPreset;
        if (m_shaderManager->LoadShaderFromFile(configPreset.filepath, loadedPreset)) {
            loadedPreset.shortcutKey       = configPreset.shortcutKey;
            loadedPreset.shortcutModifiers = configPreset.shortcutModifiers;
            // Restore saved param values by name
            for (auto& param : loadedPreset.params) {
                auto it = configPreset.savedParamValues.find(param.name);
                if (it != configPreset.savedParamValues.end()) {
                    const auto& vals = it->second;
                    for (int i = 0; i < 4 && i < static_cast<int>(vals.size()); ++i)
                        param.values[i] = vals[i];
                }
            }
            m_shaderManager->AddPreset(loadedPreset);
        }
    }
}
```

**Step 4: Build**

```bash
cmake --build build --config Release
```

Expected: build succeeds.

**Step 5: Commit**

```bash
git add src/ConfigManager.cpp src/Application.cpp
git commit -m "feat: persist and restore shader param values in config.json"
```

---

## Task 6: UI — Parameters section in Shader Editor panel

**Files:**
- Modify: `src/UIManager.h`
- Modify: `src/UIManager.cpp`

**Step 1: Declare `DrawShaderParameters` in `UIManager.h`**

In the `private:` methods section, add after `DrawKeybindingsPanel`:

```cpp
void DrawShaderParameters();
```

**Step 2: Call `DrawShaderParameters` from `DrawShaderEditor`**

In `UIManager.cpp`, find `DrawShaderEditor`. After the editor text widget and Compile button section, and before the closing `ImGui::End()`, add:

```cpp
DrawShaderParameters();
```

**Step 3: Implement `DrawShaderParameters` in `UIManager.cpp`**

Add this function implementation (before or after `DrawShaderEditor` — keep related functions together):

```cpp
void UIManager::DrawShaderParameters() {
    ShaderPreset* preset = m_app.GetShaderManager().GetActivePreset();
    if (!preset || preset->params.empty()) return;

    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) return;

    bool anyChanged = false;

    for (auto& p : preset->params) {
        ImGui::PushID(p.name.c_str());

        switch (p.type) {

        case ShaderParamType::Float: {
            float v = p.values[0];
            if (ImGui::SliderFloat(p.label.c_str(), &v, p.min, p.max)) {
                // Snap to step
                if (p.step > 0.0f) v = std::round(v / p.step) * p.step;
                p.values[0] = v;
                anyChanged = true;
            }
            break;
        }

        case ShaderParamType::Bool: {
            bool b = p.values[0] > 0.5f;
            if (ImGui::Checkbox(p.label.c_str(), &b)) {
                p.values[0] = b ? 1.0f : 0.0f;
                anyChanged = true;
            }
            break;
        }

        case ShaderParamType::Long: {
            int idx = static_cast<int>(p.values[0]);
            // Build null-separated string for ImGui::Combo
            std::string items;
            for (const auto& lbl : p.longLabels) { items += lbl; items += '\0'; }
            items += '\0';
            if (ImGui::Combo(p.label.c_str(), &idx, items.c_str())) {
                p.values[0] = static_cast<float>(idx);
                anyChanged = true;
            }
            break;
        }

        case ShaderParamType::Color: {
            if (ImGui::ColorEdit4(p.label.c_str(), p.values)) {
                anyChanged = true;
            }
            break;
        }

        case ShaderParamType::Point2D: {
            // Label on its own line
            ImGui::Text("%s", p.label.c_str());

            ImVec2 padSize(120.0f, 120.0f);
            ImVec2 padPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##pad", padSize);

            if (ImGui::IsItemActive()) {
                ImVec2 mouse = ImGui::GetMousePos();
                float nx = (mouse.x - padPos.x) / padSize.x;
                float ny = (mouse.y - padPos.y) / padSize.y;
                nx = std::clamp(nx, 0.0f, 1.0f);
                ny = std::clamp(ny, 0.0f, 1.0f);
                p.values[0] = p.min + nx * (p.max - p.min);
                p.values[1] = p.min + ny * (p.max - p.min);
                anyChanged = true;
            }

            // Draw pad background and dot
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 padEnd(padPos.x + padSize.x, padPos.y + padSize.y);
            draw->AddRectFilled(padPos, padEnd, IM_COL32(40, 40, 40, 255));
            draw->AddRect(padPos, padEnd, IM_COL32(100, 100, 100, 255));
            float dotX = padPos.x + (p.values[0] - p.min) / (p.max - p.min + 1e-6f) * padSize.x;
            float dotY = padPos.y + (p.values[1] - p.min) / (p.max - p.min + 1e-6f) * padSize.y;
            draw->AddCircleFilled(ImVec2(dotX, dotY), 5.0f, IM_COL32(255, 200, 50, 255));
            draw->AddCircle(ImVec2(dotX, dotY), 5.0f, IM_COL32(255, 255, 255, 180));
            break;
        }

        case ShaderParamType::Event: {
            if (ImGui::Button(p.label.c_str())) {
                p.values[0] = 1.0f;
                anyChanged = true;
            }
            break;
        }

        } // switch

        ImGui::PopID();
    }

    // Reset to defaults button
    ImGui::Spacing();
    if (ImGui::SmallButton("Reset to defaults")) {
        for (auto& p : preset->params)
            std::copy(p.defaultValues, p.defaultValues + 4, p.values);
        anyChanged = true;
    }

    if (anyChanged) {
        m_app.OnParamChanged();
    }
}
```

**Step 4: Add required `#include` if missing**

`std::clamp` requires `<algorithm>`. Check the includes at the top of `UIManager.cpp`; add `#include <algorithm>` if not already present (it may be pulled in transitively via Common.h — if the build fails on `std::clamp`, add it explicitly).

**Step 5: Build**

```bash
cmake --build build --config Release
```

Expected: build succeeds.

**Step 6: Commit**

```bash
git add src/UIManager.h src/UIManager.cpp
git commit -m "feat: add shader parameter widgets to Shader Editor panel"
```

---

## Task 7: Example parameterised shader

**Files:**
- Create: `shaders/pixelate.hlsl`

**Step 1: Write the shader**

```hlsl
/*{
    "INPUTS": [
        {"NAME": "PixelSize",  "LABEL": "Pixel Size",   "TYPE": "float",
         "MIN": 1.0, "MAX": 64.0, "STEP": 1.0, "DEFAULT": 8.0},
        {"NAME": "Tint",       "LABEL": "Tint Colour",  "TYPE": "color",
         "DEFAULT": [1.0, 1.0, 1.0, 1.0]},
        {"NAME": "ShowGrid",   "LABEL": "Show Grid",    "TYPE": "bool",
         "DEFAULT": false},
        {"NAME": "GridColour", "LABEL": "Grid Colour",  "TYPE": "color",
         "DEFAULT": [0.0, 0.0, 0.0, 1.0]},
        {"NAME": "Center",     "LABEL": "Offset",       "TYPE": "point2d",
         "MIN": -0.5, "MAX": 0.5, "DEFAULT": [0.0, 0.0]}
    ]
}*/

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
    float2 uv  : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.uv + Center;

    float2 normalised = float2(PixelSize, PixelSize) / resolution;
    float2 pixelUV    = normalised * floor(uv / normalised);

    float4 col = videoTexture.Sample(videoSampler, pixelUV) * Tint;

    // Optional grid lines at pixel boundaries
    if (ShowGrid) {
        float2 frac = frac(uv / normalised);
        float  edge = min(frac.x, frac.y);
        if (edge < 0.05) col = GridColour;
    }

    return col;
}
```

**Step 2: Smoke test**

1. Build and run: `cmake --build build --config Release && ./build/Release/ShaderPlayer.exe`
2. Load any video.
3. Open Shader Library (F2) → "Scan Folder" → point at the `shaders/` directory.
4. Click `pixelate` in the library.
5. Open Shader Editor (F1).
6. Verify a "Parameters" collapsible section appears below the editor with:
   - "Pixel Size" slider (1–64)
   - "Tint Colour" RGBA colour picker
   - "Show Grid" checkbox
   - "Grid Colour" RGBA colour picker
   - "Offset" 2D pad
7. Drag the Pixel Size slider — pixelation should update live.
8. Enable "Show Grid" — grid lines appear on pixel boundaries.
9. Close and reopen the app — parameter values should be restored from config.json.

**Step 3: Commit**

```bash
git add shaders/pixelate.hlsl
git commit -m "feat: add pixelate.hlsl demonstrating all ISF parameter widget types"
```

---

## Done

All tasks complete. The shader parameter system is fully wired:
- ISF JSON block → parsed params → `#define` preamble → D3DCompile
- Sliders/pickers → `OnParamChanged()` → `SetCustomUniforms` → GPU cbuffer on next `BeginFrame`
- Param values persist in `config.json` and are restored on load
- `event` type fires for exactly one frame then auto-resets

See `docs/shader-parameter-guide.md` for the author-facing reference.
