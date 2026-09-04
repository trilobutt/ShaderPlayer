#include "ui/MainWindow.h"

#include "Application.h"
#include "FrameProfiler.h"
#include "ShaderManager.h"
#include "VideoDecoder.h"
#include "VideoEncoder.h"
#include "WorkspaceManager.h"
#include "ui/AudioPanel.h"
#include "ui/EditorPanel.h"
#include "ui/KeyMap.h"
#include "ui/KeyframeDetail.h"
#include "ui/LibraryPanel.h"
#include "ui/NoisePanel.h"
#include "ui/ParamsPanel.h"
#include "ui/RecordingPanel.h"
#include "ui/SpoutPanel.h"
#include "ui/Theme.h"
#include "ui/Toast.h"
#include "ui/TransportPanel.h"
#include "ui/ViewportWidget.h"
#include "ui/dialogs/CaptureDialog.h"
#include "ui/dialogs/KeybindingsReferenceDialog.h"
#include "ui/dialogs/NewShaderDialog.h"
#include "ui/dialogs/WorkspacesDialog.h"

#include <algorithm>

#include <QApplication>
#include <QCloseEvent>
#include <QFont>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace SP {

namespace {

// Alpha of a region hairline. It is now the whole of that channel rather than the bottom
// of a hover ramp, so it has to carry the hue on its own: below about 120 the accents
// desaturate against the canvas and the greens, blues and ambers all read as one muddy
// brown-grey. High enough to be unmistakably its colour, short of the full-strength line
// that eight docks at once would turn into noise.
constexpr int kHairlineAlpha = 168;

// Passed to QMainWindow::saveState()/restoreState() so a layout saved under a stale dock
// set (a panel added, removed or renamed) is refused rather than half-applied. Bump this
// whenever the eight dock object names or the set of docks changes.
constexpr int kWorkspaceStateVersion = 1;

// A translucent token has nothing behind it in a top-level window, so it is composited
// over the canvas by hand. Same reason QMenu and QToolTip carry flat colours in the qss.
QColor Composite(const QColor& over, const QColor& base)
{
    const qreal a = over.alphaF();
    return QColor::fromRgbF(over.redF()   * a + base.redF()   * (1.0 - a),
                            over.greenF() * a + base.greenF() * (1.0 - a),
                            over.blueF()  * a + base.blueF()  * (1.0 - a));
}

QString Rgba(const QColor& c)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

// Whether the currently focused widget is a QLineEdit or a spin box (QAbstractSpinBox
// sets its internal QLineEdit as its focus proxy, so this one check also covers every
// spin box in ParamsPanel/TransportPanel/RecordingPanel/SpoutPanel). Generalises past
// EditorPanel::IsFocused(), which only answers the editor's own case: the Shader
// Library's filter box has exactly the same Escape-leaks-out problem and there is no
// reason to give every panel with a text field a matching IsFocused() of its own when
// Qt already exposes the one widget that has focus. The editor itself is still asked
// directly (see keyPressEvent) since EditorPanel::IsFocused() already answers it and a
// second, generic check of the same widget would be redundant. The dialogs' own text
// fields (NewShaderDialog, CaptureDialog, KeybindingDialog, WorkspacesDialog) never
// reach here regardless: each is a modal QDialog with its own top-level window, so Qt
// delivers their key events directly to the dialog and MainWindow::keyPressEvent is
// never invoked while one is open.
bool IsOtherTextFieldFocused()
{
    return qobject_cast<QLineEdit*>(QApplication::focusWidget()) != nullptr;
}

// QMenu right-aligns whatever follows a tab in an action's text. The accelerators are
// shown that way rather than set as real QShortcuts because Application::
// HandleKeyboardShortcuts owns every key in the product (keyPressEvent routes into it);
// a QShortcut here would fire the same action twice.
QString MenuText(const QString& label, const QString& accel)
{
    return accel.isEmpty() ? label : label + QLatin1Char('\t') + accel;
}

// The empty state's ground: the canvas running toward the anchor, with a wide accent
// bloom behind the buttons. No grey fill anywhere — the light is colour and text.
class GlowPane : public QWidget {
public:
    explicit GlowPane(QWidget* parent) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QLinearGradient ground(rect().topLeft(), rect().bottomRight());
        ground.setColorAt(0.0, Theme::kCanvas);
        ground.setColorAt(1.0, Theme::kAnchor);
        painter.fillRect(rect(), ground);

        const QPointF centre(width() * 0.5, height() * 0.56);
        const qreal radius = std::max(width(), height()) * 0.62;
        QRadialGradient bloom(centre, radius);
        QColor accent = Theme::kAccentPrimary;
        accent.setAlpha(58);  bloom.setColorAt(0.00, accent);
        accent.setAlpha(26);  bloom.setColorAt(0.42, accent);
        accent.setAlpha(0);   bloom.setColorAt(1.00, accent);
        painter.fillRect(rect(), bloom);
    }
};

}  // namespace

// =======================================================================================
// RegionDock
// =======================================================================================

RegionDock::RegionDock(const QString& title,
                       const QString& objectName,
                       const QString& iconName,
                       const QColor& hue,
                       QWidget* parent)
    : QDockWidget(title, parent), m_hue(hue)
{
    setObjectName(objectName);
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);

    const QIcon icon = Theme::RegionIcon(iconName, hue);
    setWindowIcon(icon);   // the tab and the floated window carry the tinted glyph too

    // ---- title bar -------------------------------------------------------------------
    // Replacing the stock title bar is what puts the region's glyph and its hue on the
    // panel's name. The labels do not accept mouse events, so the press still reaches the
    // QDockWidget underneath and the dock drags from its title exactly as before.
    auto* bar = new QWidget(this);
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(Theme::kSpaceUnit + 4, 6, 4, 2);
    barLayout->setSpacing(Theme::kSpaceUnit);

    auto* glyph = new QLabel(bar);
    glyph->setPixmap(icon.pixmap(16, 16));
    glyph->setFixedSize(16, 16);
    barLayout->addWidget(glyph);

    m_title = new QLabel(title, bar);
    m_title->setObjectName(QStringLiteral("PanelTitle"));
    barLayout->addWidget(m_title);
    barLayout->addStretch(1);

    auto* closeButton = new QToolButton(bar);
    closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    closeButton->setIconSize(QSize(14, 14));
    closeButton->setToolTip(tr("Close %1").arg(title));
    connect(closeButton, &QToolButton::clicked, this, &QWidget::close);
    connect(this, &QDockWidget::featuresChanged, closeButton, [this, closeButton] {
        closeButton->setVisible(features().testFlag(QDockWidget::DockWidgetClosable));
    });
    barLayout->addWidget(closeButton);

    setTitleBarWidget(bar);

    // ---- island ----------------------------------------------------------------------
    m_frame = new QFrame(this);
    m_frame->setObjectName(QStringLiteral("PanelFrame"));
    m_frameLayout = new QVBoxLayout(m_frame);
    m_frameLayout->setContentsMargins(Theme::kSpaceUnit + 4, Theme::kSpaceUnit + 2,
                                      Theme::kSpaceUnit + 4, Theme::kSpaceUnit + 4);
    m_frameLayout->setSpacing(Theme::kSpaceUnit);

    m_hairline = new QFrame(m_frame);
    m_hairline->setObjectName(QStringLiteral("PanelHairline"));
    m_frameLayout->addWidget(m_hairline);

    // Set once. The region's hue reads here and on the tinted glyph in the title bar,
    // and neither moves again: a hover channel that re-parses a stylesheet sixty times a
    // second is not worth what it shows.
    QColor line = m_hue;
    line.setAlpha(kHairlineAlpha);
    m_hairline->setStyleSheet(
        QStringLiteral("QFrame#PanelHairline { background: %1; border: none;"
                       " border-radius: 1px; min-height: 2px; max-height: 2px; }")
            .arg(Rgba(line)));

    // Placeholder until A6-A12 hand over the real panel.
    m_body = new QWidget(m_frame);
    m_body->setMinimumHeight(Theme::kSpaceUnit * 6);
    m_frameLayout->addWidget(m_body, 1);

    QDockWidget::setWidget(m_frame);
}

void RegionDock::SetBody(QWidget* body)
{
    if (!body || body == m_body) return;
    m_frameLayout->replaceWidget(m_body, body);
    delete m_body;
    m_body = body;
    m_frameLayout->setStretchFactor(m_body, 1);
}

// =======================================================================================
// MainWindow
// =======================================================================================

MainWindow::MainWindow(Application& app)
    : m_app(app)
{
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QStringLiteral("ShaderPlayer"));
    setAcceptDrops(true);
    setDockNestingEnabled(true);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    resize(1600, 950);
    setMinimumSize(1024, 640);

    // Seeded from AppConfig::recordingDefaults, which RecordingPanel writes back on every
    // edit. Only the path needs a fallback: an empty one is what a config written before
    // the panel ever ran holds, and FFmpeg cannot guess a container from it.
    m_recordingSettings = m_app.GetConfig().recordingDefaults;
    if (m_recordingSettings.outputPath.empty())
        m_recordingSettings.outputPath = "output.mp4";

    QMainWindow::DockOptions options = QMainWindow::AllowNestedDocks
                                     | QMainWindow::AllowTabbedDocks
                                     | QMainWindow::GroupedDragging
                                     | QMainWindow::AnimatedDocks;
    setDockOptions(options);

    BuildCentral();
    BuildDocks();
    BuildMenus();

    // windowState is written only by closeEvent(), alongside windowGeometry, so a non-empty
    // value here means a previous session closed cleanly under this config; empty means no
    // session ever has, i.e. first run. restoreGeometry()/restoreState() are both atomic —
    // on a version mismatch or corrupt blob they leave the current layout untouched and
    // return false — so falling through to the built-in Default afterwards is a deliberate
    // re-arrange rather than a repair of something half-applied. restoreGeometry() itself
    // clips a saved geometry that no longer fits any connected screen back onto one, so a
    // monitor unplugged or a resolution changed since the last run cannot leave the window
    // off-screen.
    const AppConfig& cfg = m_app.GetConfig();
    bool restored = false;
    if (!cfg.windowState.empty()) {
        restoreGeometry(QByteArray::fromBase64(QByteArray::fromStdString(cfg.windowGeometry)));
        restored = restoreState(
            QByteArray::fromBase64(QByteArray::fromStdString(cfg.windowState)),
            kWorkspaceStateVersion);
    } else {
        QByteArray state;
        PanelVisibility panels;
        if (m_app.GetWorkspaceManager().LoadPreset(0, state, panels)) {
            restored = !state.isEmpty() && restoreState(state, kWorkspaceStateVersion);
            if (restored) ApplyVisibility(panels);
        }
    }
    if (!restored) {
        ArrangeDefaultLayout();
    }

    // Anchored to the central area rather than to the window: a notice belongs over the
    // picture, never over the transport below it or the library beside it. The stack watches
    // both this window and that widget for geometry changes itself.
    m_toasts = new ToastStack(this, m_stack);
}

void MainWindow::BuildCentral()
{
    m_stack = new QStackedWidget(this);

    m_viewport = new ViewportWidget(m_app, m_stack);

    // ---- empty state -----------------------------------------------------------------
    // The first screen a new user sees, so it carries the atmosphere rather than a bare
    // pair of buttons on black.
    auto* pane = new GlowPane(m_stack);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(Theme::kPanelGap * 2, Theme::kPanelGap,
                               Theme::kPanelGap * 2, Theme::kPanelGap);
    layout->setSpacing(0);
    layout->addStretch(3);

    auto* heading = new QLabel(QStringLiteral("ShaderPlayer"), pane);
    QFont headingFont(QString::fromLatin1(Theme::kFontUi));
    headingFont.setPointSize(38);
    headingFont.setBold(true);
    headingFont.setLetterSpacing(QFont::AbsoluteSpacing, -1.0);
    heading->setFont(headingFont);
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);
    layout->addSpacing(Theme::kPanelGap + Theme::kSpaceUnit);

    auto* openVideo = new QPushButton(tr("Open Video..."), pane);
    openVideo->setObjectName(QStringLiteral("Primary"));
    openVideo->setMinimumSize(280, 46);
    openVideo->setCursor(Qt::PointingHandCursor);
    connect(openVideo, &QPushButton::clicked, this, [this] { m_app.OpenVideoDialog(); });
    layout->addWidget(openVideo, 0, Qt::AlignHCenter);
    layout->addSpacing(Theme::kSpaceUnit + 2);

    auto* openCapture = new QPushButton(tr("Open Stream / Webcam..."), pane);
    openCapture->setMinimumSize(280, 46);
    openCapture->setCursor(Qt::PointingHandCursor);
    connect(openCapture, &QPushButton::clicked, this, &MainWindow::OnOpenCapture);
    layout->addWidget(openCapture, 0, Qt::AlignHCenter);
    layout->addSpacing(Theme::kSpaceUnit * 2);

    auto* hint = new QLabel(tr("or drag & drop a video file"), pane);
    hint->setObjectName(QStringLiteral("Caption"));
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);
    layout->addStretch(4);

    m_emptyState = pane;

    m_stack->addWidget(m_viewport);     // index 0
    m_stack->addWidget(m_emptyState);   // index 1
    m_stack->setCurrentIndex(1);
    setCentralWidget(m_stack);
}

RegionDock* MainWindow::MakeDock(const QString& title,
                                 const QString& objectName,
                                 const QString& iconName,
                                 const QColor& hue)
{
    // The object name is load-bearing: QMainWindow::saveState keys the layout off it
    // (A15), so renaming one silently invalidates every saved workspace.
    return new RegionDock(title, objectName, iconName, hue, this);
}

void MainWindow::BuildDocks()
{
    m_dockLibrary   = MakeDock(tr("Shader Library"),     QStringLiteral("dockLibrary"),
                               QStringLiteral("library"),   Theme::kRegionLibrary);
    m_dockEditor    = MakeDock(tr("Shader Editor"),      QStringLiteral("dockEditor"),
                               QStringLiteral("editor"),    Theme::kRegionEditor);
    m_dockParams    = MakeDock(tr("Shader Parameters"),  QStringLiteral("dockParams"),
                               QStringLiteral("params"),    Theme::kRegionParams);
    m_dockTransport = MakeDock(tr("Transport"),          QStringLiteral("dockTransport"),
                               QStringLiteral("transport"), Theme::kRegionTransport);
    m_dockRecording = MakeDock(tr("Recording"),          QStringLiteral("dockRecording"),
                               QStringLiteral("recording"), Theme::kRegionRecording);
    m_dockNoise     = MakeDock(tr("Noise Generator"),    QStringLiteral("dockNoise"),
                               QStringLiteral("noise"),     Theme::kRegionNoise);
    m_dockSpout     = MakeDock(tr("Spout Output"),       QStringLiteral("dockSpout"),
                               QStringLiteral("spout"),     Theme::kRegionSpout);
    m_dockAudio     = MakeDock(tr("Audio Monitor"),      QStringLiteral("dockAudio"),
                               QStringLiteral("audio"),     Theme::kRegionAudio);

    // Shader Parameters is always submitted: it carries no PanelVisibility field and no
    // workspace preset records it, so a user who closed it would have no way back.
    m_dockParams->setFeatures(QDockWidget::DockWidgetMovable
                              | QDockWidget::DockWidgetFloatable);

    m_editor = new EditorPanel(m_app, m_dockEditor);
    m_dockEditor->SetBody(m_editor);

    m_params = new ParamsPanel(m_app, m_dockParams);
    m_dockParams->SetBody(m_params);
    connect(m_params, &ParamsPanel::NoiseGeneratorRequested,
            this, &MainWindow::ShowNoiseDock);

    m_transport = new TransportPanel(m_app, m_dockTransport);
    m_dockTransport->SetBody(m_transport);

    // The keyframe selection lives in ParamsPanel and is drawn in two places. Both
    // directions are wired: the panel tells the transport which diamonds to draw, and the
    // transport asks the panel to move the selection when a diamond is clicked or a
    // keyframe is carried by a scrub.
    connect(m_params, &ParamsPanel::KeyframeSelectionChanged,
            m_transport, &TransportPanel::SetKeyframeSelection);

    const auto followSelection = [this](int paramIndex, int keyframeIndex) {
        m_params->SetKeyframeSelection(paramIndex, keyframeIndex);

        // A keyframe carried to a new time keeps its index whenever it crosses no
        // neighbour, and SetKeyframeSelection has nothing to push in that case. The chips
        // are labelled with the time, so the track it came from is refreshed either way.
        if (QWidget* host = m_params->KeyframeDetailHost(paramIndex)) {
            if (auto* detail = host->findChild<KeyframeDetail*>()) detail->Refresh();
        }
    };
    connect(m_transport, &TransportPanel::KeyframeMoved, this, followSelection);
    connect(m_transport, &TransportPanel::KeyframeActivated, this, followSelection);

    m_recording = new RecordingPanel(m_app, m_recordingSettings, m_dockRecording);
    m_dockRecording->SetBody(m_recording);

    // The default layout tabs this dock behind the Transport, where a pulsing border is
    // invisible. A recording that has just started is exactly the thing that must not be
    // hidden, so arming it brings the dock forward.
    connect(m_recording, &RecordingPanel::RecordingArmed, this, [this] {
        m_dockRecording->show();
        m_dockRecording->raise();
    });

    m_noise = new NoisePanel(m_app, m_dockNoise);
    m_dockNoise->SetBody(m_noise);

    m_spout = new SpoutPanel(m_app, m_dockSpout);
    m_dockSpout->SetBody(m_spout);

    m_audio = new AudioPanel(m_app, m_dockAudio);
    m_dockAudio->SetBody(m_audio);

    m_library = new LibraryPanel(m_app, m_dockLibrary);
    m_dockLibrary->SetBody(m_library);
    connect(m_library, &LibraryPanel::PresetActivated, this, [this] {
        // The two follow-ups every SetActivePreset call site owes (see CLAUDE.md), and
        // then the document: picking a shader in the library is what opens it in the
        // editor, exactly as the outgoing library did.
        RefreshParameters();
        m_app.OnParamChanged();

        const ShaderPreset* preset = m_app.GetShaderManager().GetActivePreset();
        m_editor->SetSource(preset ? QString::fromStdString(preset->source) : QString());
        if (preset) {
            m_editor->ShowCompileResult(preset->isValid,
                                        QString::fromStdString(preset->compileError));
        }
    });
    // A removal shifts every preset after it, and ParamsPanel and KeyframeDetail cache a
    // ShaderPreset* into that vector. Re-seating it is all this needs: the active shader
    // has not changed, so the editor document is deliberately left alone.
    connect(m_library, &LibraryPanel::PresetsChanged, this, &MainWindow::RefreshParameters);
}

void MainWindow::BuildMenus()
{
    // ---- File ------------------------------------------------------------------------
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    QAction* openVideo = fileMenu->addAction(
        MenuText(tr("&Open Video..."), QStringLiteral("Ctrl+O")));
    connect(openVideo, &QAction::triggered, this, [this] { m_app.OpenVideoDialog(); });

    QAction* openCapture = fileMenu->addAction(tr("Open &Stream / Webcam..."));
    connect(openCapture, &QAction::triggered, this, &MainWindow::OnOpenCapture);

    m_actCloseVideo = fileMenu->addAction(tr("&Close Video"));
    connect(m_actCloseVideo, &QAction::triggered, this, [this] { m_app.CloseVideo(); });

    fileMenu->addSeparator();

    m_actSaveShader = fileMenu->addAction(
        MenuText(tr("&Save Shader"), QStringLiteral("Ctrl+S")));
    connect(m_actSaveShader, &QAction::triggered, this, [this] {
        m_app.SaveCurrentShader(m_editor->Source().toStdString());
    });

    m_actSaveShaderAs = fileMenu->addAction(tr("Save Shader &As..."));
    connect(m_actSaveShaderAs, &QAction::triggered, this, [this] {
        m_app.SaveShaderAsDialog(m_editor->Source().toStdString());
    });

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction(
        MenuText(tr("E&xit"), QStringLiteral("Alt+F4")));
    connect(exitAction, &QAction::triggered, this, [this] { m_app.RequestExit(); });

    connect(fileMenu, &QMenu::aboutToShow, this, [this] {
        m_actCloseVideo->setEnabled(m_app.GetDecoder().IsOpen());
    });

    // ---- View ------------------------------------------------------------------------
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

    auto addDockItem = [&](RegionDock* dock, const QString& label, const QString& accel) {
        QAction* action = dock->toggleViewAction();
        action->setText(MenuText(label, accel));
        action->setIcon(dock->windowIcon());
        viewMenu->addAction(action);
    };
    addDockItem(m_dockEditor,    tr("Shader &Editor"),      QStringLiteral("F1"));
    addDockItem(m_dockLibrary,   tr("Shader &Library"),     QStringLiteral("F2"));
    addDockItem(m_dockTransport, tr("&Transport Controls"), QStringLiteral("F3"));
    addDockItem(m_dockRecording, tr("&Recording Panel"),    QStringLiteral("F4"));

    QAction* keybindings = viewMenu->addAction(
        MenuText(tr("&Keybindings..."), QStringLiteral("F6")));
    connect(keybindings, &QAction::triggered, this, &MainWindow::ShowKeybindingsReference);

    addDockItem(m_dockNoise, tr("&Noise Generator"), QString());
    addDockItem(m_dockSpout, tr("&Spout Output"),    QStringLiteral("F8"));
    addDockItem(m_dockAudio, tr("&Audio Monitor"),   QString());

    viewMenu->addSeparator();

    m_actVideoOutputWindow = viewMenu->addAction(
        MenuText(tr("&Video Output Window"), QStringLiteral("F7")));
    m_actVideoOutputWindow->setCheckable(true);
    connect(m_actVideoOutputWindow, &QAction::triggered, this,
            [this] { m_app.ToggleVideoOutputWindow(); });

    viewMenu->addSeparator();

    m_workspaceMenu = viewMenu->addMenu(tr("&Workspace Presets"));
    connect(m_workspaceMenu, &QMenu::aboutToShow, this, &MainWindow::RebuildWorkspaceMenu);

    connect(viewMenu, &QMenu::aboutToShow, this, [this] {
        m_actVideoOutputWindow->setChecked(m_app.IsVideoOutputWindowOpen());
    });

    // ---- Shader ----------------------------------------------------------------------
    QMenu* shaderMenu = menuBar()->addMenu(tr("&Shader"));

    QAction* newShader = shaderMenu->addAction(
        MenuText(tr("&New Shader..."), QStringLiteral("Ctrl+N")));
    connect(newShader, &QAction::triggered, this, &MainWindow::OnNewShader);

    m_actCompile = shaderMenu->addAction(
        MenuText(tr("&Compile"), QStringLiteral("F5")));
    connect(m_actCompile, &QAction::triggered, this, &MainWindow::CompileShader);

    shaderMenu->addSeparator();

    QAction* passthrough = shaderMenu->addAction(
        MenuText(tr("Reset to &Passthrough"), QStringLiteral("Escape")));
    connect(passthrough, &QAction::triggered, this, [this] {
        m_app.GetShaderManager().SetPassthrough();
        RefreshLibrary();
        RefreshParameters();
    });

    // ---- Recording -------------------------------------------------------------------
    QMenu* recordingMenu = menuBar()->addMenu(tr("&Recording"));

    m_actRecordToggle = recordingMenu->addAction(
        MenuText(tr("Start &Recording"), QStringLiteral("F9")));
    connect(m_actRecordToggle, &QAction::triggered, this, &MainWindow::ToggleRecording);

    recordingMenu->addSeparator();

    m_actRecordingSettings = recordingMenu->addAction(
        MenuText(tr("Recording &Settings..."), QStringLiteral("F4")));
    m_actRecordingSettings->setCheckable(true);
    m_actRecordingSettings->setIcon(m_dockRecording->windowIcon());
    connect(m_actRecordingSettings, &QAction::toggled, this,
            [this](bool on) { m_dockRecording->setVisible(on); });

    connect(recordingMenu, &QMenu::aboutToShow, this, [this] {
        const bool recording = m_app.GetEncoder().IsRecording();
        m_actRecordToggle->setText(MenuText(recording ? tr("Stop &Recording")
                                                      : tr("Start &Recording"),
                                            QStringLiteral("F9")));
        QSignalBlocker block(m_actRecordingSettings);
        m_actRecordingSettings->setChecked(!m_dockRecording->isHidden());
    });
}

void MainWindow::RebuildWorkspaceMenu()
{
    m_workspaceMenu->clear();

    QAction* saveAs = m_workspaceMenu->addAction(tr("&Save Current As..."));
    connect(saveAs, &QAction::triggered, this, &MainWindow::OnSaveWorkspaceAs);

    m_workspaceMenu->addSeparator();

    const std::vector<WorkspacePreset>& presets = m_app.GetWorkspaceManager().GetPresets();
    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
        const WorkspacePreset& preset = presets[static_cast<size_t>(i)];
        QString label = QString::fromStdString(preset.name);
        if (preset.shortcutKey != 0) {
            label += QLatin1Char('\t')
                   + QString::fromStdString(m_app.GetComboName(preset.shortcutKey,
                                                               preset.shortcutModifiers));
        }
        QAction* action = m_workspaceMenu->addAction(label);
        connect(action, &QAction::triggered, this,
                [this, i] { LoadWorkspacePreset(i); });
    }

    m_workspaceMenu->addSeparator();

    QAction* manage = m_workspaceMenu->addAction(tr("&Manage Workspaces..."));
    connect(manage, &QAction::triggered, this, &MainWindow::OnManageWorkspaces);
}

void MainWindow::ArrangeDefaultLayout()
{
    // A18 captures the real default from a hand-arranged window; this is enough to start
    // docked rather than as a pile of floating panels.
    addDockWidget(Qt::LeftDockWidgetArea,   m_dockParams);
    addDockWidget(Qt::RightDockWidgetArea,  m_dockLibrary);
    splitDockWidget(m_dockLibrary, m_dockEditor, Qt::Vertical);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockTransport);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockRecording);
    tabifyDockWidget(m_dockTransport, m_dockRecording);
    tabifyDockWidget(m_dockRecording, m_dockNoise);
    tabifyDockWidget(m_dockNoise,     m_dockSpout);
    tabifyDockWidget(m_dockSpout,     m_dockAudio);
    m_dockTransport->raise();

    resizeDocks({m_dockParams, m_dockLibrary}, {340, 400}, Qt::Horizontal);
    resizeDocks({m_dockTransport}, {200}, Qt::Vertical);

    ApplyVisibility(PanelVisibility{});   // the shipped defaults, from Common.h
}

// ---------------------------------------------------------------------------------------
// Frame tick and refresh contract
// ---------------------------------------------------------------------------------------

bool MainWindow::Tick()
{
    SP_PROFILE(kMainWindowTick);
    // Which page: anything at all to draw, or the empty state. Any active shader has a
    // picture, not only a generative one — an audio visualiser has its idle form and a
    // video effect has whatever it makes of an unbound t0 (black, until footage opens).
    // The empty state is for passthrough with no video, which is the only case where the
    // viewport genuinely has nothing in it.
    const ShaderPreset* active = m_app.GetShaderManager().GetActivePreset();
    const bool havePicture = m_app.GetDecoder().IsOpen() || active != nullptr;
    const int page = havePicture ? 0 : 1;
    if (m_stack->currentIndex() != page) m_stack->setCurrentIndex(page);

    const bool presented = (page == 0) && m_viewport->RenderAndPresent();

    // The live meters attach here and nowhere else: A10's transport clock and scrubber,
    // A12's audio bars. Nothing that a user can be mid-edit in may join them.
    //
    // The parameters panel qualifies on both counts: its AudioBand rows are meters, and a
    // keyframe timeline rewrites values every frame during playback. It follows those
    // without rebuilding, so the user's cursor is left alone.
    m_params->Tick();

    // The playhead, the clock, and which page of the track is showing. Live state only:
    // nothing here rebuilds a widget the user could be mid-gesture in.
    m_transport->Tick();

    // The elapsed clock and the encoder counters, and the armed state itself: F9 and the
    // menu can arm the encoder without the panel hearing about it, so the panel watches.
    m_recording->Tick();

    // The Spout sender registers itself on the first frame it shares, which no signal
    // announces, so its status is polled. A compare against the last state, and nothing at
    // all while the dock is hidden.
    m_spout->Tick();

    // The band meters and the spectrum: the one surface here whose content is a live
    // measurement, and the only one for which continuous repaint is the correct behaviour.
    // It also returns immediately while hidden, so a tabbed-away monitor costs nothing.
    m_audio->Tick();

    return presented;
}

void MainWindow::RefreshLibrary()
{
    m_library->Refresh();
}

void MainWindow::RefreshParameters()
{
    // The panel rebuilds its rows from the active preset's params, and clears the keyframe
    // selection A9 edits whenever the shader shown has changed. nullptr is passthrough and
    // the panel has a page for it.
    m_params->SetPreset(m_app.GetShaderManager().GetActivePreset());

    // The editor highlights and completes the ISF parameter names, because they reach the
    // shader as #define aliases the file itself never declares.
    QStringList names;
    if (const ShaderPreset* preset = m_app.GetShaderManager().GetActivePreset()) {
        names.reserve(static_cast<qsizetype>(preset->params.size()));
        for (const ShaderParam& param : preset->params) {
            names << QString::fromStdString(param.name);
        }
    }
    m_editor->SetParamNames(names);
}

void MainWindow::RefreshAll()
{
    RefreshLibrary();
    RefreshParameters();

    const ShaderPreset* preset = m_app.GetShaderManager().GetActivePreset();
    m_editor->SetSource(preset ? QString::fromStdString(preset->source) : QString());
    if (preset) {
        // SetSource leaves the status neutral, which is the right answer with no preset.
        // With one, the panel should open showing where that shader actually stands.
        m_editor->ShowCompileResult(preset->isValid,
                                    QString::fromStdString(preset->compileError));
    }
}

// ---------------------------------------------------------------------------------------
// The shader document, and what the keyboard reaches
// ---------------------------------------------------------------------------------------

QString MainWindow::EditorSource() const
{
    return m_editor->Source();
}

void MainWindow::CompileShader()
{
    const bool ok = m_app.CompileCurrentShader(m_editor->Source().toStdString());
    const ShaderPreset* preset = m_app.GetShaderManager().GetActivePreset();
    m_editor->ShowCompileResult(
        ok, preset ? QString::fromStdString(preset->compileError) : QString());
}

// toggleViewAction rather than setVisible: it is the same QAction the View menu shows, so
// its checkmark follows the key press without being pushed.
void MainWindow::ToggleEditorDock()    { m_dockEditor->toggleViewAction()->trigger(); }
void MainWindow::ToggleLibraryDock()   { m_dockLibrary->toggleViewAction()->trigger(); }
void MainWindow::ToggleTransportDock() { m_dockTransport->toggleViewAction()->trigger(); }
void MainWindow::ToggleRecordingDock() { m_dockRecording->toggleViewAction()->trigger(); }
void MainWindow::ToggleSpoutDock()     { m_dockSpout->toggleViewAction()->trigger(); }

// ---------------------------------------------------------------------------------------
// Visibility
// ---------------------------------------------------------------------------------------

PanelVisibility MainWindow::GetVisibility() const
{
    PanelVisibility visibility;
    visibility.editor    = !m_dockEditor->isHidden();
    visibility.library   = !m_dockLibrary->isHidden();
    visibility.transport = !m_dockTransport->isHidden();
    visibility.recording = !m_dockRecording->isHidden();
    visibility.noise     = !m_dockNoise->isHidden();
    visibility.spout     = !m_dockSpout->isHidden();
    visibility.audio     = !m_dockAudio->isHidden();
    return visibility;
}

void MainWindow::ShowNoiseDock()
{
    // Tabbed behind another dock is the common case (the default layout tabs it with
    // Recording and Spout), so showing it is not enough on its own: raise() is what brings
    // the tab forward and makes the shortcut land somewhere visible.
    m_dockNoise->show();
    m_dockNoise->raise();
}

void MainWindow::ApplyVisibility(const PanelVisibility& v)
{
    m_dockEditor->setVisible(v.editor);
    m_dockLibrary->setVisible(v.library);
    m_dockTransport->setVisible(v.transport);
    m_dockRecording->setVisible(v.recording);
    m_dockNoise->setVisible(v.noise);
    m_dockSpout->setVisible(v.spout);
    m_dockAudio->setVisible(v.audio);
}

// ---------------------------------------------------------------------------------------
// Toasts
// ---------------------------------------------------------------------------------------

void MainWindow::ShowToast(const QString& msg, int ms)
{
    m_toasts->Show(msg, ms);
}

// ---------------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------------

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;

    // Only the first file, exactly as the Win32 drop handler did.
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) return;
    event->acceptProposedAction();

    const std::string utf8 = path.toStdString();
    const QString extension = QFileInfo(path).suffix().toLower();

    if (extension == QLatin1String("hlsl") || extension == QLatin1String("fx")
        || extension == QLatin1String("ps")) {
        ShaderManager& shaders = m_app.GetShaderManager();
        ShaderPreset preset;
        // Metadata only: AddPreset compiles, and LoadShaderFromFile compiled first into a
        // local whose bytecode was then thrown away. A file that fails to compile is still
        // added, carrying its error, exactly as a scanned one is.
        if (!ShaderManager::LoadShaderMetadataFromFile(utf8, preset)) return;
        const int index = shaders.AddPreset(preset);
        shaders.SetActivePreset(index);
        m_app.OnParamChanged();
        RefreshAll();
        ShowToast(tr("Loaded shader: %1").arg(QString::fromStdString(preset.name)));
    } else {
        m_app.OpenVideo(utf8);
    }
}

void MainWindow::SaveWindowState()
{
    // Called from closeEvent and from Application::RequestExit. quit() unwinds the event
    // loop without closing widgets, so File > Exit and the menu's Alt+F4 never reach
    // closeEvent, and every layout change made in that session was thrown away.
    AppConfig& cfg = m_app.GetConfig();
    cfg.windowGeometry = saveGeometry().toBase64().toStdString();
    cfg.windowState = saveState(kWorkspaceStateVersion).toBase64().toStdString();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Application::Shutdown() calls SaveConfig() unconditionally after qApp->exec()
    // returns, and RequestExit()'s quit() is what makes exec() return — so this runs
    // and completes before that save, and the write below reaches disk.
    SaveWindowState();

    m_app.RequestExit();
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    // Auto-repeat is dropped: holding Space would otherwise fire TogglePlayback over and
    // over and leave the player in the state opposite the one intended.
    if (event->isAutoRepeat()) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    // Space and Escape are reserved actions rather than bindable ones, so the shared
    // mapping refuses them (see ui/KeyMap.h) and the dispatch side names them here.
    int vk = 0;
    switch (event->key()) {
    case Qt::Key_Space:  vk = VK_SPACE;  break;
    case Qt::Key_Escape: vk = VK_ESCAPE; break;
    default:             vk = VkFromQt(*event); break;
    }

    if (vk == 0) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    // Every character a focused text field can actually insert is already consumed
    // there before this handler runs — Qt's own editing widgets accept Space and every
    // printable key as typed input, so those never reach this point while the editor,
    // the library filter, or a spin box has focus. Escape is the one exception: no
    // QLineEdit, QPlainTextEdit or spin box claims it (that is conventionally a
    // dialog's job), so it reaches here even mid-edit and would otherwise reset the
    // shader out from under whatever is being typed. F-keys and Ctrl-chords (F5
    // compile, Ctrl+S save) are deliberately left able to fire while a text field has
    // focus — they act on the shader document itself, matching ordinary editor
    // conventions where those keep working while the cursor is in the text.
    if (vk == VK_ESCAPE && (m_editor->IsFocused() || IsOtherTextFieldFocused())) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    // Only keys no focused widget wanted arrive here — a QPlainTextEdit accepts its own
    // Space and letters — which is what the outgoing shell's WantsCaptureKeyboard test
    // was for. HandleKeyboardShortcuts reads the modifiers itself, off the live keyboard
    // state, so nothing else has to be passed across.
    m_app.HandleKeyboardShortcuts(static_cast<UINT>(vk));
    event->accept();
}

// ---------------------------------------------------------------------------------------
// Menu handlers
// ---------------------------------------------------------------------------------------

void MainWindow::ToggleRecording()
{
    // The panel owns the settings and the toggle; F9 and the menu are another way to press
    // its button, not a second route into the encoder.
    m_recording->ToggleRecording();
}

void MainWindow::OnNewShader()
{
    const QString name = NewShaderDialog::Create(m_app, this);
    if (name.isEmpty()) return;   // cancelled

    // The dialog made the preset active; these are the follow-ups every SetActivePreset call
    // site owes (see CLAUDE.md), plus the document the editor now opens on.
    RefreshAll();
    m_app.OnParamChanged();
    ShowToast(tr("New shader: %1").arg(name));
}

void MainWindow::ShowKeybindingsReference()
{
    // Per showing, never cached: the list is a snapshot of the presets and their bindings at
    // the moment it opens, and a kept instance would show the state of the last time.
    KeybindingsReferenceDialog dialog(m_app, this);
    dialog.exec();
}

void MainWindow::OnManageWorkspaces()
{
    WorkspacesDialog dialog(m_app, this);
    dialog.exec();
    // The workspace menu rebuilds itself from aboutToShow, so a rename, a deletion or a new
    // shortcut is picked up the next time it is opened without being pushed from here.
}

void MainWindow::OnOpenCapture()
{
    CaptureDialog dialog(m_app, this);
    dialog.exec();
}

void MainWindow::OnSaveWorkspaceAs()
{
    const QString name = QInputDialog::getText(this, tr("Save Workspace"),
                                                tr("Workspace name:")).trimmed();
    if (name.isEmpty()) return;   // cancelled, or left blank — getText returns "" either way

    const int index = m_app.GetWorkspaceManager().SavePreset(
        name.toStdString(), saveState(kWorkspaceStateVersion), GetVisibility());

    ShowToast(index >= 0 ? tr("Workspace saved: %1").arg(name)
                         : tr("Could not save workspace: %1").arg(name));
}

void MainWindow::LoadWorkspacePreset(int index)
{
    QByteArray state;
    PanelVisibility panels;
    if (!m_app.GetWorkspaceManager().LoadPreset(index, state, panels)) return;

    // restoreState() is atomic — on a version mismatch or corrupt blob it leaves the
    // current layout untouched and returns false, so falling back here is a deliberate
    // re-arrange rather than a repair of something half-applied. The built-in Default
    // takes the same path today because its blob is empty until A18 captures the real one.
    if (state.isEmpty() || !restoreState(state, kWorkspaceStateVersion)) {
        ArrangeDefaultLayout();
    }
    ApplyVisibility(panels);

    const std::string& name = m_app.GetWorkspaceManager().GetPresets()[
        static_cast<size_t>(index)].name;
    ShowToast(tr("Workspace: %1").arg(QString::fromStdString(name)));
}

} // namespace SP
