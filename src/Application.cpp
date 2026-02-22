#include "Application.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <fstream>

namespace SP {

Application::Application() = default;

Application::~Application() {
    Shutdown();
}

bool Application::Initialize(HINSTANCE hInstance, int nCmdShow) {
    // Load configuration
    m_configManager.Load(ConfigManager::GetDefaultConfigPath());

    // Create window
    if (!CreateMainWindow(hInstance, nCmdShow)) {
        return false;
    }

    // Initialize D3D11 renderer
    if (!m_renderer.Initialize(m_hwnd, m_windowWidth, m_windowHeight)) {
        MessageBoxA(nullptr, "Failed to initialize D3D11", "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // Create shader manager
    m_shaderManager = std::make_unique<ShaderManager>(m_renderer);
    m_shaderManager->EnableFileWatching(true);

    // Create UI manager
    m_uiManager = std::make_unique<UIManager>(*this);
    if (!m_uiManager->Initialize(m_hwnd, m_renderer.GetDevice(), m_renderer.GetContext())) {
        MessageBoxA(nullptr, "Failed to initialize UI", "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // Load shader presets from config
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

    // Resolve the shader directory: if the configured path doesn't exist, try the
    // directory next to the executable (works for dev builds run from build/Release/).
    {
        auto& shaderDir = m_configManager.GetConfig().shaderDirectory;
        if (!std::filesystem::exists(shaderDir)) {
            char exePath[MAX_PATH];
            GetModuleFileNameA(nullptr, exePath, MAX_PATH);
            auto altDir = std::filesystem::path(exePath).parent_path() / "shaders";
            if (std::filesystem::exists(altDir)) {
                shaderDir = altDir.string();
            }
        }
    }

    // Scan shader directory
    m_shaderManager->ScanDirectory(m_configManager.GetConfig().shaderDirectory);

    // Create shaders directory if it doesn't exist
    std::filesystem::create_directories(m_configManager.GetConfig().shaderDirectory);

    // Initialise workspace manager
    m_workspaceManager = std::make_unique<WorkspaceManager>();
    {
        auto& layoutsDir = m_configManager.GetConfig().layoutsDirectory;
        if (!std::filesystem::path(layoutsDir).is_absolute() &&
            !std::filesystem::exists(layoutsDir)) {
            char exePath[MAX_PATH];
            GetModuleFileNameA(nullptr, exePath, MAX_PATH);
            layoutsDir = (std::filesystem::path(exePath).parent_path() / "layouts").string();
        }
        m_workspaceManager->Initialize(layoutsDir);
    }

    // Open last video if available
    if (!m_configManager.GetConfig().lastOpenedVideo.empty()) {
        OpenVideo(m_configManager.GetConfig().lastOpenedVideo);
    }

    m_lastFrameTime = std::chrono::steady_clock::now();

    // Upload initial param values to GPU if a preset is already active
    OnParamChanged();

    return true;
}

void Application::Shutdown() {
    StopRecording();
    SaveConfig();
    
    m_uiManager.reset();
    m_shaderManager.reset();
    m_renderer.Shutdown();
    m_decoder.Close();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool Application::CreateMainWindow(HINSTANCE hInstance, int nCmdShow) {
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ShaderPlayerWindow";
    RegisterClassExW(&wc);

    // Calculate window size for desired client area
    RECT rc = { 0, 0, m_windowWidth, m_windowHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    // Create window
    m_hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"ShaderPlayerWindow",
        L"Shader Player",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) {
        return false;
    }

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    return true;
}

LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Application* app = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        app = static_cast<Application*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (app) {
        return app->HandleMessage(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Application::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Let ImGui handle messages first
    if (m_uiManager && m_uiManager->HandleMessage(hwnd, msg, wParam, lParam)) {
        return 0;
    }

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && m_renderer.IsInitialized()) {
            m_windowWidth = LOWORD(lParam);
            m_windowHeight = HIWORD(lParam);
            m_renderer.Resize(m_windowWidth, m_windowHeight);
        }
        return 0;

    case WM_DROPFILES:
        HandleDroppedFiles(reinterpret_cast<HDROP>(wParam));
        return 0;

    case WM_KEYDOWN: {
        UINT vk = static_cast<UINT>(wParam);
        // F-keys and modifier-key combos always fire so panel toggles and user
        // shader keybinds work even when the editor has focus.
        bool hasModifier = (GetKeyState(VK_CONTROL) & 0x8000) != 0
                        || (GetKeyState(VK_SHIFT)   & 0x8000) != 0
                        || (GetKeyState(VK_MENU)    & 0x8000) != 0;
        bool alwaysHandle = (vk >= VK_F1 && vk <= VK_F12) || hasModifier;
        if (alwaysHandle || !m_uiManager || !m_uiManager->WantsCaptureKeyboard()) {
            HandleKeyboardShortcuts(vk);
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Application::HandleDroppedFiles(HDROP hDrop) {
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    if (fileCount > 0) {
        wchar_t filepath[MAX_PATH];
        DragQueryFileW(hDrop, 0, filepath, MAX_PATH);
        
        // Convert to UTF-8
        int len = WideCharToMultiByte(CP_UTF8, 0, filepath, -1, nullptr, 0, nullptr, nullptr);
        std::string utf8Path(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, filepath, -1, utf8Path.data(), len, nullptr, nullptr);
        utf8Path.resize(len - 1);  // Remove null terminator

        // Check if it's a shader file
        std::string ext = std::filesystem::path(utf8Path).extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(c));

        if (ext == ".hlsl" || ext == ".fx" || ext == ".ps") {
            // Load as shader
            ShaderPreset preset;
            if (m_shaderManager->LoadShaderFromFile(utf8Path, preset)) {
                int idx = m_shaderManager->AddPreset(preset);
                m_shaderManager->SetActivePreset(idx);
                OnParamChanged();
                m_uiManager->SetEditorContent(preset.source);
                m_uiManager->ShowNotification("Loaded shader: " + preset.name);
            }
        } else {
            // Try to open as video
            OpenVideo(utf8Path);
        }
    }
    DragFinish(hDrop);
}

void Application::HandleKeyboardShortcuts(UINT vkCode) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

    // Global shortcuts
    switch (vkCode) {
    case VK_SPACE:
        TogglePlayback();
        return;
    case VK_ESCAPE:
        m_shaderManager->SetPassthrough();
        return;
    case VK_F1:
        if (m_uiManager) m_uiManager->ToggleEditor();
        return;
    case VK_F2:
        if (m_uiManager) m_uiManager->ToggleLibrary();
        return;
    case VK_F3:
        if (m_uiManager) m_uiManager->ToggleTransport();
        return;
    case VK_F4:
        if (m_uiManager) m_uiManager->ToggleRecording();
        return;
    case VK_F5:
        if (m_uiManager) {
            CompileCurrentShader(m_uiManager->GetEditorContent());
        }
        return;
    case VK_F6:
        m_uiManager->ToggleKeybindingsPanel();
        return;
    case VK_F9:
        if (m_encoder.IsRecording()) {
            StopRecording();
        } else {
            RecordingSettings settings;
            settings.outputPath = "output.mp4";
            StartRecording(settings);
        }
        return;
    case 'O':
        if (ctrl) {
            OpenVideoDialog();
        }
        return;
    case 'S':
        if (ctrl && m_uiManager) {
            SaveCurrentShader(m_uiManager->GetEditorContent());
        }
        return;
    }

    // Check shader keybindings
    for (int i = 0; i < m_shaderManager->GetPresetCount(); ++i) {
        auto* preset = m_shaderManager->GetPreset(i);
        if (!preset || preset->shortcutKey == 0) continue;

        bool modifiersMatch = true;
        if ((preset->shortcutModifiers & MOD_CONTROL) && !ctrl) modifiersMatch = false;
        if ((preset->shortcutModifiers & MOD_SHIFT) && !shift) modifiersMatch = false;
        if ((preset->shortcutModifiers & MOD_ALT) && !alt) modifiersMatch = false;

        if (modifiersMatch && vkCode == static_cast<UINT>(preset->shortcutKey)) {
            m_shaderManager->SetActivePreset(i);
            OnParamChanged();
            m_uiManager->SetEditorContent(preset->source);
            m_uiManager->ShowNotification("Switched to: " + preset->name);
            return;
        }
    }

    // Check workspace preset keybindings (skip index 0 = Default, it has no shortcut)
    for (int i = 1; i < m_workspaceManager->GetPresetCount(); ++i) {
        const WorkspacePreset& wp = m_workspaceManager->GetPresets()[i];
        if (wp.shortcutKey == 0) continue;

        bool modifiersMatch = true;
        if ((wp.shortcutModifiers & MOD_CONTROL) && !ctrl) modifiersMatch = false;
        if ((wp.shortcutModifiers & MOD_SHIFT)   && !shift) modifiersMatch = false;
        if ((wp.shortcutModifiers & MOD_ALT)     && !alt)   modifiersMatch = false;

        if (modifiersMatch && vkCode == static_cast<UINT>(wp.shortcutKey)) {
            LoadWorkspacePreset(i);
            return;
        }
    }
}

int Application::Run() {
    MSG msg = {};

    while (!m_exitRequested) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_exitRequested = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (m_exitRequested) break;

        ProcessFrame();
        RenderFrame();
    }

    return static_cast<int>(msg.wParam);
}

void Application::ProcessFrame() {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_lastFrameTime).count();

    // Check for shader file changes
    m_shaderManager->CheckForChanges();

    // Process video frame if playing
    if (m_playbackState == PlaybackState::Playing && m_decoder.IsOpen()) {
        if (elapsed >= m_frameDuration) {
            if (m_decoder.DecodeNextFrame(m_currentFrame)) {
                m_playbackTime = static_cast<float>(m_currentFrame.timestamp);
            } else {
                // End of video, loop
                m_decoder.SeekToTime(0.0);
            }
            m_lastFrameTime = now;
        }
    }
}

/*static*/ void Application::PackParamValues(const ShaderPreset& preset, float out[16]) {
    std::fill(out, out + 16, 0.0f);
    for (const auto& p : preset.params) {
        const int off = p.cbufferOffset;
        switch (p.type) {
        case ShaderParamType::Float:
        case ShaderParamType::Bool:
        case ShaderParamType::Long:
        case ShaderParamType::Event:
            if (off < 16)       out[off] = p.values[0];
            break;
        case ShaderParamType::Point2D:
            if (off + 1 < 16) { out[off] = p.values[0]; out[off + 1] = p.values[1]; }
            break;
        case ShaderParamType::Color:
            if (off + 3 < 16) {
                out[off] = p.values[0]; out[off + 1] = p.values[1];
                out[off + 2] = p.values[2]; out[off + 3] = p.values[3];
            }
            break;
        }
    }
}

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

void Application::RenderFrame() {
    // Upload current video frame
    if (!m_currentFrame.data[0].empty()) {
        m_renderer.UploadVideoFrame(m_currentFrame);
    }

    // Set shader uniforms
    m_renderer.SetShaderTime(m_playbackTime);

    // Set up D3D11 pipeline and clear backbuffer to black
    m_renderer.BeginFrame();
    // Render video+shader to the display texture; ImGui::Image picks it up from there
    m_renderer.RenderToDisplay();

    // Render UI
    m_uiManager->BeginFrame();
    m_uiManager->Render();
    m_uiManager->EndFrame();

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

    // If recording, capture frame
    if (m_encoder.IsRecording() && !m_currentFrame.data[0].empty()) {
        // Pipeline state is already configured from the BeginFrame() call above.
        // RenderToTexture() sets its own render target and viewport, so no second
        // BeginFrame() is needed (and calling it would clear the visible backbuffer).
        if (m_renderer.RenderToTexture()) {
            std::vector<uint8_t> frameData;
            int width, height;
            if (m_renderer.CopyRenderTargetToStaging(frameData, width, height)) {
                m_encoder.SubmitFrame(frameData, width, height);
            }
        }
    }

    // Present
    m_renderer.Present(true);
}

bool Application::OpenVideo(const std::string& filepath) {
    if (!m_decoder.Open(filepath)) {
        m_uiManager->ShowNotification("Failed to open video: " + filepath);
        return false;
    }

    m_frameDuration = 1.0 / m_decoder.GetFPS();
    m_configManager.GetConfig().lastOpenedVideo = filepath;
    
    // Decode first frame
    m_decoder.DecodeNextFrame(m_currentFrame);
    m_playbackState = PlaybackState::Paused;

    m_uiManager->ShowNotification("Opened: " + std::filesystem::path(filepath).filename().string());
    return true;
}

void Application::CloseVideo() {
    Stop();
    m_decoder.Close();
    m_currentFrame = VideoFrame{};
}

void Application::OpenVideoDialog() {
    char filepath[MAX_PATH] = {};
    
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = "Video Files\0*.mp4;*.mov;*.avi;*.mkv;*.webm;*.mxf\0All Files\0*.*\0";
    ofn.lpstrFile = filepath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        OpenVideo(filepath);
    }
}

void Application::Play() {
    if (m_decoder.IsOpen()) {
        m_playbackState = PlaybackState::Playing;
        m_lastFrameTime = std::chrono::steady_clock::now();
    }
}

void Application::Pause() {
    m_playbackState = PlaybackState::Paused;
}

void Application::Stop() {
    m_playbackState = PlaybackState::Stopped;
    if (m_decoder.IsOpen()) {
        m_decoder.SeekToTime(0.0);
        m_decoder.DecodeNextFrame(m_currentFrame);
    }
    m_playbackTime = 0.0f;
}

void Application::TogglePlayback() {
    if (m_playbackState == PlaybackState::Playing) {
        Pause();
    } else {
        Play();
    }
}

void Application::SeekTo(double seconds) {
    if (m_decoder.IsOpen()) {
        m_decoder.SeekToTime(seconds);
        m_decoder.DecodeNextFrame(m_currentFrame);
        m_playbackTime = static_cast<float>(seconds);
    }
}

bool Application::CompileCurrentShader(const std::string& source) {
    int activeIndex = m_shaderManager->GetActivePresetIndex();
    auto* preset = m_shaderManager->GetActivePreset();
    if (preset) {
        // Update source in the stored preset, then recompile via index so the
        // compiled ID3D11PixelShader is reliably written into m_compiledShaders.
        preset->source = source;
        if (m_shaderManager->RecompilePreset(activeIndex)) {
            m_shaderManager->SetActivePreset(activeIndex);
            OnParamChanged();
            m_uiManager->ShowNotification("Shader compiled successfully");
            return true;
        } else {
            m_uiManager->ShowNotification("Shader compilation failed: " +
                (preset->compileError.empty() ? "unknown error" : preset->compileError.substr(0, 80)));
            return false;
        }
    } else {
        // No active preset — compile the editor content into a new preset.
        ShaderPreset newPreset;
        newPreset.name = "Untitled";
        newPreset.source = source;
        // AddPreset compiles and stores the shader; no double-compile needed.
        int idx = m_shaderManager->AddPreset(newPreset);
        auto* added = m_shaderManager->GetPreset(idx);
        if (added && added->isValid) {
            m_shaderManager->SetActivePreset(idx);
            OnParamChanged();
            m_uiManager->ShowNotification("Shader compiled successfully");
            return true;
        } else {
            m_shaderManager->RemovePreset(idx);
            m_uiManager->ShowNotification("Shader compilation failed");
            return false;
        }
    }
}

bool Application::SaveCurrentShader(const std::string& source) {
    auto* preset = m_shaderManager->GetActivePreset();
    if (!preset) {
        SaveShaderAsDialog(source);
        return true;
    }

    preset->source = source;

    if (!preset->filepath.empty()) {
        std::ofstream file(preset->filepath);
        if (file.is_open()) {
            file << source;
            m_uiManager->ShowNotification("Shader saved: " + preset->name);
            return true;
        }
    }

    SaveShaderAsDialog(source);
    return true;
}

void Application::SaveShaderAsDialog(const std::string& source) {
    char filepath[MAX_PATH] = {};
    
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = "HLSL Shader\0*.hlsl\0All Files\0*.*\0";
    ofn.lpstrFile = filepath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "hlsl";
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameA(&ofn)) {
        std::ofstream file(filepath);
        if (file.is_open()) {
            file << source;
            
            // Update or create preset
            auto* preset = m_shaderManager->GetActivePreset();
            if (preset) {
                preset->filepath = filepath;
                preset->name = std::filesystem::path(filepath).stem().string();
            } else {
                ShaderPreset newPreset;
                newPreset.filepath = filepath;
                newPreset.name = std::filesystem::path(filepath).stem().string();
                newPreset.source = source;
                m_shaderManager->CompilePreset(newPreset);
                int idx = m_shaderManager->AddPreset(newPreset);
                m_shaderManager->SetActivePreset(idx);
                OnParamChanged();
            }
            
            m_uiManager->ShowNotification("Shader saved: " + std::string(filepath));
        }
    }
}

void Application::ScanFolderDialog() {
    IFileOpenDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pDialog));
    if (FAILED(hr)) return;

    DWORD opts = 0;
    pDialog->GetOptions(&opts);
    pDialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
    pDialog->SetTitle(L"Select Shader Folder");

    hr = pDialog->Show(m_hwnd);
    if (SUCCEEDED(hr)) {
        IShellItem* pItem = nullptr;
        hr = pDialog->GetResult(&pItem);
        if (SUCCEEDED(hr)) {
            PWSTR pszPath = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                int len = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, nullptr, 0, nullptr, nullptr);
                std::string path(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, path.data(), len, nullptr, nullptr);

                m_configManager.GetConfig().shaderDirectory = path;
                m_shaderManager->ScanDirectory(path);
                m_uiManager->ShowNotification("Scanned: " + std::filesystem::path(path).filename().string());
                CoTaskMemFree(pszPath);
            }
            pItem->Release();
        }
    }
    pDialog->Release();
}

bool Application::StartRecording(const RecordingSettings& settings) {
    if (!m_decoder.IsOpen()) {
        m_uiManager->ShowNotification("No video loaded");
        return false;
    }

    if (m_encoder.StartRecording(settings, m_decoder.GetWidth(), m_decoder.GetHeight(), m_decoder.GetFPS())) {
        m_uiManager->ShowNotification("Recording started: " + settings.outputPath);
        return true;
    } else {
        m_uiManager->ShowNotification("Failed to start recording");
        return false;
    }
}

void Application::StopRecording() {
    if (m_encoder.IsRecording()) {
        m_encoder.StopRecording();
        m_uiManager->ShowNotification("Recording stopped");
    }
}

void Application::SaveConfig() {
    // Update shader presets in config
    auto& config = m_configManager.GetConfig();
    config.shaderPresets.clear();
    
    for (int i = 0; i < m_shaderManager->GetPresetCount(); ++i) {
        auto* preset = m_shaderManager->GetPreset(i);
        if (preset && !preset->filepath.empty()) {
            config.shaderPresets.push_back(*preset);
        }
    }

    m_configManager.Save(ConfigManager::GetDefaultConfigPath());
}

std::string Application::GetKeyName(int vkCode) const {
    if (vkCode >= 'A' && vkCode <= 'Z') {
        return std::string(1, static_cast<char>(vkCode));
    }
    if (vkCode >= '0' && vkCode <= '9') {
        return std::string(1, static_cast<char>(vkCode));
    }
    if (vkCode >= VK_F1 && vkCode <= VK_F12) {
        return "F" + std::to_string(vkCode - VK_F1 + 1);
    }
    return "Key" + std::to_string(vkCode);
}


std::string Application::GetComboName(int vkCode, int modifiers) const {
    std::string result;
    if (modifiers & MOD_CONTROL) result += "Ctrl+";
    if (modifiers & MOD_ALT)     result += "Alt+";
    if (modifiers & MOD_SHIFT)   result += "Shift+";
    result += GetKeyName(vkCode);
    return result;
}

void Application::LoadWorkspacePreset(int index) {
    bool se, sl, st, sr, sk;
    if (m_workspaceManager->LoadPreset(index, se, sl, st, sr, sk)) {
        m_uiManager->ApplyVisibility(se, sl, st, sr, sk);
        const std::string& name = m_workspaceManager->GetPresets()[index].name;
        m_uiManager->ShowNotification("Workspace: " + name);
    }
}

std::string Application::FindBindingConflict(int vkCode, int modifiers,
                                              int excludeShaderIdx,
                                              int excludeWorkspaceIdx) const
{
    if (vkCode == 0) return {};

    // Hardcoded unmodified keys
    if (modifiers == 0) {
        switch (vkCode) {
        case VK_SPACE:  return "reserved for Play/Pause (Space)";
        case VK_ESCAPE: return "reserved for Reset to Passthrough (Escape)";
        case VK_F1:     return "reserved for Toggle Editor (F1)";
        case VK_F2:     return "reserved for Toggle Library (F2)";
        case VK_F3:     return "reserved for Toggle Transport (F3)";
        case VK_F4:     return "reserved for Toggle Recording (F4)";
        case VK_F5:     return "reserved for Compile (F5)";
        case VK_F6:     return "reserved for Toggle Keybindings (F6)";
        case VK_F9:     return "reserved for Start/Stop Recording (F9)";
        }
    }
    // Hardcoded Ctrl combos
    if (modifiers == MOD_CONTROL) {
        if (vkCode == 'O') return "reserved for Open Video (Ctrl+O)";
        if (vkCode == 'S') return "reserved for Save Shader (Ctrl+S)";
        if (vkCode == 'N') return "reserved for New Shader (Ctrl+N)";
    }

    // Shader presets
    const auto& shaderPresets = m_shaderManager->GetPresets();
    for (int i = 0; i < static_cast<int>(shaderPresets.size()); ++i) {
        if (i == excludeShaderIdx) continue;
        const ShaderPreset& p = shaderPresets[i];
        if (p.shortcutKey == 0) continue;
        if (p.shortcutKey == vkCode && p.shortcutModifiers == modifiers)
            return "conflicts with shader \"" + p.name + "\"";
    }

    // Workspace presets
    if (m_workspaceManager) {
        const auto& wps = m_workspaceManager->GetPresets();
        for (int i = 0; i < static_cast<int>(wps.size()); ++i) {
            if (i == excludeWorkspaceIdx) continue;
            if (wps[i].shortcutKey == 0) continue;
            if (wps[i].shortcutKey == vkCode && wps[i].shortcutModifiers == modifiers)
                return "conflicts with workspace \"" + wps[i].name + "\"";
        }
    }

    return {};
}

} // namespace SP
