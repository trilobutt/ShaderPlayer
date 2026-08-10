#pragma once

// ParamsPanel.h — the body of the Shader Parameters dock, and the product's main working
// surface: every other panel is configuration around it.
//
// One row per ISF parameter, laid out on a single grid so that the label column, the
// controls and the two per-row buttons keep one set of vertical edges however many
// parameters a shader declares (the CRT shader declares fifteen). A row's value stays
// readable beside its control rather than hiding in a tooltip, because reading a shader's
// current state is half of what this panel is for.
//
// The update model splits in two, and the split is the whole reason the panel is fast:
//
//   SetPreset()  rebuilds the widget tree. Called from MainWindow::RefreshParameters, so
//                only when the active preset or its parameter list actually changed.
//   SyncValues() writes current values into the existing widgets, under QSignalBlocker so
//                nothing loops back into Application::OnParamChanged. Called every frame
//                while a keyframe timeline is driving values, which is the one case where
//                a parameter changes without the user touching it.
//   Tick()       the live meters (AudioBand) plus that keyframe-driven sync.
//
// Parameters are addressed by index into `ShaderPreset::params`, never by ShaderParam*: a
// recompile rebuilds that vector and every element address with it.

#include "Common.h"

#include <QString>
#include <QVector>
#include <QWidget>

#include <random>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSlider;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;
QT_END_NAMESPACE

namespace SP {

class Application;
class KeyframeDetail;

class ParamsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ParamsPanel(Application& app, QWidget* parent = nullptr);

    // nullptr is passthrough and a real state of the product, not an error: it gets a page
    // saying so, as does a preset that declares no INPUTS.
    void SetPreset(ShaderPreset* preset);

    // Widget values from the parameter values, without rebuilding and without emitting.
    void SyncValues();

    // Once per frame from MainWindow::Tick: the AudioBand meters, and SyncValues while a
    // timeline owns the values.
    void Tick();

    // ---- keyframe seam -------------------------------------------------------------------
    // Each keyframeable row owns a full-width host directly beneath it, holding that
    // parameter's KeyframeDetail and shown for exactly as long as the row's KF toggle is
    // on. The selection KeyframeDetail edits and the transport's diamonds read lives here,
    // so it survives a rebuild policy this panel already owns (it is cleared whenever the
    // shown preset changes or a KF toggle goes off).
    QWidget* KeyframeDetailHost(int paramIndex) const;
    void SetKeyframeSelection(int paramIndex, int keyframeIndex);
    int SelectedKeyframeParam() const { return m_selectedKeyframeParam; }
    int SelectedKeyframeIndex() const { return m_selectedKeyframeIndex; }

signals:
    void KeyframeSelectionChanged(int paramIndex, int keyframeIndex);

    // The shader samples the global noise texture, and the user asked for the dial that
    // generates it. The panel cannot reach the Noise dock itself, so MainWindow answers.
    void NoiseGeneratorRequested();

private:
    // Every widget a row can own. Only the members its type uses are non-null.
    struct Row {
        // A widget and the tooltip it carries when the user owns the value, so the
        // keyframe-driven state can borrow every tooltip and give them all back.
        struct Tip {
            QWidget* widget = nullptr;
            QString text;
        };

        int index = 0;                     // into ShaderPreset::params
        ShaderParamType type = ShaderParamType::Float;

        QLabel* label = nullptr;
        QWidget* cell = nullptr;           // the control column's container
        QSlider* slider = nullptr;
        QDoubleSpinBox* spin[2] = {nullptr, nullptr};
        QCheckBox* check = nullptr;
        QComboBox* combo = nullptr;
        QPushButton* swatch = nullptr;
        QLabel* swatchText = nullptr;
        QPushButton* trigger = nullptr;
        QProgressBar* meter = nullptr;
        QLabel* meterValue = nullptr;
        QToolButton* reset = nullptr;
        QToolButton* randomise = nullptr;
        QToolButton* keyframe = nullptr;
        QWidget* detailHost = nullptr;     // shown for as long as the KF toggle is on
        KeyframeDetail* detail = nullptr;  // the keyframe track living in that host

        QVector<Tip> tips;                 // every widget above that carries one
        int ticks = 1;                     // slider resolution for Float
        bool beatHot = false;              // last applied beat tint, so the restyle is rare
        bool keyframed = false;            // last applied disabled state
    };

    enum Page { kPageParams = 0, kPagePassthrough, kPageNoParams };

    ShaderParam* Param(int index) const;

    void BuildRows();
    void BuildRow(QGridLayout* grid, int index, int gridRow);
    void UpdateMeters();
    void SetRowKeyframed(Row& row, bool driven);
    void PaintSwatch(Row& row, const ShaderParam& param);

    // A row's reset arrow is live only while there is something to undo, which is a byte
    // comparison against the declared defaults rather than a float ==.
    void UpdateResetStates();

    // Video blend: chrome around the parameter list rather than a parameter, so it gets its
    // own block at the foot of the panel and writes AppConfig rather than the cbuffer.
    void BuildBlendSection(QVBoxLayout* layout);
    void SyncBlend();
    void UpdateBlendVisibility();
    void SaveBlend();

    // True while the timeline, not the user, owns this parameter's value.
    bool IsDriven(const ShaderParam& param) const;

    // Ported verbatim from UIManager::RandomiseParam: every branch draws from exactly the
    // range its widget exposes, and the types with nothing to roll say so by returning false.
    bool RandomiseParam(ShaderParam& param);

    void RandomiseAll();
    void ResetAll();
    void Changed();                        // the one route to Application::OnParamChanged

    Application& m_app;

    ShaderPreset* m_preset = nullptr;
    QString m_shownName;                   // guards scroll/selection across a recompile

    QWidget* m_header = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_noiseShortcut = nullptr;   // shown when the source samples noiseTexture
    QStackedWidget* m_stack = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;          // rebuilt wholesale by SetPreset
    QLabel* m_noParams = nullptr;

    QVector<Row> m_rows;

    // Video blend. The mode and amount live on the preset, not on a ShaderParam, so these
    // bypass Changed() and persist through Application::SaveConfig instead.
    QWidget* m_blend = nullptr;
    QComboBox* m_blendMode = nullptr;
    QWidget* m_blendAmountRow = nullptr;      // hidden while the mode is Off
    QSlider* m_blendAmount = nullptr;
    QDoubleSpinBox* m_blendAmountSpin = nullptr;
    bool m_blendShown = false;                // isVisible() also answers for a closed dock

    int m_selectedKeyframeParam = -1;
    int m_selectedKeyframeIndex = -1;
    bool m_wasDriven = false;              // so the release from keyframe control syncs once

    // Seeded once from system entropy; the randomiser is a toy, not a source of secrets.
    std::mt19937 m_rng{std::random_device{}()};
};

} // namespace SP
