#pragma once

#include "Common.h"
#include "imgui.h"
#include "TextEditor.h"

#include <random>

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

    // Live capture / stream dialog
    void ShowCaptureDialog();

    void ToggleEditor()           { m_showEditor           = !m_showEditor; }
    void ToggleLibrary()          { m_showLibrary          = !m_showLibrary; }
    void ToggleTransport()        { m_showTransport        = !m_showTransport; }
    void ToggleRecording()        { m_showRecording        = !m_showRecording; }
    void ToggleKeybindingsPanel() { m_showKeybindingsPanel = !m_showKeybindingsPanel; }
    void ToggleSpoutPanel()       { m_showSpoutPanel       = !m_showSpoutPanel; }

    // Reset keyframe selection state. Must be called whenever the active shader preset
    // changes so stale param/keyframe indices don't address into the wrong preset's vectors.
    void ResetKeyframeSelection() {
        m_selectedKeyframeParam = -1;
        m_selectedKeyframeIndex = -1;
        m_keyframeFollowMode    = false;
    }

    // False when no imgui.ini existed at startup, i.e. this is a fresh install
    // and the built-in Default workspace should be applied.
    bool HadSavedLayout() const { return m_hadSavedLayout; }

    // Panel visibility as a whole (workspace preset save/load). Every closable
    // dockable panel must appear in both directions — see PanelVisibility.
    PanelVisibility GetVisibility() const {
        PanelVisibility v;
        v.editor      = m_showEditor;
        v.library     = m_showLibrary;
        v.transport   = m_showTransport;
        v.recording   = m_showRecording;
        v.keybindings = m_showKeybindingsPanel;
        v.noise       = m_showNoisePanel;
        v.spout       = m_showSpoutPanel;
        v.audio       = m_showAudioPanel;
        return v;
    }

    void ApplyVisibility(const PanelVisibility& v) {
        m_showEditor           = v.editor;
        m_showLibrary          = v.library;
        m_showTransport        = v.transport;
        m_showRecording        = v.recording;
        m_showKeybindingsPanel = v.keybindings;
        m_showNoisePanel       = v.noise;
        m_showSpoutPanel       = v.spout;
        m_showAudioPanel       = v.audio;
    }

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
    void DrawKeybindingsPanel();
    void DrawShaderParameters();
    void DrawManageWorkspacesModal();
    void DrawWorkspaceKeybindingModal();
    void DrawKeyframeDetail(ShaderParam& param, KeyframeTimeline& timeline,
                            int keyframeIndex, bool& anyChanged);
    void DrawNoisePanel();
    void DrawCaptureDialog();
    void DrawSpoutPanel();
    void DrawAudioPanel();

    // Assigns a random value to `param`, always inside the bounds the UI itself
    // offers for that parameter. Returns false for types with nothing to randomise
    // (Event triggers and read-only AudioBand meters).
    bool RandomiseParam(ShaderParam& param);

    Application& m_app;

    // Source for the parameter randomiser. Seeded once from the system entropy
    // source; fixed size, so it is safe to hold for the lifetime of the UI.
    std::mt19937 m_rng{std::random_device{}()};
    
    // UI state
    bool m_showEditor = true;
    bool m_showLibrary = true;
    bool m_showTransport = true;
    bool m_showRecording = false;
    bool m_showKeybindingModal = false;
    bool m_showNewShaderModal = false;
    bool m_showKeybindingsPanel = false;
    int m_keybindingPresetIndex = -1;
    bool m_keybindingIsPassthrough = false;  // true = modal edits the passthrough binding
    std::string m_keybindingConflictMsg;

    // Workspace preset UI state
    bool m_showManageWorkspacesModal = false;
    bool m_showSaveWorkspacePopup = false;
    char m_saveWorkspaceName[256] = "";
    bool m_showWorkspaceKeybindingModal = false;
    int m_workspaceKeybindingIndex = -1;
    std::string m_workspaceKeybindingConflictMsg;
    
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

    // ImGui ini path — must outlive the ImGui context (io.IniFilename is a raw ptr)
    std::string m_iniFilePath;

    // False when no imgui.ini existed at startup (fresh install / first run)
    bool m_hadSavedLayout = false;

    // Monospace font for the shader editor (Consolas; null = use default)
    ImFont* m_editorFont = nullptr;

    // Noise generator panel
    bool m_showNoisePanel = false;

    // Spout output panel
    bool m_showSpoutPanel = false;

    // Audio monitor panel
    bool m_showAudioPanel = false;

    // Capture / stream dialog
    bool m_showCaptureDialog = false;
    std::vector<std::string> m_captureDevices;
    int m_selectedCaptureIdx = 0;
    char m_captureUrlBuf[512] = "";

    // Keyframe editing state
    int m_selectedKeyframeParam = -1;   // index into preset->params, or -1
    int m_selectedKeyframeIndex = -1;   // index into timeline->keyframes, or -1
    bool m_keyframeFollowMode   = false; // transport seek also moves selected keyframe
};

} // namespace SP
