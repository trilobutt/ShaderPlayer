#pragma once

#include "Common.h"
#include "imgui.h"
#include "TextEditor.h"

namespace SP {

class Application;

class UIManager {
public:
    UIManager(Application& app);
    ~UIManager();

    // Non-copyable
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    // Initialization
    bool Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // Frame rendering
    void BeginFrame();
    void EndFrame();
    void Render();

    // Event handling
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // UI state
    bool WantsCaptureMouse() const;
    bool WantsCaptureKeyboard() const;

    // Shader editor
    void SetEditorContent(const std::string& content);
    std::string GetEditorContent() const;
    bool IsEditorFocused() const;

    // Notifications
    void ShowNotification(const std::string& message, float duration = 3.0f);

private:
    void DrawMenuBar();
    void DrawVideoViewport();
    void DrawShaderEditor();
    void DrawShaderLibrary();
    void DrawTransportControls();
    void DrawRecordingPanel();
    void DrawNotifications();
    void DrawKeybindingModal();
    void DrawNewShaderModal();

    Application& m_app;
    
    // UI state
    bool m_showEditor = true;
    bool m_showLibrary = true;
    bool m_showTransport = true;
    bool m_showRecording = false;
    bool m_showKeybindingModal = false;
    bool m_showNewShaderModal = false;
    int m_keybindingPresetIndex = -1;
    std::string m_keybindingConflictMsg;
    
    // Editor
    TextEditor m_editor;
    bool m_editorNeedsCompile = false;
    float m_compileTimer = 0.0f;
    
    // Layout sizes
    float m_editorWidth = 500.0f;
    float m_libraryHeight = 200.0f;
    
    // Notifications
    struct Notification {
        std::string message;
        float timeRemaining;
    };
    std::vector<Notification> m_notifications;
    
    // Recording UI state
    char m_recordingPath[512] = "output.mp4";
    int m_recordingCodec = 0;  // 0 = H.264, 1 = ProRes
    int m_recordingBitrate = 20;  // Mbps
    int m_proresProfile = 2;
    
    // New shader modal
    char m_newShaderName[256] = "";
};

} // namespace SP
