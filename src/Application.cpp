#include "Application.h"
#include <commdlg.h>
#include <shellapi.h>
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
    for (auto& preset : m_configManager.GetConfig().shaderPresets) {
        if (!preset.filepath.empty()) {
            ShaderPreset loadedPreset;
            if (m_shaderManager->LoadShaderFromFile(preset.filepath, loadedPreset)) {
                loadedPreset.shortcutKey = preset.shortcutKey;
                loadedPreset.shortcutModifiers = preset.shortcutModifiers;
                m_shaderManager->AddPreset(loadedPreset);
            }
        }
    }

    // Scan shader directory
    m_shaderManager->ScanDirectory(m_configManager.GetConfig().shaderDirectory);

    // Create shaders directory if it doesn't exist
    std::filesystem::create_directories(m_configManager.GetConfig().shaderDirectory);

    // Open last video if available
    if (!m_configManager.GetConfig().lastOpenedVideo.empty()) {
        OpenVideo(m_configManager.GetConfig().lastOpenedVideo);
    }

    m_lastFrameTime = std::chrono::steady_clock::now();

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

    case WM_KEYDOWN:
        if (!m_uiManager || !m_uiManager->WantsCaptureKeyboard()) {
            HandleKeyboardShortcuts(static_cast<UINT>(wParam));
        }
        return 0;

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
    case VK_F5:
        if (m_uiManager) {
            CompileCurrentShader(m_uiManager->GetEditorContent());
        }
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
            m_uiManager->SetEditorContent(preset->source);
            m_uiManager->ShowNotification("Switched to: " + preset->name);
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

void Application::RenderFrame() {
    // Upload current video frame
    if (!m_currentFrame.data[0].empty()) {
        m_renderer.UploadVideoFrame(m_currentFrame);
    }

    // Set shader uniforms
    m_renderer.SetShaderTime(m_playbackTime);

    // Begin frame
    m_renderer.BeginFrame();
    m_renderer.EndFrame();

    // Render UI
    m_uiManager->BeginFrame();
    m_uiManager->Render();
    m_uiManager->EndFrame();

    // If recording, capture frame
    if (m_encoder.IsRecording() && !m_currentFrame.data[0].empty()) {
        // Render to texture
        m_renderer.BeginFrame();  // Set up pipeline
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
    auto* preset = m_shaderManager->GetActivePreset();
    if (preset) {
        preset->source = source;
        if (m_shaderManager->CompilePreset(*preset)) {
            m_shaderManager->SetActivePreset(m_shaderManager->GetActivePresetIndex());
            m_uiManager->ShowNotification("Shader compiled successfully");
            return true;
        } else {
            m_uiManager->ShowNotification("Shader compilation failed");
            return false;
        }
    } else {
        // Create new preset from source
        ShaderPreset newPreset;
        newPreset.name = "Untitled";
        newPreset.source = source;
        if (m_shaderManager->CompilePreset(newPreset)) {
            int idx = m_shaderManager->AddPreset(newPreset);
            m_shaderManager->SetActivePreset(idx);
            m_uiManager->ShowNotification("Shader compiled successfully");
            return true;
        } else {
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
            }
            
            m_uiManager->ShowNotification("Shader saved: " + std::string(filepath));
        }
    }
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

} // namespace SP
