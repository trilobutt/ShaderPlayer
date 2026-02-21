#include "UIManager.h"
#include "Application.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

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
            if (ImGui::BeginPopupContextItem()) {
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
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;
    
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 windowPos(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, 
                     viewport->WorkPos.y + viewport->WorkSize.y - 60);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    
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

        // Recording indicator
        if (m_app.GetEncoder().IsRecording()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), " [REC]");
            ImGui::SameLine();
            ImGui::Text("%lld frames", m_app.GetEncoder().GetFramesEncoded());
        }
    }
    ImGui::End();
}

void UIManager::DrawRecordingPanel() {
    if (ImGui::Begin("Recording Settings", &m_showRecording)) {
        ImGui::InputText("Output Path", m_recordingPath, sizeof(m_recordingPath));
        
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

            int conflict = m_app.IsBindingConflict(triggerKey, mods, m_keybindingPresetIndex);
            if (conflict >= 0) {
                // Conflict: show warning, do NOT commit
                const ShaderPreset* other = m_app.GetShaderManager().GetPreset(conflict);
                std::string otherName = other ? other->name : "another shader";
                m_keybindingConflictMsg = "Already bound to \"" + otherName +
                                          "\" â choose a different key.";
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

} // namespace SP
