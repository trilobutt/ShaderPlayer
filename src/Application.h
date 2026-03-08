#pragma once

#include "Common.h"
#include "VideoDecoder.h"
#include "D3D11Renderer.h"
#include "ShaderManager.h"
#include "VideoEncoder.h"
#include "UIManager.h"
#include "ConfigManager.h"
#include "WorkspaceManager.h"
#include "VideoOutputWindow.h"
#include "SpoutOutput.h"

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

    // Live capture (webcam / RTSP stream)
    bool OpenCapture(const std::string& deviceOrUrl, bool isDshow = true);
    void OpenCaptureDialog();

    // Playback control
    void Play();
    void Pause();
    void Stop();
    void TogglePlayback();
    void SeekTo(double seconds);
    PlaybackState GetPlaybackState() const { return m_playbackState; }
    float GetPlaybackTime() const { return m_playbackTime; }

    // Shader operations
    bool CompileCurrentShader(const std::string& source);
    bool SaveCurrentShader(const std::string& source);
    void SaveShaderAsDialog(const std::string& source);
    void ScanFolderDialog();

    // Recording
    bool StartRecording(const RecordingSettings& settings);
    void StopRecording();
    void OpenRecordingOutputDialog(char* pathBuf, size_t bufSize);

    // Configuration
    void SaveConfig();

    // Video output window — separate OS window for screen-sharing
    void ToggleVideoOutputWindow();
    bool IsVideoOutputWindowOpen() const { return m_videoOutputWindow.IsOpen(); }

    // Spout output — GPU texture sharing with Spout-aware receivers
    void SetSpoutEnabled(bool enabled);
    bool IsSpoutEnabled() const { return m_spoutOutput.IsEnabled(); }
    void SetSpoutSenderName(const std::string& name);

    // Noise generator — regenerates the global t1 noise texture from current config
    void RegenerateNoise();

    // Generative resolution — applies config.generativeWidth/Height to the renderer
    void ApplyGenerativeResolution();
    AppConfig& GetConfig() { return m_configManager.GetConfig(); }
    const AppConfig& GetConfig() const { return m_configManager.GetConfig(); }

    // Component access
    VideoDecoder& GetDecoder() { return m_decoder; }
    D3D11Renderer& GetRenderer() { return m_renderer; }
    ShaderManager& GetShaderManager() { return *m_shaderManager; }
    VideoEncoder& GetEncoder() { return m_encoder; }
    UIManager& GetUI() { return *m_uiManager; }
    WorkspaceManager& GetWorkspaceManager() { return *m_workspaceManager; }

    // Key name helper
    std::string GetKeyName(int vkCode) const;

    // Returns a human-readable combo string, e.g. "Ctrl+Shift+F1"
    std::string GetComboName(int vkCode, int modifiers) const;

    // Returns a human-readable conflict description, or empty string if the binding is free.
    // excludeShaderIdx: shader preset index to skip (-1 = check all)
    // excludeWorkspaceIdx: workspace preset index to skip (-1 = check all)
    std::string FindBindingConflict(int vkCode, int modifiers,
                                     int excludeShaderIdx,
                                     int excludeWorkspaceIdx) const;

    void LoadWorkspacePreset(int index);

    // Called by UIManager after any shader parameter widget changes value.
    void OnParamChanged();

private:
    // Window handling
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool CreateMainWindow(HINSTANCE hInstance, int nCmdShow);
    void HandleDroppedFiles(HDROP hDrop);
    void HandleKeyboardShortcuts(UINT vkCode);
    static void PackParamValues(const ShaderPreset& preset, float out[16]);
    void EvaluateKeyframes();

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
    std::unique_ptr<WorkspaceManager> m_workspaceManager;
    VideoOutputWindow m_videoOutputWindow;
    SpoutOutput m_spoutOutput;

    // State
    PlaybackState m_playbackState = PlaybackState::Stopped;
    bool m_exitRequested = false;
    VideoFrame m_currentFrame;
    
    // Timing
    std::chrono::steady_clock::time_point m_lastFrameTime;
    double m_frameDuration = 1.0 / 30.0;
    float m_playbackTime = 0.0f;
    float m_generativeTime = 0.0f;  // Accumulated wall-clock time for generative shaders
    bool m_eventResetPending = false;
    bool m_newVideoFrame = false;
};

} // namespace SP
