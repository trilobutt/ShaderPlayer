#pragma once

// RecordingPanel.h — the body of the Recording dock.
//
// This panel exists to prevent one specific failure: a recording left running for hours
// into a file nobody is watching. Everything about it is arranged around that.
//
//   The armed state is unmissable. While the encoder runs, the panel's own border pulses
//   in state-error, the elapsed clock is the largest thing in the dock, and the output
//   path sits right under it, so a glance answers both "am I recording" and "into what".
//
//   Stopping is the easiest thing to do. The toggle spans the panel and grows when armed,
//   so the largest target on the surface while recording is the one that ends it.
//
//   The settings cannot drift under a running encoder. Path, codec, bitrate and profile
//   are all disabled while recording, because FFmpeg took its copy at StartRecording and a
//   widget that still moves is telling the user something untrue.
//
// The panel is the only editor of the RecordingSettings store it is handed (MainWindow
// owns it), and ToggleRecording is the only place recording starts or stops: the Recording
// menu and F9 come through here rather than keeping a second copy of the settings.

#include "Common.h"

#include <QElapsedTimer>
#include <QLineEdit>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QPaintEvent;
class QPushButton;
class QSpinBox;
class QVariantAnimation;
class QVBoxLayout;
QT_END_NAMESPACE

namespace SP {

class Application;

// A path field that shows the middle of a long path elided rather than its first thirty
// characters, which on a Windows path is the drive letter and nothing that identifies the
// file. The full path is the truth held here; the elided form only ever reaches the
// widget's display, so no elision can be committed by accident. Focus restores the real
// text for editing.
class PathEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit PathEdit(QWidget* parent = nullptr);

    QString Path() const { return m_path; }
    void SetPath(const QString& path);

signals:
    void PathChanged(const QString& path);

protected:
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void ShowElided();

    QString m_path;
};

class RecordingPanel : public QWidget {
    Q_OBJECT
public:
    // `settings` is MainWindow's store and outlives this panel; the widgets write straight
    // into it, so what the menu records with is always what the panel shows.
    RecordingPanel(Application& app, RecordingSettings& settings, QWidget* parent = nullptr);

    // Start or stop, whichever applies. The panel's own toggle, the Recording menu and F9
    // all land here so there is exactly one path into the encoder.
    void ToggleRecording();

    // Once per frame: the elapsed clock, the counters, and the armed/idle state. Nothing
    // here rebuilds a widget the user could be mid-edit in.
    void Tick();

signals:
    // A recording just started. MainWindow brings this dock forward on it: the armed state
    // cannot be unmissable while the dock is tabbed behind another.
    void RecordingArmed();

protected:
    // Paints the border that pulses while armed. The dock's own frame and glow belong to
    // RegionDock and are left alone.
    void paintEvent(QPaintEvent* event) override;

private:
    void BuildOutput(QVBoxLayout* layout);
    void BuildEncoding(QVBoxLayout* layout);
    void BuildStatus(QVBoxLayout* layout);

    void SyncCodec();                     // which encoding rows the chosen codec shows
    void SyncArmed(bool recording);       // idle <-> armed: labels, sizes, enablement, pulse
    void SyncStatus();                    // the clock and the counters
    // Idle only: whether pressing the button renders an open file end to end or captures
    // whatever is on screen until stopped. Two different actions behind one button, so the
    // button has to say which one it is before it is pressed.
    void SyncIdleAffordance();

    void BrowseForOutput();

    // Copy the store into AppConfig::recordingDefaults and save. Every widget that edits a
    // setting ends here: the store belongs to MainWindow and lives only as long as the
    // window, so a change that is not written through is lost at exit.
    void Persist();

    Application& m_app;
    RecordingSettings& m_settings;

    PathEdit* m_path = nullptr;
    QPushButton* m_browse = nullptr;
    QComboBox* m_codec = nullptr;
    QWidget* m_bitrateRow = nullptr;
    QSpinBox* m_bitrate = nullptr;
    QWidget* m_profileRow = nullptr;
    QComboBox* m_profile = nullptr;

    QPushButton* m_toggle = nullptr;
    QWidget* m_statusBlock = nullptr;     // hidden while idle
    QLabel* m_badge = nullptr;
    QLabel* m_elapsed = nullptr;
    QLabel* m_destination = nullptr;
    QLabel* m_counters = nullptr;
    QLabel* m_idleHint = nullptr;

    // Started on the frame the encoder is first seen running, whichever surface armed it.
    QElapsedTimer m_since;

    QVariantAnimation* m_pulse = nullptr;
    qreal m_pulseT = 0.0;                 // 0 dormant, 1 fully lit

    // Last applied live state, so the per-frame sync is a compare rather than a restyle.
    bool m_armed = false;
    int m_fileSourceApplied = -1;         // tri-state: -1 forces the idle button to re-apply
    bool m_rendering = false;             // that render is running
    QString m_elapsedText;
    QString m_countersText;
};

}  // namespace SP
