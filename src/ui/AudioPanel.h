#pragma once

// AudioPanel.h — the body of the Audio Monitor dock.
//
// This is the one surface in the application where continuous animation is correct: every
// number on it is a live measurement, and a meter that only moved on an event would be
// lying about a signal that is changing sixty times a second. Everywhere else in the UI,
// idle motion competes with the work; here it *is* the work.
//
// That licence is spent carefully:
//
//   The spectrum is painted, not built. 256 bins is 256 widgets if the obvious thing is
//   done, and 256 widgets restyled per frame in a docked panel is real CPU for a strip of
//   colour. SpectrumView draws the whole field in one QPainter pass: one filled path for
//   the bars and one stroked path for the peak trace.
//
//   Nothing repaints while the dock is hidden. Tick returns immediately on !isVisible(),
//   so a monitor tabbed behind another dock costs nothing at all.
//
//   Silence is stated rather than drawn. A spectrum with no signal is a flat line, which
//   reads as a broken widget rather than as quiet, so both the no-source case and the
//   nothing-playing case say in words which one they are.
//
// The DSP sliders write AppConfig::audio and reach the analyser through
// Application::UpdateAudioSettings, which also persists. That call is a config write, so it
// happens when a drag ends rather than on every tick of one.

#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPaintEvent;
class QProgressBar;
class QSlider;
class QStackedWidget;
class QVBoxLayout;
QT_END_NAMESPACE

namespace SP {

class Application;

// The 256-bin spectrum, drawn in one pass: a filled area profile for the current frame and
// a decaying peak trace above it, so a transient stays legible for longer than the single
// frame it occupied.
class SpectrumView : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumView(QWidget* parent = nullptr);

    // Copies the bins and repaints. `silent` swaps the field for a line of text: a flat
    // spectrum and a broken spectrum look identical.
    void SetSpectrum(const float* bins, int count, bool silent);

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<float> m_bins;
    QVector<float> m_peaks;
    bool m_silent = true;
};

class AudioPanel : public QWidget {
    Q_OBJECT
public:
    explicit AudioPanel(Application& app, QWidget* parent = nullptr);

    // Once per frame: the band meters and the spectrum. Does nothing while hidden.
    void Tick();

private:
    void BuildLive(QVBoxLayout* layout);
    void BuildLevels(QVBoxLayout* layout);
    void BuildSpectrum(QVBoxLayout* layout);
    void BuildSettings(QVBoxLayout* layout);
    QWidget* BuildEmptyPage(QWidget* parent);

    // One DSP slider: the readout follows the handle, the analyser and the config file
    // follow the release.
    void AddSetting(QVBoxLayout* layout, const QString& label, const QString& tip,
                    float& store, float min, float max);

    void UpdateMeters();

    Application& m_app;

    QStackedWidget* m_stack = nullptr;

    // The five live levels, in the order they are read.
    struct Meter {
        QProgressBar* bar = nullptr;
        QLabel* value = nullptr;
        bool hot = false;          // beat only: restyled on the flip, never per frame
    };
    Meter m_rms;
    Meter m_bass;
    Meter m_mid;
    Meter m_high;
    Meter m_beat;

    SpectrumView* m_spectrum = nullptr;

    bool m_hadAudio = false;       // last applied page, so Tick is a compare
};

}  // namespace SP
