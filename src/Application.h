#pragma once

#include "Common.h"
#include "AudioAnalyzer.h"
#include "AudioPlayer.h"
#include "VideoDecoder.h"
#include "D3D11Renderer.h"
#include "ShaderManager.h"
#include "VideoEncoder.h"
#include "ConfigManager.h"
#include "WorkspaceManager.h"
#include "VideoOutputWindow.h"
#include "SpoutOutput.h"

namespace SP {

class MainWindow;

class Application {
public:
    Application();
    ~Application();

    // Non-copyable
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Initialization and per-frame entry points. WinMain constructs the QApplication,
    // calls InitializeQt() (which builds MainWindow — a QWidget, so the QApplication
    // must already exist), and drives TickOnce() from a zero-interval QTimer.
    bool InitializeQt();
    void Shutdown();
    // What the unhandled-exception filter is allowed to do. Called from the crash handler
    // in main.cpp, off the normal Shutdown() path — see the definition for why it does
    // only this much.
    void CrashCleanup();
    // ProcessFrame(); RenderFrame(); returns whether the viewport actually presented a
    // frame this tick — WinMain's QTimer uses it to decide its own next interval, since
    // a vsync Present already paces the common case and a zero interval must not be
    // added on top of it.
    bool TickOnce();
    MainWindow* GetMainWindow() const;

    // Ends the event loop. Reached from File > Exit and from MainWindow::closeEvent.
    void RequestExit();

    // Video operations
    bool OpenVideo(const std::string& filepath);
    void CloseVideo();
    void OpenVideoDialog();

    // Live capture (webcam / RTSP stream). The dialog that chooses the device belongs
    // to the window (MainWindow::OnOpenCapture); this is only the open itself.
    bool OpenCapture(const std::string& deviceOrUrl, bool isDshow = true);

    // Playback control
    void Play();
    void Pause();
    void Stop();
    void TogglePlayback();
    void SeekTo(double seconds);
    PlaybackState GetPlaybackState() const { return m_playbackState; }
    float GetPlaybackTime() const { return m_playbackTime; }

    // Shader operations
    bool CompileCurrentShader(const std::string& source, bool quiet = false);
    bool SaveCurrentShader(const std::string& source);
    void SaveShaderAsDialog(const std::string& source);
    void ScanFolderDialog();

    // Recording
    bool StartRecording(const RecordingSettings& settings);
    void StopRecording();
    // True while a recording is walking an opened video file from its first frame to its
    // last, one frame per tick, and will stop itself at the end. False for a generative
    // render (which has no end of its own to reach and is stopped by hand) and for live
    // capture. The transport is inert while it is true (see Stop/SeekTo/TogglePlayback).
    bool IsOfflineRender() const { return m_offlineRender; }
    // True while a generative recording is stepping the shader clock one output frame per
    // tick. The preview is deliberately not real-time while this is set, so the surfaces
    // that would otherwise imply it is say so instead.
    bool IsGenerativeRender() const { return m_generativeRender; }
    // Returns the chosen path, or an empty string if the user cancelled.
    std::string OpenRecordingOutputDialog(const std::string& currentPath);

    // Configuration
    void SaveConfig();

    // Video output window — separate OS window for screen-sharing
    void ToggleVideoOutputWindow();
    bool IsVideoOutputWindowOpen() const { return m_videoOutputWindow.IsOpen(); }

    // Spout output — GPU texture sharing with Spout-aware receivers
    void SetSpoutEnabled(bool enabled);
    bool IsSpoutEnabled()  const { return m_spoutOutput.IsEnabled(); }
    bool IsSpoutActive()   const { return m_spoutOutput.IsActive(); }
    std::string GetSpoutActiveSenderName() const { return m_spoutOutput.GetActiveSenderName(); }
    void SetSpoutSenderName(const std::string& name);

    // Noise generator — regenerates the global t1 noise texture from current config
    void RegenerateNoise();

    // Audio analysis settings (beat sensitivity, smoothing, etc.)
    void UpdateAudioSettings();
    const AudioData& GetAudioData() const { return m_audioData; }

    // Audio playback volume / mute — persisted to config.json
    void SetAudioVolume(float vol);
    void SetAudioMute(bool mute);

    // Generative resolution — applies config.generativeWidth/Height to the renderer
    void ApplyGenerativeResolution();
    AppConfig& GetConfig() { return m_configManager.GetConfig(); }
    const AppConfig& GetConfig() const { return m_configManager.GetConfig(); }

    // Component access
    VideoDecoder& GetDecoder() { return m_decoder; }
    D3D11Renderer& GetRenderer() { return m_renderer; }
    ShaderManager& GetShaderManager() { return *m_shaderManager; }
    VideoEncoder& GetEncoder() { return m_encoder; }
    WorkspaceManager& GetWorkspaceManager() { return *m_workspaceManager; }

    // Key name helper
    std::string GetKeyName(int vkCode) const;

    // Returns a human-readable combo string, e.g. "Ctrl+Shift+F1"
    std::string GetComboName(int vkCode, int modifiers) const;

    // Returns a human-readable conflict description, or empty string if the binding is free.
    // excludeShaderIdx: shader preset index to skip (-1 = check all)
    // excludeWorkspaceIdx: workspace preset index to skip (-1 = check all)
    // excludePassthrough: skip the passthrough binding (use when editing it)
    std::string FindBindingConflict(int vkCode, int modifiers,
                                     int excludeShaderIdx,
                                     int excludeWorkspaceIdx,
                                     bool excludePassthrough = false) const;

    // Restores a workspace preset's dock layout and panel visibility immediately.
    // QMainWindow::restoreState() is atomic and safe to call from anywhere, so unlike
    // the shell it replaces this needs no deferral to a between-frames queue.
    void LoadWorkspacePreset(int index);

    // Called after any shader parameter widget changes value.
    void OnParamChanged();

    // The single dispatch for every keyboard shortcut in the product, expressed in
    // Win32 VK codes because that is what every stored binding is. Invoked from
    // MainWindow::keyPressEvent, which maps the Qt key first (see ui/KeyMap.h).
    void HandleKeyboardShortcuts(UINT vkCode);

private:
    void EvaluateKeyframes();

    // End the file render started by StartRecording: closes the encoder, rewinds, and
    // leaves the transport stopped. Called at end of stream and by StopRecording.
    void FinishOfflineRender(bool completed);

    // Hand the decoder's recording tap to the encoder. Called every tick of a render and
    // once more as it ends, so the audio the last video frame did not reach still lands
    // in the file.
    void PumpRecordingAudio();

    // Frame processing
    void ProcessFrame();
    bool RenderFrame();      // returns whether the viewport presented (see TickOnce)

    // Components
    AudioAnalyzer m_audioAnalyzer;
    AudioPlayer   m_audioPlayer;
    AudioData     m_audioData;
    VideoDecoder  m_decoder;
    D3D11Renderer m_renderer;
    std::unique_ptr<ShaderManager> m_shaderManager;
    VideoEncoder m_encoder;
    // The Qt shell window. Forward-declared above so Application.h doesn't pull in Qt
    // headers. Shutdown() resets it explicitly, and does so before m_renderer.Shutdown():
    // the viewport inside it owns a swap chain on the renderer's device, and that swap
    // chain has to go before the device it was created from.
    std::unique_ptr<MainWindow> m_mainWindow;
    ConfigManager m_configManager;
    std::unique_ptr<WorkspaceManager> m_workspaceManager;
    VideoOutputWindow m_videoOutputWindow;
    SpoutOutput m_spoutOutput;

    // State
    PlaybackState m_playbackState = PlaybackState::Stopped;
    bool m_exitRequested = false;
    bool m_shutdownDone = false;
    VideoFrame m_currentFrame;
    
    // Timing
    std::chrono::steady_clock::time_point m_lastFrameTime;
    double m_frameDuration = 1.0 / 30.0;
    float m_playbackTime = 0.0f;
    float m_generativeTime = 0.0f;  // Accumulated wall-clock time for generative shaders
    bool m_eventResetPending = false;
    bool m_newVideoFrame = false;
    // Set wherever m_currentFrame is written, cleared by the upload itself. Distinct from
    // m_newVideoFrame, which ProcessFrame resets every tick: a frame decoded by OpenVideo,
    // Stop or SeekTo arrives between ticks and would otherwise never reach the GPU.
    bool m_videoUploadPending = false;

    // Offline render (recording an opened file). m_offlineAudioFed counts the analyser
    // samples handed over so far, so the audio fed per frame is derived from the video's
    // own timestamp rather than accumulated per tick, which drifts.
    bool m_offlineRender = false;
    int64_t m_offlineAudioFed = 0;

    // Generative render: no decoder, so the shader itself is the source and its clock is
    // stepped one output frame per tick instead of by the wall clock. m_renderFrameStep is
    // 1/fps in seconds, fixed for the life of the recording.
    bool m_generativeRender = false;
    float m_renderFrameStep = 0.0f;
};

} // namespace SP
