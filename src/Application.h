#pragma once

#include "Common.h"
#include "VideoDecoder.h"
#include "D3D11Renderer.h"
#include "ShaderManager.h"
#include "VideoEncoder.h"
#include "UIManager.h"
#include "ConfigManager.h"

namespace SP {

class Application {
public:
    Application();
    ~Application();

    // Non-copyable
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Initialization
    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    void Shutdown();

    // Main loop
    int Run();
    void RequestExit() { m_exitRequested = true; }

    // Video operations
    bool OpenVideo(const std::string& filepath);
    void CloseVideo();
    void OpenVideoDialog();

    // Playback control
    void Play();
    void Pause();
    void Stop();
    void TogglePlayback();
    void SeekTo(double seconds);
    PlaybackState GetPlaybackState() const { return m_playbackState; }

    // Shader operations
    bool CompileCurrentShader(const std::string& source);
    bool SaveCurrentShader(const std::string& source);
    void SaveShaderAsDialog(const std::string& source);

    // Recording
    bool StartRecording(const RecordingSettings& settings);
    void StopRecording();

    // Configuration
    void SaveConfig();
    AppConfig& GetConfig() { return m_configManager.GetConfig(); }
    const AppConfig& GetConfig() const { return m_configManager.GetConfig(); }

    // Component access
    VideoDecoder& GetDecoder() { return m_decoder; }
    D3D11Renderer& GetRenderer() { return m_renderer; }
    ShaderManager& GetShaderManager() { return *m_shaderManager; }
    VideoEncoder& GetEncoder() { return m_encoder; }
    UIManager& GetUI() { return *m_uiManager; }

    // Key name helper
    std::string GetKeyName(int vkCode) const;

private:
    // Window handling
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool CreateMainWindow(HINSTANCE hInstance, int nCmdShow);
    void HandleDroppedFiles(HDROP hDrop);
    void HandleKeyboardShortcuts(UINT vkCode);

    // Frame processing
    void ProcessFrame();
    void RenderFrame();

    // Window
    HWND m_hwnd = nullptr;
    int m_windowWidth = 1280;
    int m_windowHeight = 720;

    // Components
    VideoDecoder m_decoder;
    D3D11Renderer m_renderer;
    std::unique_ptr<ShaderManager> m_shaderManager;
    VideoEncoder m_encoder;
    std::unique_ptr<UIManager> m_uiManager;
    ConfigManager m_configManager;

    // State
    PlaybackState m_playbackState = PlaybackState::Stopped;
    bool m_exitRequested = false;
    VideoFrame m_currentFrame;
    
    // Timing
    std::chrono::steady_clock::time_point m_lastFrameTime;
    double m_frameDuration = 1.0 / 30.0;
    float m_playbackTime = 0.0f;
};

} // namespace SP
