#pragma once

// MainWindow.h — the Qt shell: menu bar, eight region docks, and the central stack
// holding the D3D viewport over its empty state.
//
// The refresh contract is the whole difference from the outgoing immediate-mode UI and is
// fixed here so the panels of A6-A12 are written against the right update model:
//
//   Tick()            runs every frame and touches only the viewport and the live meters.
//   RefreshLibrary()  called when the preset list or the active preset changes.
//   RefreshParameters() called when the active preset's parameters change.
//   RefreshAll()      both of the above, plus the editor document.
//
// A panel that rebuilds itself from Tick() would fight the user's cursor and burn CPU on an
// idle window, so nothing but the meters is allowed on that path.

#include "Common.h"

#include <QColor>
#include <QDockWidget>
#include <QMainWindow>
#include <QString>

QT_BEGIN_NAMESPACE
class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QKeyEvent;
class QFrame;
class QLabel;
class QMenu;
class QStackedWidget;
class QVBoxLayout;
QT_END_NAMESPACE

namespace SP {

class Application;
class AudioPanel;
class EditorPanel;
class LibraryPanel;
class NoisePanel;
class ParamsPanel;
class RecordingPanel;
class SpoutPanel;
class ToastStack;
class TransportPanel;
class ViewportWidget;

// A dock that carries one region's identity. One hue, on two channels that are always
// there and never move: the tinted glyph in the title bar and the hairline above the
// panel's body. Both are readable without a pointer and both cost nothing per frame.
class RegionDock : public QDockWidget {
    Q_OBJECT
public:
    RegionDock(const QString& title,
               const QString& objectName,
               const QString& iconName,
               const QColor& hue,
               QWidget* parent);

    // Installs the panel A6-A12 builds inside the floating island, taking ownership.
    // Replaces whatever body is already there (the placeholder, on the first call).
    void SetBody(QWidget* body);

    QColor Hue() const { return m_hue; }

private:
    QColor m_hue;
    QFrame* m_frame = nullptr;              // QFrame#PanelFrame, the floating island
    QFrame* m_hairline = nullptr;           // QFrame#PanelHairline, in the region hue
    QLabel* m_title = nullptr;              // QLabel#PanelTitle, in the title bar
    QVBoxLayout* m_frameLayout = nullptr;
    QWidget* m_body = nullptr;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Application& app);

    ViewportWidget* Viewport() const { return m_viewport; }

    // Once per frame from Application: present the viewport and move the live meters.
    // Returns whether the viewport actually presented (the video/generative page is
    // showing) — WinMain's timer uses this to pace itself when nothing is presenting.
    bool Tick();

    // Writes the current geometry and dock state into AppConfig. Called by closeEvent and
    // by Application::RequestExit, which is the route File > Exit takes.
    void SaveWindowState();

    void ShowToast(const QString& msg, int ms = 3000);

    PanelVisibility GetVisibility() const;
    void ApplyVisibility(const PanelVisibility& v);

    // Reveal the Noise Generator dock: shown if it was closed, and raised to the front of
    // whatever tab group it shares. The parameters panel asks for this when the active
    // shader samples the global noise texture; the dock's contents are A12's.
    void ShowNoiseDock();

    void RefreshLibrary();
    void RefreshParameters();
    void RefreshAll();

    // What the shader document currently says. File > Save Shader and Ctrl+S both read
    // it, so the editor is the single answer to "what is the current shader text".
    QString EditorSource() const;

    // Compile what the editor holds and show the result in its status line. Shader >
    // Compile and F5 are the same action, so they are the same call.
    void CompileShader();

    // Application::HandleKeyboardShortcuts owns every binding in the product and reaches
    // the window through these; they are what F1-F4, F6 and F8 actually do.
    void ToggleEditorDock();
    void ToggleLibraryDock();
    void ToggleTransportDock();
    void ToggleRecordingDock();
    void ToggleSpoutDock();
    void ShowKeybindingsReference();

    // F9. Presses the Recording dock's own toggle, so the recording uses the settings the
    // user configured there rather than a second set held somewhere else.
    void ToggleRecording();

    // Ctrl+N. Prompts for a new shader's name and template, same as Shader > New.
    void OnNewShader();

    // restoreState() from WorkspaceManager, or ArrangeDefaultLayout(). Reached from the
    // Workspace Presets menu and from a workspace preset's own keybinding.
    void LoadWorkspacePreset(int index);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;      // .hlsl/.fx/.ps -> shader, else video
    void closeEvent(QCloseEvent* event) override;

    // Maps the Qt key to a VK code and hands it to Application, which owns the dispatch.
    void keyPressEvent(QKeyEvent* event) override;

private:
    void BuildCentral();
    void BuildDocks();
    void BuildMenus();
    void RebuildWorkspaceMenu();
    void ArrangeDefaultLayout();

    RegionDock* MakeDock(const QString& title,
                         const QString& objectName,
                         const QString& iconName,
                         const QColor& hue);

    void OnManageWorkspaces();
    void OnOpenCapture();

    void OnSaveWorkspaceAs();               // prompts for a name, saveState() into WorkspaceManager

    Application& m_app;

    // Central stack: index 0 the D3D viewport, index 1 the empty state.
    QStackedWidget* m_stack = nullptr;
    ViewportWidget* m_viewport = nullptr;
    QWidget* m_emptyState = nullptr;

    RegionDock* m_dockLibrary = nullptr;
    RegionDock* m_dockEditor = nullptr;
    RegionDock* m_dockParams = nullptr;
    RegionDock* m_dockTransport = nullptr;
    RegionDock* m_dockRecording = nullptr;
    RegionDock* m_dockNoise = nullptr;
    RegionDock* m_dockSpout = nullptr;
    RegionDock* m_dockAudio = nullptr;

    // Owns the shader document. File > Save Shader, Save Shader As and Shader > Compile
    // all read EditorPanel::Source(), so the editor is the single answer to "what is the
    // current shader text" rather than the active preset's stored copy.
    EditorPanel* m_editor = nullptr;

    // The preset list. RefreshLibrary is the only thing that rebuilds it; its own
    // PresetActivated signal is the only thing that comes back.
    LibraryPanel* m_library = nullptr;

    // The active preset's parameters. RefreshParameters rebuilds its rows; Tick moves its
    // audio meters and follows the values a keyframe timeline is driving.
    ParamsPanel* m_params = nullptr;

    // The playhead, the clock and keyframe follow mode. Tick moves its scrubber; the
    // keyframe selection it draws and edits belongs to m_params, which it is kept in step
    // with in both directions.
    TransportPanel* m_transport = nullptr;

    // The recording settings, the armed state and the toggle. The Recording menu and F9 go
    // through it rather than starting the encoder themselves, so there is one path in.
    RecordingPanel* m_recording = nullptr;

    // The global t1 noise texture: its scale, its size, and a preview read back off the
    // texture the shaders are actually sampling. Nothing here moves on its own, so it takes
    // no part in Tick.
    NoisePanel* m_noise = nullptr;

    // Whether the picture is leaving the machine, and under what name. Tick polls it: the
    // sender only registers once a frame has been sent, and nothing signals that.
    SpoutPanel* m_spout = nullptr;

    // The band meters and the 256-bin spectrum. The one panel in the window whose whole
    // content is live measurement, so Tick repaints it every frame it is visible for.
    AudioPanel* m_audio = nullptr;

    QAction* m_actCloseVideo = nullptr;
    QAction* m_actSaveShader = nullptr;
    QAction* m_actSaveShaderAs = nullptr;
    QAction* m_actCompile = nullptr;
    QAction* m_actVideoOutputWindow = nullptr;
    QAction* m_actRecordToggle = nullptr;
    QAction* m_actRecordingSettings = nullptr;
    QMenu* m_workspaceMenu = nullptr;

    // Settings the Recording menu records with. RecordingPanel is handed a reference and is
    // their only editor; the defaults match the outgoing UIManager fields exactly.
    RecordingSettings m_recordingSettings;

    // The transient-notice surface, anchored to the central area. It keeps itself positioned
    // (see Toast.h), so nothing on this class's event path has to.
    ToastStack* m_toasts = nullptr;
};

} // namespace SP
