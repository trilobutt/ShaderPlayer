# Keyframe Animation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add per-parameter keyframe animation to shader parameters, tied to absolute video time, with cubic bezier curve editing and config.json persistence.

**Architecture:** KeyframeTimeline as an optional struct on ShaderParam. Evaluation runs per-frame in Application between ProcessFrame and BeginFrame. UI adds keyframe toggle/editor inline with each parameter widget. Persistence extends existing config.json serialisation.

**Tech Stack:** C++20, ImGui (docking branch), nlohmann/json, Direct3D 11

---

### Task 1: Data Model — New Types in Common.h

**Files:**
- Modify: `src/Common.h:41-55`

**Step 1: Add keyframe types before ShaderParam**

Insert after line 40 (after the forward declarations, before `ShaderParamType`):

```cpp
// Keyframe interpolation
enum class InterpolationMode { Linear, EaseInOut, CubicBezier };

struct BezierHandles {
    float outX = 0.33f, outY = 0.0f;  // control point leaving this keyframe (normalised 0-1)
    float inX  = 0.67f, inY  = 1.0f;  // control point entering next keyframe (normalised 0-1)
};

struct Keyframe {
    float time = 0.0f;              // absolute video time in seconds
    float values[4] = {};           // same layout as ShaderParam::values
    InterpolationMode mode = InterpolationMode::Linear;
    BezierHandles handles;
};

struct KeyframeTimeline {
    bool enabled = false;
    std::vector<Keyframe> keyframes; // sorted by time

    // Evaluate interpolated value at given time. Writes to out[0..valueCount-1].
    // Returns true if a value was written (timeline enabled and non-empty).
    bool Evaluate(float time, float out[4], int valueCount) const;

    // Insert keyframe maintaining sort order by time. Returns index of inserted keyframe.
    int AddKeyframe(const Keyframe& kf);

    // Remove keyframe at index.
    void RemoveKeyframe(int index);
};
```

**Step 2: Add timeline field to ShaderParam**

Add after line 54 (`int cbufferOffset = 0;`):

```cpp
    std::optional<KeyframeTimeline> timeline;  // nullopt until user enables keyframing
```

**Step 3: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Compiles (KeyframeTimeline methods are declared but not defined yet, which is fine as long as nothing calls them yet).

**Step 4: Commit**

```bash
git add src/Common.h
git commit -m "feat: add Keyframe, KeyframeTimeline, BezierHandles types to Common.h"
```

---

### Task 2: KeyframeTimeline Implementation

**Files:**
- Create: `src/KeyframeTimeline.cpp`
- Modify: `CMakeLists.txt:95-105`

**Step 1: Create KeyframeTimeline.cpp**

```cpp
#include "Common.h"
#include <algorithm>
#include <cmath>

namespace SP {

// Cubic bezier evaluation: given control points (0,0), (cx1,cy1), (cx2,cy2), (1,1),
// solve for Y at a given X using binary search on the parametric t.
static float EvalCubicBezier(float cx1, float cy1, float cx2, float cy2, float x) {
    // Binary search for t where bezierX(t) ≈ x
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 16; ++i) {
        float t = (lo + hi) * 0.5f;
        float omt = 1.0f - t;
        float bx = 3.0f * omt * omt * t * cx1
                  + 3.0f * omt * t * t * cx2
                  + t * t * t;
        if (bx < x) lo = t;
        else         hi = t;
    }
    float t = (lo + hi) * 0.5f;
    float omt = 1.0f - t;
    return 3.0f * omt * omt * t * cy1
         + 3.0f * omt * t * t * cy2
         + t * t * t;
}

static float Smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

bool KeyframeTimeline::Evaluate(float time, float out[4], int valueCount) const {
    if (!enabled || keyframes.empty()) return false;

    // Clamp to first/last keyframe
    if (time <= keyframes.front().time) {
        for (int i = 0; i < valueCount; ++i) out[i] = keyframes.front().values[i];
        return true;
    }
    if (time >= keyframes.back().time) {
        for (int i = 0; i < valueCount; ++i) out[i] = keyframes.back().values[i];
        return true;
    }

    // Binary search for segment
    // Find last keyframe with time <= target
    int lo = 0, hi = static_cast<int>(keyframes.size()) - 1;
    while (lo < hi - 1) {
        int mid = (lo + hi) / 2;
        if (keyframes[mid].time <= time) lo = mid;
        else                              hi = mid;
    }

    const Keyframe& a = keyframes[lo];
    const Keyframe& b = keyframes[lo + 1];
    float segLen = b.time - a.time;
    float t = (segLen > 1e-9f) ? (time - a.time) / segLen : 0.0f;

    // Remap t based on interpolation mode
    float remapped = t;
    switch (a.mode) {
    case InterpolationMode::Linear:
        break;
    case InterpolationMode::EaseInOut:
        remapped = Smoothstep(t);
        break;
    case InterpolationMode::CubicBezier:
        remapped = EvalCubicBezier(a.handles.outX, a.handles.outY,
                                    a.handles.inX,  a.handles.inY, t);
        break;
    }

    // Lerp values
    for (int i = 0; i < valueCount; ++i) {
        out[i] = a.values[i] + (b.values[i] - a.values[i]) * remapped;
    }

    return true;
}

int KeyframeTimeline::AddKeyframe(const Keyframe& kf) {
    auto it = std::lower_bound(keyframes.begin(), keyframes.end(), kf.time,
        [](const Keyframe& k, float t) { return k.time < t; });
    int idx = static_cast<int>(it - keyframes.begin());
    keyframes.insert(it, kf);
    return idx;
}

void KeyframeTimeline::RemoveKeyframe(int index) {
    if (index >= 0 && index < static_cast<int>(keyframes.size())) {
        keyframes.erase(keyframes.begin() + index);
    }
}

} // namespace SP
```

**Step 2: Add to CMakeLists.txt**

In the `add_executable` block (line 95-105), add `src/KeyframeTimeline.cpp` after `src/WorkspaceManager.cpp`:

```cmake
    src/KeyframeTimeline.cpp
```

**Step 3: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Clean compile, 0 errors.

**Step 4: Commit**

```bash
git add src/KeyframeTimeline.cpp CMakeLists.txt
git commit -m "feat: implement KeyframeTimeline evaluation with cubic bezier interpolation"
```

---

### Task 3: Keyframe Evaluation in the Render Loop

**Files:**
- Modify: `src/Application.h:84-97`
- Modify: `src/Application.cpp:348-377,419-471`

**Step 1: Add EvaluateKeyframes declaration to Application.h**

After `static void PackParamValues(...)` (line 93), add:

```cpp
    void EvaluateKeyframes();
```

**Step 2: Implement EvaluateKeyframes in Application.cpp**

Add after `OnParamChanged()` (after line 417):

```cpp
void Application::EvaluateKeyframes() {
    ShaderPreset* preset = m_shaderManager->GetActivePreset();
    if (!preset) return;

    bool anyChanged = false;

    for (auto& p : preset->params) {
        if (!p.timeline || !p.timeline->enabled) continue;

        int valueCount = 1;
        if (p.type == ShaderParamType::Point2D) valueCount = 2;
        else if (p.type == ShaderParamType::Color) valueCount = 4;

        // For Bool/Long: step interpolation (snap to nearest keyframe, no lerp).
        // Evaluate still returns lerped values; we snap afterwards.
        float interpolated[4] = {};
        if (p.timeline->Evaluate(m_playbackTime, interpolated, valueCount)) {
            bool changed = false;
            for (int i = 0; i < valueCount; ++i) {
                float val = interpolated[i];
                // Step types: snap to 0 or 1 (bool) or round to int (long)
                if (p.type == ShaderParamType::Bool)
                    val = (val >= 0.5f) ? 1.0f : 0.0f;
                else if (p.type == ShaderParamType::Long)
                    val = std::round(val);

                if (p.values[i] != val) {
                    p.values[i] = val;
                    changed = true;
                }
            }
            if (changed) anyChanged = true;
        }
    }

    if (anyChanged) OnParamChanged();
}
```

**Step 3: Call EvaluateKeyframes in the render loop**

In `Application::RenderFrame()`, after `m_renderer.SetShaderTime(m_playbackTime);` (line 426) and before `m_renderer.BeginFrame();` (line 429), insert:

```cpp
    // Evaluate keyframe animations at current playback time
    EvaluateKeyframes();
```

**Step 4: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Clean compile.

**Step 5: Commit**

```bash
git add src/Application.h src/Application.cpp
git commit -m "feat: evaluate keyframe timelines per-frame in render loop"
```

---

### Task 4: Keyframe Persistence in ConfigManager

**Files:**
- Modify: `src/ConfigManager.cpp:7-43`

**Step 1: Extend to_json for ShaderPreset**

After the `paramValues` serialisation (after line 26, before the closing `}`), add keyframe serialisation:

```cpp
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
```

**Step 2: Extend from_json for ShaderPreset**

After the `paramValues` deserialisation (after line 42, before the closing `}`), add:

```cpp
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
```

**Step 3: Add savedKeyframes field to ShaderPreset in Common.h**

In `ShaderPreset` (after line 80, after `savedParamValues`), add:

```cpp
    std::unordered_map<std::string, KeyframeTimeline> savedKeyframes;
```

**Step 4: Restore keyframes on load in Application::Initialize**

In `Application::Initialize`, inside the param restore loop (around line 49-56), after the value restoration, add:

```cpp
                    // Restore saved keyframe timeline
                    auto kit = configPreset.savedKeyframes.find(param.name);
                    if (kit != configPreset.savedKeyframes.end()) {
                        param.timeline = kit->second;
                    }
```

**Step 5: Add `#include <algorithm>` to ConfigManager.cpp**

Needed for `std::sort`. Add after existing includes at the top.

**Step 6: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Clean compile.

**Step 7: Commit**

```bash
git add src/Common.h src/ConfigManager.cpp src/Application.cpp
git commit -m "feat: persist keyframe timelines in config.json"
```

---

### Task 5: Keyframe UI — Toggle, Add/Remove, Keyframe Chips

**Files:**
- Modify: `src/UIManager.cpp:374-493` (DrawShaderParameters)
- Modify: `src/UIManager.h:59-119`

**Step 1: Add UI state for keyframe editing to UIManager.h**

After the existing private member variables (around line 118), add:

```cpp
    // Keyframe editing state
    int m_selectedKeyframeParam = -1;   // index into preset->params, or -1
    int m_selectedKeyframeIndex = -1;   // index into timeline->keyframes, or -1
```

**Step 2: Add keyframe toggle and controls per parameter**

In `DrawShaderParameters()` (UIManager.cpp), after each parameter's widget switch block (after `ImGui::PopID()` at line 477, but restructure so the keyframe UI is *inside* the PushID/PopID for that param), add the keyframe UI for each parameter.

Replace the entire `DrawShaderParameters()` method with the version that adds keyframe controls after each parameter widget. The structure for each param becomes:

```
[existing widget]  [KF toggle button]
  if KF enabled:
    [Add Keyframe button]  [keyframe timestamp chips]
    if a keyframe is selected:
      [time input] [value widget] [mode combo] [bezier curve] [delete button]
```

The key additions per parameter (inside the `for` loop, after the `switch` block, before `PopID`):

```cpp
        // --- Keyframe toggle (skip Event type) ---
        if (p.type != ShaderParamType::Event) {
            ImGui::SameLine();
            bool hasTimeline = p.timeline.has_value() && p.timeline->enabled;
            if (hasTimeline) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.6f, 0.1f, 1.0f));
            std::string kfLabel = hasTimeline ? "KF##kf" : "+KF##kf";
            if (ImGui::SmallButton(kfLabel.c_str())) {
                if (!p.timeline.has_value()) {
                    p.timeline.emplace();
                }
                p.timeline->enabled = !p.timeline->enabled;
            }
            if (hasTimeline) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle keyframe animation");

            // Keyframe controls when enabled
            if (p.timeline && p.timeline->enabled) {
                int paramIdx = static_cast<int>(&p - &preset->params[0]);
                int valueCount = 1;
                if (p.type == ShaderParamType::Point2D) valueCount = 2;
                else if (p.type == ShaderParamType::Color) valueCount = 4;

                // Add keyframe at current time
                if (ImGui::SmallButton("+ Key")) {
                    Keyframe kf;
                    kf.time = m_app.GetPlaybackTime();
                    for (int i = 0; i < 4; ++i) kf.values[i] = p.values[i];
                    int idx = p.timeline->AddKeyframe(kf);
                    m_selectedKeyframeParam = paramIdx;
                    m_selectedKeyframeIndex = idx;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add keyframe at current time");

                // Keyframe chips
                ImGui::SameLine();
                auto& kfs = p.timeline->keyframes;
                for (int ki = 0; ki < static_cast<int>(kfs.size()); ++ki) {
                    ImGui::SameLine();
                    char chipLabel[32];
                    snprintf(chipLabel, sizeof(chipLabel), "%.1fs##kf%d", kfs[ki].time, ki);
                    bool isSelected = (m_selectedKeyframeParam == paramIdx && m_selectedKeyframeIndex == ki);
                    if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
                    if (ImGui::SmallButton(chipLabel)) {
                        m_selectedKeyframeParam = paramIdx;
                        m_selectedKeyframeIndex = ki;
                        // Seek video to keyframe time
                        m_app.SeekTo(kfs[ki].time);
                    }
                    if (isSelected) ImGui::PopStyleColor();
                }

                // Keyframe detail editor
                if (m_selectedKeyframeParam == paramIdx &&
                    m_selectedKeyframeIndex >= 0 &&
                    m_selectedKeyframeIndex < static_cast<int>(kfs.size()))
                {
                    DrawKeyframeDetail(p, *p.timeline, m_selectedKeyframeIndex,
                                       valueCount, anyChanged);
                }
            }
        }
```

**Step 3: Add `GetPlaybackTime()` accessor to Application.h**

After `PlaybackState GetPlaybackState() const` (line 42), add:

```cpp
    float GetPlaybackTime() const { return m_playbackTime; }
```

**Step 4: Add DrawKeyframeDetail declaration to UIManager.h**

In the private section (around line 72), add:

```cpp
    void DrawKeyframeDetail(ShaderParam& param, KeyframeTimeline& timeline,
                            int keyframeIndex, int valueCount, bool& anyChanged);
```

**Step 5: Build (DrawKeyframeDetail is Task 6)**

Stub it out temporarily to verify the toggle/chips compile. Add to UIManager.cpp:

```cpp
void UIManager::DrawKeyframeDetail(ShaderParam& param, KeyframeTimeline& timeline,
                                    int keyframeIndex, int valueCount, bool& anyChanged) {
    // Implemented in Task 6
}
```

**Step 6: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | tail -10`
Expected: Clean compile.

**Step 7: Commit**

```bash
git add src/UIManager.h src/UIManager.cpp src/Application.h
git commit -m "feat: keyframe toggle, add/remove, and timestamp chips per parameter"
```

---

### Task 6: Keyframe Detail Panel with Bezier Curve Editor

**Files:**
- Modify: `src/UIManager.cpp` (replace DrawKeyframeDetail stub)

**Step 1: Implement DrawKeyframeDetail**

Replace the stub with:

```cpp
void UIManager::DrawKeyframeDetail(ShaderParam& param, KeyframeTimeline& timeline,
                                    int keyframeIndex, int valueCount, bool& anyChanged) {
    Keyframe& kf = timeline.keyframes[keyframeIndex];

    ImGui::Indent(16.0f);
    ImGui::PushID(keyframeIndex + 1000); // avoid ID collision with param widgets

    // Time input
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputFloat("Time##kftime", &kf.time, 0.1f, 1.0f, "%.2f s")) {
        // Re-sort: remove and re-insert to maintain order
        Keyframe copy = kf;
        timeline.RemoveKeyframe(keyframeIndex);
        int newIdx = timeline.AddKeyframe(copy);
        m_selectedKeyframeIndex = newIdx;
    }

    // Value editing based on type
    ImGui::SameLine();
    switch (param.type) {
    case ShaderParamType::Float: {
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat("Value##kfval", &kf.values[0], param.min, param.max);
        break;
    }
    case ShaderParamType::Bool: {
        bool b = kf.values[0] > 0.5f;
        if (ImGui::Checkbox("Value##kfval", &b)) kf.values[0] = b ? 1.0f : 0.0f;
        break;
    }
    case ShaderParamType::Long: {
        int idx = static_cast<int>(kf.values[0]);
        std::string items;
        for (const auto& lbl : param.longLabels) { items += lbl; items += '\0'; }
        items += '\0';
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo("Value##kfval", &idx, items.c_str())) {
            kf.values[0] = static_cast<float>(idx);
        }
        break;
    }
    case ShaderParamType::Color: {
        ImGui::ColorEdit4("Value##kfval", kf.values);
        break;
    }
    case ShaderParamType::Point2D: {
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputFloat("X##kfval", &kf.values[0], 0.01f, 0.1f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputFloat("Y##kfval", &kf.values[1], 0.01f, 0.1f);
        break;
    }
    default: break;
    }

    // Interpolation mode
    int modeInt = static_cast<int>(kf.mode);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("Lerp##kfmode", &modeInt, "Linear\0Ease In/Out\0Custom Bezier\0")) {
        InterpolationMode newMode = static_cast<InterpolationMode>(modeInt);
        // When switching to CubicBezier from EaseInOut, set handles to approximate smoothstep
        if (newMode == InterpolationMode::CubicBezier && kf.mode == InterpolationMode::EaseInOut) {
            kf.handles = { 0.42f, 0.0f, 0.58f, 1.0f };
        }
        kf.mode = newMode;
    }

    // Bezier curve editor (inline, ~160x100px)
    if (kf.mode == InterpolationMode::CubicBezier ||
        kf.mode == InterpolationMode::EaseInOut ||
        kf.mode == InterpolationMode::Linear) {

        ImVec2 curveSize(160.0f, 100.0f);
        ImVec2 curvePos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##curve", curveSize);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        ImVec2 p0(curvePos.x, curvePos.y + curveSize.y);             // bottom-left = (0,0)
        ImVec2 p3(curvePos.x + curveSize.x, curvePos.y);             // top-right = (1,1)

        // Background
        draw->AddRectFilled(curvePos, ImVec2(curvePos.x + curveSize.x, curvePos.y + curveSize.y),
                            IM_COL32(30, 30, 30, 255));
        draw->AddRect(curvePos, ImVec2(curvePos.x + curveSize.x, curvePos.y + curveSize.y),
                      IM_COL32(80, 80, 80, 255));

        // Diagonal reference line
        draw->AddLine(p0, p3, IM_COL32(60, 60, 60, 255));

        // Determine bezier control points for display
        float cx1, cy1, cx2, cy2;
        if (kf.mode == InterpolationMode::Linear) {
            cx1 = 0.0f; cy1 = 0.0f; cx2 = 1.0f; cy2 = 1.0f;
        } else if (kf.mode == InterpolationMode::EaseInOut) {
            cx1 = 0.42f; cy1 = 0.0f; cx2 = 0.58f; cy2 = 1.0f;
        } else {
            cx1 = kf.handles.outX; cy1 = kf.handles.outY;
            cx2 = kf.handles.inX;  cy2 = kf.handles.inY;
        }

        // Draw curve using bezier
        ImVec2 cp1(curvePos.x + cx1 * curveSize.x, curvePos.y + (1.0f - cy1) * curveSize.y);
        ImVec2 cp2(curvePos.x + cx2 * curveSize.x, curvePos.y + (1.0f - cy2) * curveSize.y);
        draw->AddBezierCubic(p0, cp1, cp2, p3, IM_COL32(220, 180, 50, 255), 2.0f);

        // Draggable handles (only for CubicBezier mode)
        if (kf.mode == InterpolationMode::CubicBezier) {
            // Handle 1 (out)
            ImVec2 h1(curvePos.x + kf.handles.outX * curveSize.x,
                      curvePos.y + (1.0f - kf.handles.outY) * curveSize.y);
            draw->AddLine(p0, h1, IM_COL32(100, 180, 255, 200));
            draw->AddCircleFilled(h1, 5.0f, IM_COL32(100, 180, 255, 255));

            // Handle 2 (in)
            ImVec2 h2(curvePos.x + kf.handles.inX * curveSize.x,
                      curvePos.y + (1.0f - kf.handles.inY) * curveSize.y);
            draw->AddLine(p3, h2, IM_COL32(255, 100, 100, 200));
            draw->AddCircleFilled(h2, 5.0f, IM_COL32(255, 100, 100, 255));

            // Drag interaction
            if (ImGui::IsItemActive()) {
                ImVec2 mouse = ImGui::GetMousePos();
                float mx = (mouse.x - curvePos.x) / curveSize.x;
                float my = 1.0f - (mouse.y - curvePos.y) / curveSize.y;
                mx = std::clamp(mx, 0.0f, 1.0f);  // X clamped to [0,1]
                // Y unclamped for overshoot

                // Determine which handle is closer to mouse
                float d1 = (mouse.x - h1.x) * (mouse.x - h1.x) + (mouse.y - h1.y) * (mouse.y - h1.y);
                float d2 = (mouse.x - h2.x) * (mouse.x - h2.x) + (mouse.y - h2.y) * (mouse.y - h2.y);
                if (d1 <= d2) {
                    kf.handles.outX = mx;
                    kf.handles.outY = my;
                } else {
                    kf.handles.inX = mx;
                    kf.handles.inY = my;
                }
            }
        }

        // Click on EaseInOut curve → convert to CubicBezier with equivalent handles
        if (kf.mode == InterpolationMode::EaseInOut && ImGui::IsItemClicked()) {
            kf.mode = InterpolationMode::CubicBezier;
            kf.handles = { 0.42f, 0.0f, 0.58f, 1.0f };
        }
    }

    // Delete button
    if (ImGui::SmallButton("Delete Keyframe")) {
        timeline.RemoveKeyframe(keyframeIndex);
        m_selectedKeyframeIndex = -1;
        m_selectedKeyframeParam = -1;
    }

    ImGui::PopID();
    ImGui::Unindent(16.0f);
}
```

**Step 2: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | tail -10`
Expected: Clean compile.

**Step 3: Commit**

```bash
git add src/UIManager.cpp
git commit -m "feat: keyframe detail panel with bezier curve editor"
```

---

### Task 7: Transport Bar Keyframe Markers

**Files:**
- Modify: `src/UIManager.cpp:578-649` (DrawTransportControls)

**Step 1: Add keyframe markers on the transport timeline**

After the timeline `SliderFloat` call (after line 606, inside the `if (decoder.IsOpen())` block), add marker rendering:

```cpp
            // Draw keyframe markers for the currently-selected parameter
            if (m_selectedKeyframeParam >= 0) {
                ShaderPreset* preset = m_app.GetShaderManager().GetActivePreset();
                if (preset && m_selectedKeyframeParam < static_cast<int>(preset->params.size())) {
                    auto& param = preset->params[m_selectedKeyframeParam];
                    if (param.timeline && param.timeline->enabled) {
                        // Get the slider's screen rect (ImGui stores the last item rect)
                        ImVec2 sliderMin = ImGui::GetItemRectMin();
                        ImVec2 sliderMax = ImGui::GetItemRectMax();
                        float sliderW = sliderMax.x - sliderMin.x;
                        ImDrawList* draw = ImGui::GetWindowDrawList();

                        for (const auto& kf : param.timeline->keyframes) {
                            float frac = (duration > 0.0f) ? kf.time / duration : 0.0f;
                            float x = sliderMin.x + frac * sliderW;
                            float cy = (sliderMin.y + sliderMax.y) * 0.5f;
                            float sz = 4.0f;
                            // Diamond marker
                            draw->AddQuadFilled(
                                ImVec2(x, cy - sz), ImVec2(x + sz, cy),
                                ImVec2(x, cy + sz), ImVec2(x - sz, cy),
                                IM_COL32(220, 180, 50, 200));
                        }
                    }
                }
            }
```

**Step 2: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Clean compile.

**Step 3: Commit**

```bash
git add src/UIManager.cpp
git commit -m "feat: diamond keyframe markers on transport timeline"
```

---

### Task 8: Read-Only Widget State During Keyframe Playback

**Files:**
- Modify: `src/UIManager.cpp` (DrawShaderParameters, inside the for loop)

**Step 1: Disable widget interaction when keyframes are driving the parameter**

At the top of each parameter's widget rendering (inside the `for` loop, right after `ImGui::PushID(&p)`), add:

```cpp
        bool kfDriven = p.timeline && p.timeline->enabled && !p.timeline->keyframes.empty()
                        && m_app.GetPlaybackState() == PlaybackState::Playing;
        if (kfDriven) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            ImGui::BeginDisabled();
        }
```

And after the widget switch block (before the keyframe toggle code), close the disabled state:

```cpp
        if (kfDriven) {
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
        }
```

**Step 2: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Clean compile.

**Step 3: Commit**

```bash
git add src/UIManager.cpp
git commit -m "feat: grey out parameter widgets when driven by keyframes during playback"
```

---

### Task 9: Full Build, Manual Testing, and CLAUDE.md Update

**Files:**
- Modify: `CLAUDE.md`

**Step 1: Clean build**

Run: `cmake --build build --config Release 2>&1 | tail -10`
Expected: Clean compile in Release.

**Step 2: Manual testing checklist**

1. Open a video, load a shader with float params (e.g. pixelate.hlsl)
2. Click "KF" toggle on PixelSize → toggle turns gold
3. Seek to 0s, click "+ Key" → chip appears at "0.0s"
4. Seek to 5s, change PixelSize to 32, click "+ Key" → second chip "5.0s"
5. Play video → PixelSize interpolates from first to second keyframe value
6. Click on a keyframe chip → detail panel opens, mode defaults to Linear
7. Change mode to Ease In/Out → curve preview updates
8. Change mode to Custom Bezier → handles appear, drag them
9. Click "Delete Keyframe" → keyframe removed
10. Close and reopen app → keyframes restored from config.json
11. Transport bar shows diamond markers at keyframe positions

**Step 3: Update CLAUDE.md**

Add a new section under "## Shader Parameter System" documenting:
- KeyframeTimeline struct and its location
- Evaluation pipeline position in the render loop
- Config persistence format
- UI controls summary

**Step 4: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: document keyframe animation system in CLAUDE.md"
```
