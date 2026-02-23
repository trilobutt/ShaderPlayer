#include "UIManager.h"
#include "Application.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <algorithm>
#include <cmath>

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace SP {

UIManager::UIManager(Application& app)
    : m_app(app)
{
}

UIManager::~UIManager() {
    Shutdown();
}

bool UIManager::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context);

    // Setup text editor
    m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::HLSL());
    m_editor.SetShowWhitespaces(false);
    m_editor.SetText(ShaderManager::GetShaderTemplate());

    return true;
}

void UIManager::Shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void UIManager::BeginFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void UIManager::EndFrame() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void UIManager::Render() {
    // Create dockspace
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags windowFlags = 
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("DockSpace", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("MainDockspace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    DrawMenuBar();
    
    ImGui::End();

    // Draw panels
    DrawVideoViewport();
    
    if (m_showEditor) {
        DrawShaderEditor();
    }
    
    if (m_showLibrary) {
        DrawShaderLibrary();
    }
    
    if (m_showTransport) {
        DrawTransportControls();
    }
    
    if (m_showRecording) {
        DrawRecordingPanel();
    }

    DrawNotifications();
    
    if (m_showKeybindingModal) {
        DrawKeybindingModal();
    }
    
    if (m_showNewShaderModal) {
        DrawNewShaderModal();
    }

    if (m_showKeybindingsPanel) {
        DrawKeybindingsPanel();
    }

    DrawShaderParameters();

    if (m_showManageWorkspacesModal) {
        DrawManageWorkspacesModal();
    }
    if (m_showWorkspaceKeybindingModal) {
        DrawWorkspaceKeybindingModal();
    }

    // "Save Current As" popup
    if (m_showSaveWorkspacePopup) {
        ImGui::OpenPopup("Save Workspace As");
        m_showSaveWorkspacePopup = false;
    }
    if (ImGui::BeginPopup("Save Workspace As")) {
        ImGui::Text("Workspace name:");
        ImGui::SetNextItemWidth(260.0f);
        bool submit = ImGui::InputText("##wsname", m_saveWorkspaceName,
                                       sizeof(m_saveWorkspaceName),
                                       ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Save") || submit) {
            if (m_saveWorkspaceName[0] != '\0') {
                m_app.GetWorkspaceManager().SavePreset(
                    m_saveWorkspaceName,
                    m_showEditor, m_showLibrary, m_showTransport,
                    m_showRecording, m_showKeybindingsPanel);
                ShowNotification("Workspace saved: " + std::string(m_saveWorkspaceName));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Handle auto-compile
    if (m_editorNeedsCompile && m_app.GetConfig().autoCompileOnSave) {
        m_compileTimer += ImGui::GetIO().DeltaTime;
        if (m_compileTimer >= m_app.GetConfig().autoCompileDelayMs / 1000.0f) {
            m_app.CompileCurrentShader(GetEditorContent());
            m_editorNeedsCompile = false;
            m_compileTimer = 0.0f;
        }
    }
}

void UIManager::DrawMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Video...", "Ctrl+O")) {
                m_app.OpenVideoDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Shader", "Ctrl+S")) {
                m_app.SaveCurrentShader(GetEditorContent());
            }
            if (ImGui::MenuItem("Save Shader As...")) {
                m_app.SaveShaderAsDialog(GetEditorContent());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                m_app.RequestExit();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Shader Editor", "F1", &m_showEditor);
            ImGui::MenuItem("Shader Library", "F2", &m_showLibrary);
            ImGui::MenuItem("Transport Controls", "F3", &m_showTransport);
            ImGui::MenuItem("Recording Panel", "F4", &m_showRecording);
            ImGui::MenuItem("Keybindings", "F6", &m_showKeybindingsPanel);

            ImGui::Separator();
            if (ImGui::BeginMenu("Workspace Presets")) {
                auto& wm = m_app.GetWorkspaceManager();

                if (ImGui::MenuItem("Save Current As...")) {
                    m_showSaveWorkspacePopup = true;
                    memset(m_saveWorkspaceName, 0, sizeof(m_saveWorkspaceName));
                }

                ImGui::Separator();

                const auto& presets = wm.GetPresets();
                for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
                    const auto& wp = presets[i];
                    std::string label = wp.name;
                    if (wp.shortcutKey != 0) {
                        label += "  " + m_app.GetComboName(wp.shortcutKey, wp.shortcutModifiers);
                    }
                    if (ImGui::MenuItem(label.c_str())) {
                        m_app.LoadWorkspacePreset(i);
                    }
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Manage Workspaces...")) {
                    m_showManageWorkspacesModal = true;
                }

                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Shader")) {
            if (ImGui::MenuItem("New Shader...", "Ctrl+N")) {
                m_showNewShaderModal = true;
                memset(m_newShaderName, 0, sizeof(m_newShaderName));
            }
            if (ImGui::MenuItem("Compile", "F5")) {
                m_app.CompileCurrentShader(GetEditorContent());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset to Passthrough", "Escape")) {
                m_app.GetShaderManager().SetPassthrough();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Recording")) {
            if (!m_app.GetEncoder().IsRecording()) {
                if (ImGui::MenuItem("Start Recording", "F9")) {
                    RecordingSettings settings;
                    settings.outputPath = m_recordingPath;
                    settings.codec = (m_recordingCodec == 0) ? "libx264" : "prores_ks";
                    settings.bitrate = m_recordingBitrate * 1000000;
                    settings.proresProfile = m_proresProfile;
                    m_app.StartRecording(settings);
                }
            } else {
                if (ImGui::MenuItem("Stop Recording", "F9")) {
                    m_app.StopRecording();
                }
            }
            ImGui::Separator();
            ImGui::MenuItem("Recording Settings...", nullptr, &m_showRecording);
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

void UIManager::DrawVideoViewport() {
    ImGui::Begin("Video", nullptr,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    auto& decoder = m_app.GetDecoder();

    if (decoder.IsOpen()) {
        // Info line above the image
        ImGui::Text("%dx%d @ %.2f fps | %s",
            decoder.GetWidth(), decoder.GetHeight(),
            decoder.GetFPS(), decoder.GetCodecName().c_str());

        ID3D11ShaderResourceView* srv = m_app.GetRenderer().GetDisplaySRV();
        if (srv) {
            // Compute letterbox rect: largest fit inside the available content area
            // while preserving the video's exact aspect ratio.
            const ImVec2 origin  = ImGui::GetCursorScreenPos();
            const ImVec2 avail   = ImGui::GetContentRegionAvail();
            const float  videoW  = static_cast<float>(decoder.GetWidth());
            const float  videoH  = static_cast<float>(decoder.GetHeight());
            const float  scale   = std::min(avail.x / videoW, avail.y / videoH);
            const float  drawW   = videoW * scale;
            const float  drawH   = videoH * scale;
            const float  padX    = (avail.x - drawW) * 0.5f;
            const float  padY    = (avail.y - drawH) * 0.5f;

            ImGui::SetCursorScreenPos(ImVec2(origin.x + padX, origin.y + padY));
            ImGui::Image(reinterpret_cast<ImTextureID>(srv), ImVec2(drawW, drawH));
        }
    } else {
        // No video loaded: centre a button + hint text vertically and horizontally
        const ImVec2 cursorStart = ImGui::GetCursorPos();
        const ImVec2 avail       = ImGui::GetContentRegionAvail();
        constexpr float kButtonW = 200.0f;
        constexpr float kButtonH = 40.0f;
        const char* hint         = "or drag & drop a video file";
        const float  totalH      = kButtonH + 8.0f + ImGui::GetTextLineHeight();

        ImGui::SetCursorPos(ImVec2(
            cursorStart.x + (avail.x - kButtonW) * 0.5f,
            cursorStart.y + (avail.y - totalH)   * 0.5f));

        if (ImGui::Button("Open Video...", ImVec2(kButtonW, kButtonH))) {
            m_app.OpenVideoDialog();
        }

        const float hintW = ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - hintW) * 0.5f);
        ImGui::TextDisabled("%s", hint);
    }

    ImGui::End();
}

void UIManager::DrawShaderEditor() {
    ImGui::SetNextWindowSize(ImVec2(m_editorWidth, 400), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Shader Editor", &m_showEditor, ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::Button("Compile (F5)")) {
                m_app.CompileCurrentShader(GetEditorContent());
            }
            
            auto* preset = m_app.GetShaderManager().GetActivePreset();
            if (preset) {
                ImGui::SameLine();
                if (preset->isValid) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "OK");
                } else {
                    ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "Error");
                }
            }
            
            ImGui::EndMenuBar();
        }

        // Error display
        auto* preset = m_app.GetShaderManager().GetActivePreset();
        if (preset && !preset->compileError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextWrapped("%s", preset->compileError.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        // Text editor
        m_editor.Render("ShaderCode");
        
        if (m_editor.IsTextChanged()) {
            m_editorNeedsCompile = true;
            m_compileTimer = 0.0f;
        }

        m_editorWidth = ImGui::GetWindowWidth();
    }
    ImGui::End();
}

void UIManager::DrawShaderParameters() {
    ImGui::SetNextWindowSize(ImVec2(300, 350), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Shader Parameters")) {
        ImGui::End();
        return;
    }

    ShaderPreset* preset = m_app.GetShaderManager().GetActivePreset();
    if (!preset || preset->params.empty()) {
        ImGui::TextDisabled("No parameters for active shader.");
        ImGui::End();
        return;
    }

    bool anyChanged = false;

    for (auto& p : preset->params) {
        ImGui::PushID(&p);

        // Check if parameter is being driven by keyframes during playback
        bool kfDriven = p.timeline && p.timeline->enabled && !p.timeline->keyframes.empty()
                        && m_app.GetPlaybackState() == PlaybackState::Playing;
        if (kfDriven) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            ImGui::BeginDisabled();
        }

        switch (p.type) {

        case ShaderParamType::Float: {
            float v = p.values[0];
            if (ImGui::SliderFloat(p.label.c_str(), &v, p.min, p.max)) {
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
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 padEnd(padPos.x + padSize.x, padPos.y + padSize.y);
            draw->AddRectFilled(padPos, padEnd, IM_COL32(40, 40, 40, 255));
            draw->AddRect(padPos, padEnd, IM_COL32(100, 100, 100, 255));
            float range = p.max - p.min;
            float dotX = padPos.x + (range > 1e-6f ? (p.values[0] - p.min) / range : 0.0f) * padSize.x;
            float dotY = padPos.y + (range > 1e-6f ? (p.values[1] - p.min) / range : 0.0f) * padSize.y;
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

        if (kfDriven) {
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
        }

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
                    int kidx = p.timeline->AddKeyframe(kf);
                    m_selectedKeyframeParam = paramIdx;
                    m_selectedKeyframeIndex = kidx;
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
                        m_app.SeekTo(kfs[ki].time);
                    }
                    if (isSelected) ImGui::PopStyleColor();
                }

                // Keyframe detail editor (implemented in Task 6)
                if (m_selectedKeyframeParam == paramIdx &&
                    m_selectedKeyframeIndex >= 0 &&
                    m_selectedKeyframeIndex < static_cast<int>(kfs.size()))
                {
                    DrawKeyframeDetail(p, *p.timeline, m_selectedKeyframeIndex,
                                       valueCount, anyChanged);
                }
            }
        }

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

    ImGui::End();
}

void UIManager::DrawKeyframeDetail(ShaderParam& param, KeyframeTimeline& timeline,
                                    int keyframeIndex, int valueCount, bool& anyChanged) {
    Keyframe& kf = timeline.keyframes[keyframeIndex];

    ImGui::Indent(16.0f);
    ImGui::PushID(keyframeIndex + 1000); // avoid ID collision with param widgets

    // Time input
    ImGui::SetNextItemWidth(100.0f);
    float editTime = kf.time;
    if (ImGui::InputFloat("Time##kftime", &editTime, 0.1f, 1.0f, "%.2f s")) {
        if (editTime != kf.time) {
            // Re-sort: remove and re-insert to maintain order
            Keyframe copy = kf;
            copy.time = editTime;
            timeline.RemoveKeyframe(keyframeIndex);
            int newIdx = timeline.AddKeyframe(copy);
            m_selectedKeyframeIndex = newIdx;
            // Early return since keyframeIndex is now invalid
            ImGui::PopID();
            ImGui::Unindent(16.0f);
            return;
        }
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
    {
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

void UIManager::DrawShaderLibrary() {
    ImGui::SetNextWindowSize(ImVec2(300, m_libraryHeight), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Shader Library", &m_showLibrary)) {
        if (ImGui::Button("+ New")) {
            m_showNewShaderModal = true;
            memset(m_newShaderName, 0, sizeof(m_newShaderName));
        }
        ImGui::SameLine();
        if (ImGui::Button("Scan Folder")) {
            m_app.ScanFolderDialog();
        }

        ImGui::Separator();

        // Passthrough option
        bool isPassthrough = m_app.GetShaderManager().IsPassthrough();
        if (ImGui::Selectable("(No Effect)", isPassthrough)) {
            m_app.GetShaderManager().SetPassthrough();
        }

        // Shader list
        auto& manager = m_app.GetShaderManager();
        for (int i = 0; i < manager.GetPresetCount(); ++i) {
            auto* preset = manager.GetPreset(i);
            if (!preset) continue;

            ImGui::PushID(i);
            
            bool isActive = (manager.GetActivePresetIndex() == i);
            
            // Status indicator
            if (preset->isValid) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "*");
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "!");
            }
            ImGui::SameLine();

            // Selectable name — single click activates, double-click opens keybinding modal
            if (ImGui::Selectable(preset->name.c_str(), isActive)) {
                manager.SetActivePreset(i);
                m_app.OnParamChanged();
                m_editor.SetText(preset->source);
            }
            if (ImGui::IsItemHovered()) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    m_keybindingPresetIndex = i;
                    m_keybindingConflictMsg.clear();
                    m_showKeybindingModal = true;
                }
                ImGui::SetTooltip("Double-click to set keybinding");
            }

            // Right-aligned keybinding label
            if (preset->shortcutKey != 0) {
                std::string combo = "[" + m_app.GetComboName(preset->shortcutKey, preset->shortcutModifiers) + "]";
                float textW = ImGui::CalcTextSize(combo.c_str()).x;
                ImGui::SameLine(ImGui::GetContentRegionMax().x - textW - 4.0f);
                ImGui::TextDisabled("%s", combo.c_str());
            }

            // Context menu
            if (ImGui::BeginPopupContextItem("##ctx")) {
                if (ImGui::MenuItem("Set Keybinding...")) {
                    m_keybindingPresetIndex = i;
                    m_showKeybindingModal = true;
                    m_keybindingConflictMsg.clear();
                }
                if (ImGui::MenuItem("Remove")) {
                    manager.RemovePreset(i);
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
        
        m_libraryHeight = ImGui::GetWindowHeight();
    }
    ImGui::End();
}

void UIManager::DrawTransportControls() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize;

    // Set initial position only once; after that the user can move/dock freely.
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 windowPos(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                     viewport->WorkPos.y + viewport->WorkSize.y - 60);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, ImVec2(0.5f, 1.0f));

    if (ImGui::Begin("Transport", &m_showTransport, flags)) {
        auto& decoder = m_app.GetDecoder();
        
        // Play/Pause button
        bool isPlaying = (m_app.GetPlaybackState() == PlaybackState::Playing);
        if (ImGui::Button(isPlaying ? "||" : ">", ImVec2(40, 30))) {
            m_app.TogglePlayback();
        }
        
        ImGui::SameLine();
        
        // Timeline slider
        if (decoder.IsOpen()) {
            float currentTime = static_cast<float>(decoder.GetCurrentTime());
            float duration = static_cast<float>(decoder.GetDuration());
            
            ImGui::SetNextItemWidth(400);
            if (ImGui::SliderFloat("##timeline", &currentTime, 0.0f, duration, "%.1f s")) {
                m_app.SeekTo(currentTime);
            }
            
            ImGui::SameLine();
            ImGui::Text("/ %.1f s", duration);
        } else {
            ImGui::Text("No video loaded");
        }

        // Record / Stop button
        ImGui::SameLine();
        if (m_app.GetEncoder().IsRecording()) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button("Stop##rec", ImVec2(50, 30))) {
                m_app.StopRecording();
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[REC]");
            ImGui::SameLine();
            ImGui::Text("%lld", m_app.GetEncoder().GetFramesEncoded());
        } else {
            if (ImGui::Button("Rec##rec", ImVec2(50, 30))) {
                RecordingSettings settings;
                settings.outputPath = m_recordingPath;
                settings.codec = (m_recordingCodec == 0) ? "libx264" : "prores_ks";
                settings.bitrate = m_recordingBitrate * 1000000;
                settings.proresProfile = m_proresProfile;
                m_app.StartRecording(settings);
            }
        }

        // Recording settings panel toggle
        ImGui::SameLine();
        if (ImGui::Button("...##recsettings", ImVec2(24, 30))) {
            m_showRecording = !m_showRecording;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Recording settings");
        }
    }
    ImGui::End();
}

void UIManager::DrawRecordingPanel() {
    if (ImGui::Begin("Recording Settings", &m_showRecording)) {
        ImGui::Text("Output Path");
        float browseW = 80.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseW - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##outputPath", m_recordingPath, sizeof(m_recordingPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(browseW, 0))) {
            m_app.OpenRecordingOutputDialog(m_recordingPath, sizeof(m_recordingPath));
        }
        
        ImGui::Combo("Codec", &m_recordingCodec, "H.264 (MP4)\0ProRes (MOV)\0");
        
        if (m_recordingCodec == 0) {
            ImGui::SliderInt("Bitrate (Mbps)", &m_recordingBitrate, 5, 100);
        } else {
            ImGui::Combo("ProRes Profile", &m_proresProfile, "Proxy\0LT\0422\0HQ\0");
        }

        ImGui::Separator();

        if (!m_app.GetEncoder().IsRecording()) {
            if (ImGui::Button("Start Recording", ImVec2(-1, 40))) {
                RecordingSettings settings;
                settings.outputPath = m_recordingPath;
                settings.codec = (m_recordingCodec == 0) ? "libx264" : "prores_ks";
                settings.bitrate = m_recordingBitrate * 1000000;
                settings.proresProfile = m_proresProfile;
                m_app.StartRecording(settings);
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Recording in progress...");
            ImGui::Text("Frames: %lld | Dropped: %lld", 
                m_app.GetEncoder().GetFramesEncoded(),
                m_app.GetEncoder().GetFramesDropped());
            ImGui::Text("Encoding FPS: %.1f", m_app.GetEncoder().GetEncodingFPS());
            
            if (ImGui::Button("Stop Recording", ImVec2(-1, 40))) {
                m_app.StopRecording();
            }
        }
    }
    ImGui::End();
}

void UIManager::DrawNotifications() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float yOffset = 10.0f;
    
    for (auto it = m_notifications.begin(); it != m_notifications.end();) {
        it->timeRemaining -= ImGui::GetIO().DeltaTime;
        
        if (it->timeRemaining <= 0.0f) {
            it = m_notifications.erase(it);
            continue;
        }
        
        float alpha = std::min(it->timeRemaining, 1.0f);
        
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 10, viewport->WorkPos.y + yOffset));
        ImGui::SetNextWindowBgAlpha(alpha * 0.8f);
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::Begin(("##notif" + std::to_string(yOffset)).c_str(), nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | 
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", it->message.c_str());
        
        yOffset += ImGui::GetWindowHeight() + 5.0f;
        
        ImGui::End();
        ImGui::PopStyleVar();
        
        ++it;
    }
}

void UIManager::DrawKeybindingModal() {
    ImGui::OpenPopup("Set Keybinding");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Set Keybinding", &m_showKeybindingModal,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        auto* preset = m_app.GetShaderManager().GetPreset(m_keybindingPresetIndex);
        if (!preset) {
            m_showKeybindingModal = false;
            ImGui::EndPopup();
            return;
        }

        // Edge-detection state - reset each time the modal (re)opens
        static bool s_wasOpen     = false;
        static int  s_prevTrigger = 0;
        static bool s_prevEsc     = false;
        static bool s_prevDel     = false;
        if (!s_wasOpen) {
            s_prevTrigger = 0;
            s_prevEsc     = false;
            s_prevDel     = false;
            s_wasOpen     = true;
        }

        ImGui::Text("Setting keybinding for: %s", preset->name.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("Hold modifiers and press a key   |   Esc = cancel   |   Del = clear");
        ImGui::Spacing();

        // --- Live preview ---
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        bool alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;

        int triggerKey = 0;
        for (int k = 0; k < 256; ++k) {
            if (k == VK_CONTROL || k == VK_SHIFT || k == VK_MENU ||
                k == VK_LCONTROL || k == VK_RCONTROL ||
                k == VK_LSHIFT   || k == VK_RSHIFT   ||
                k == VK_LMENU    || k == VK_RMENU)
                continue;
            if (!(GetKeyState(k) & 0x8000)) continue;
            if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9') ||
                (k >= VK_F1 && k <= VK_F12)) {
                triggerKey = k;
                break;
            }
        }

        // Clear stale conflict message when the trigger key changes
        if (triggerKey != s_prevTrigger)
            m_keybindingConflictMsg.clear();

        // Display live preview line
        if (triggerKey != 0 || ctrl || shift || alt) {
            std::string preview;
            if (ctrl)       preview += "Ctrl+";
            if (alt)        preview += "Alt+";
            if (shift)      preview += "Shift+";
            if (triggerKey) preview += m_app.GetKeyName(triggerKey);
            else            preview += "...";

            if (!m_keybindingConflictMsg.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", preview.c_str());
            else
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", preview.c_str());
        } else {
            ImGui::TextDisabled("\xe2\x80\x94");
        }

        if (!m_keybindingConflictMsg.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                               m_keybindingConflictMsg.c_str());
        }

        ImGui::Spacing();

        // --- Key press handling ---
        bool escDown = (GetKeyState(VK_ESCAPE) & 0x8000) != 0;
        bool delDown = (GetKeyState(VK_DELETE) & 0x8000) != 0;

        if (escDown && !s_prevEsc) {
            m_keybindingConflictMsg.clear();
            m_showKeybindingModal = false;
            s_wasOpen = false;
        } else if (delDown && !s_prevDel) {
            preset->shortcutKey       = 0;
            preset->shortcutModifiers = 0;
            m_keybindingConflictMsg.clear();
            m_showKeybindingModal = false;
            m_app.SaveConfig();
            s_wasOpen = false;
        } else if (triggerKey != 0 && triggerKey != s_prevTrigger) {
            int mods = 0;
            if (ctrl)  mods |= MOD_CONTROL;
            if (alt)   mods |= MOD_ALT;
            if (shift) mods |= MOD_SHIFT;

            std::string conflict = m_app.FindBindingConflict(triggerKey, mods,
                                                              m_keybindingPresetIndex, -1);
            if (!conflict.empty()) {
                // Conflict: show warning, do NOT commit
                m_keybindingConflictMsg = conflict + " — choose a different key.";
            } else {
                // No conflict: commit, save, close
                preset->shortcutKey = triggerKey;
                preset->shortcutModifiers = mods;
                m_keybindingConflictMsg.clear();
                m_showKeybindingModal = false;
                m_app.SaveConfig();
                s_wasOpen = false;
            }
        }

        s_prevTrigger = triggerKey;
        s_prevEsc     = escDown;
        s_prevDel     = delDown;

        ImGui::EndPopup();
    }
}
void UIManager::DrawNewShaderModal() {
    ImGui::OpenPopup("New Shader");
    
    if (ImGui::BeginPopupModal("New Shader", &m_showNewShaderModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", m_newShaderName, sizeof(m_newShaderName));
        
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            if (strlen(m_newShaderName) > 0) {
                ShaderPreset preset;
                preset.name = m_newShaderName;
                preset.source = ShaderManager::GetShaderTemplate();
                m_app.GetShaderManager().LoadShaderFromSource(preset.name, preset.source, preset);
                int idx = m_app.GetShaderManager().AddPreset(preset);
                m_app.GetShaderManager().SetActivePreset(idx);
                m_app.OnParamChanged();
                m_editor.SetText(preset.source);
                m_showNewShaderModal = false;
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_showNewShaderModal = false;
        }
        
        ImGui::EndPopup();
    }
}

void UIManager::DrawKeybindingsPanel() {
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Keybindings", &m_showKeybindingsPanel)) {
        auto& manager = m_app.GetShaderManager();
        const int count = manager.GetPresetCount();

        if (count == 0) {
            ImGui::TextDisabled("No shaders loaded.");
        } else if (ImGui::BeginTable("kb_table", 2,
                       ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                       ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Shader", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < count; ++i) {
                const ShaderPreset* preset = manager.GetPreset(i);
                if (!preset) continue;

                ImGui::TableNextRow();
                ImGui::PushID(i);

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(preset->name.c_str());

                ImGui::TableSetColumnIndex(1);
                std::string label;
                if (preset->shortcutKey != 0)
                    label = m_app.GetComboName(preset->shortcutKey, preset->shortcutModifiers);
                else
                    label = "\xe2\x80\x94";  // UTF-8 em dash

                if (ImGui::Selectable(label.c_str(), false,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    m_keybindingPresetIndex = i;
                    m_keybindingConflictMsg.clear();
                    m_showKeybindingModal = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click to set keybinding");

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Click a binding to assign or change it.");
    }
    ImGui::End();
}

bool UIManager::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}

bool UIManager::WantsCaptureMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}

bool UIManager::WantsCaptureKeyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

void UIManager::SetEditorContent(const std::string& content) {
    m_editor.SetText(content);
}

std::string UIManager::GetEditorContent() const {
    return m_editor.GetText();
}

bool UIManager::IsEditorFocused() const {
    // TextEditor has no IsFocused(); WantCaptureKeyboard is true
    // whenever any ImGui widget (including the editor) holds focus.
    return ImGui::GetIO().WantCaptureKeyboard;
}

void UIManager::ShowNotification(const std::string& message, float duration) {
    m_notifications.push_back({message, duration});
}

void UIManager::DrawManageWorkspacesModal() {
    ImGui::OpenPopup("Manage Workspaces");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500.0f, 360.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Manage Workspaces", &m_showManageWorkspacesModal)) {
        auto& wm = m_app.GetWorkspaceManager();
        const auto& presets = wm.GetPresets();

        ImGui::TextDisabled("Right-click a workspace to rename, delete, or set a keybinding.");
        ImGui::Spacing();

        if (ImGui::BeginTable("##wspresets", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY, ImVec2(0, 260.0f)))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Keybinding", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
                const auto& wp = presets[i];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                std::string rowLabel = wp.name;
                if (i == 0) rowLabel += " (built-in)";
                // Use Selectable spanning both columns for context menu
                ImGui::Selectable(rowLabel.c_str(), false,
                    ImGuiSelectableFlags_SpanAllColumns);

                if (i > 0) {
                    if (ImGui::BeginPopupContextItem(("##wsctx" + std::to_string(i)).c_str())) {
                        if (ImGui::MenuItem("Set Keybinding...")) {
                            m_workspaceKeybindingIndex = i;
                            m_workspaceKeybindingConflictMsg.clear();
                            m_showWorkspaceKeybindingModal = true;
                        }
                        if (ImGui::MenuItem("Delete")) {
                            wm.DeletePreset(i);
                            ImGui::EndPopup();
                            break;  // vector modified; exit loop safely
                        }
                        ImGui::EndPopup();
                    }
                }

                ImGui::TableSetColumnIndex(1);
                if (wp.shortcutKey != 0)
                    ImGui::Text("%s", m_app.GetComboName(wp.shortcutKey, wp.shortcutModifiers).c_str());
                else
                    ImGui::TextDisabled("\xe2\x80\x94");  // UTF-8 em dash
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(100, 0))) {
            m_showManageWorkspacesModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void UIManager::DrawWorkspaceKeybindingModal() {
    ImGui::OpenPopup("Set Workspace Keybinding");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(340.0f, 200.0f), ImGuiCond_Appearing);

    static bool s_wasOpen    = false;
    static int  s_prevTrigger = 0;
    static bool s_prevEsc    = false;
    static bool s_prevDel    = false;

    bool open = true;
    if (ImGui::BeginPopupModal("Set Workspace Keybinding", &open)) {
        if (!s_wasOpen) { s_prevTrigger = 0; s_wasOpen = true; }

        auto& wm = m_app.GetWorkspaceManager();
        if (m_workspaceKeybindingIndex < 0 ||
            m_workspaceKeybindingIndex >= wm.GetPresetCount())
        {
            m_showWorkspaceKeybindingModal = false;
            s_wasOpen = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const WorkspacePreset& wp = wm.GetPresets()[m_workspaceKeybindingIndex];
        ImGui::Text("Workspace: %s", wp.name.c_str());
        ImGui::TextDisabled("Press a key combination. Delete = clear. Escape = cancel.");
        ImGui::Spacing();

        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        bool alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;

        // Scan for a non-modifier trigger key (alphanumeric + F1-F12)
        int triggerKey = 0;
        for (int vk = 0x30; vk <= 0x5A; ++vk) {
            if (GetKeyState(vk) & 0x8000) { triggerKey = vk; break; }
        }
        if (triggerKey == 0) {
            for (int vk = VK_F1; vk <= VK_F12; ++vk) {
                if (GetKeyState(vk) & 0x8000) { triggerKey = vk; break; }
            }
        }

        // Clear stale conflict message when the trigger key changes
        if (triggerKey != s_prevTrigger)
            m_workspaceKeybindingConflictMsg.clear();

        // Preview line
        if (triggerKey != 0 || ctrl || shift || alt) {
            std::string preview;
            if (ctrl)       preview += "Ctrl+";
            if (alt)        preview += "Alt+";
            if (shift)      preview += "Shift+";
            if (triggerKey) preview += m_app.GetKeyName(triggerKey);
            else            preview += "...";

            if (!m_workspaceKeybindingConflictMsg.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", preview.c_str());
            else
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", preview.c_str());
        } else {
            ImGui::TextDisabled("\xe2\x80\x94");
        }

        if (!m_workspaceKeybindingConflictMsg.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                               m_workspaceKeybindingConflictMsg.c_str());
        }

        ImGui::Spacing();
        bool escDown = (GetKeyState(VK_ESCAPE) & 0x8000) != 0;
        bool delDown = (GetKeyState(VK_DELETE) & 0x8000) != 0;

        if (escDown && !s_prevEsc) {
            m_workspaceKeybindingConflictMsg.clear();
            m_showWorkspaceKeybindingModal = false;
            s_wasOpen = false;
            ImGui::CloseCurrentPopup();
        } else if (delDown && !s_prevDel) {
            if (!wm.SetKeybinding(m_workspaceKeybindingIndex, 0, 0)) {
                m_workspaceKeybindingConflictMsg = "Failed to write layout file.";
            } else {
                m_workspaceKeybindingConflictMsg.clear();
                m_showWorkspaceKeybindingModal = false;
                s_wasOpen = false;
                ImGui::CloseCurrentPopup();
            }
        } else if (triggerKey != 0 && triggerKey != s_prevTrigger) {
            int mods = 0;
            if (ctrl)  mods |= MOD_CONTROL;
            if (alt)   mods |= MOD_ALT;
            if (shift) mods |= MOD_SHIFT;

            std::string conflict = m_app.FindBindingConflict(
                triggerKey, mods, -1, m_workspaceKeybindingIndex);
            if (!conflict.empty()) {
                m_workspaceKeybindingConflictMsg = conflict + " — choose a different key.";
            } else {
                if (!wm.SetKeybinding(m_workspaceKeybindingIndex, triggerKey, mods)) {
                    m_workspaceKeybindingConflictMsg = "Failed to write layout file.";
                } else {
                    m_workspaceKeybindingConflictMsg.clear();
                    m_showWorkspaceKeybindingModal = false;
                    s_wasOpen = false;
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        s_prevTrigger = triggerKey;
        s_prevEsc     = escDown;
        s_prevDel     = delDown;

        ImGui::EndPopup();
    }

    if (!open) {
        m_showWorkspaceKeybindingModal = false;
        s_wasOpen = false;
    }
}

} // namespace SP
