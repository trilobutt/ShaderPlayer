#pragma once

// KeyframeDetail.h — one parameter's keyframe track: the strip that creates and selects
// keyframes, and the editor for whichever one is selected.
//
// It hangs in ParamsPanel::KeyframeDetailHost(paramIndex), directly beneath the parameter
// it animates, and is shown for exactly as long as that parameter's KF toggle is on. The
// strip (a "+ Key" button and a chip per keyframe) is the only way a keyframe is ever
// created or selected, so it stays visible whenever the track is on; the editor beneath it
// appears only once a chip is picked.
//
// Two contracts carry over from the immediate-mode panel this replaces, and both are the
// kind that a rewrite silently drops:
//
//   Time is repositioned by copy / RemoveKeyframe / AddKeyframe, taking the index
//   AddKeyframe returns. KeyframeTimeline keeps its vector sorted only through that pair,
//   so writing a new time into a keyframe still sitting in the vector corrupts the order
//   the evaluator binary-searches. RepositionSelected is the single place it happens.
//
//   Every mutation ends in Application::OnParamChanged. The outgoing function carried an
//   `anyChanged` out-parameter that each branch (including its early returns) had to set;
//   here Mutated() is the one place `Changed` is emitted and every mutating branch calls
//   it. ParamsPanel connects that to its own Changed(), which owns the OnParamChanged call.

#include "Common.h"

#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QVBoxLayout;
QT_END_NAMESPACE

namespace SP {

class Application;
class BezierEditor;

// The chips, wrapped. Qt ships no wrapping layout and height-for-width through a scroll
// area is fragile enough to clip a row silently, so the strip places its own children and
// sets its own height from the result.
class ChipStrip : public QWidget {
public:
    explicit ChipStrip(QWidget* parent = nullptr);

    void Clear();                    // deletes the chips it holds
    void Add(QPushButton* chip);
    void Reflow();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QVector<QPushButton*> m_chips;
    int m_height = 0;
};

class KeyframeDetail : public QWidget {
    Q_OBJECT
public:
    KeyframeDetail(Application& app, ShaderPreset* preset, int paramIndex,
                   QWidget* parent = nullptr);

    // -1 hides the editor and leaves the strip. Never emits: it is how ParamsPanel pushes
    // its selection down, including the pushes that clear it.
    void SetSelectedKeyframe(int keyframeIndex);

    // Chips and editor from the data as it stands. Never emits.
    void Refresh();

signals:
    // The single mutation signal. ParamsPanel routes it to Application::OnParamChanged.
    void Changed();

    // A keyframe was picked, added or repositioned, or the selection was dropped because
    // its keyframe was deleted. ParamsPanel owns the selection; this widget asks.
    void SelectionRequested(int paramIndex, int keyframeIndex);

private:
    ShaderParam* Param() const;
    KeyframeTimeline* Timeline() const;
    Keyframe* Selected() const;

    void BuildStrip(QVBoxLayout* layout);
    void BuildEditor(QVBoxLayout* layout);
    void BuildValueEditor(QWidget* cell);

    void RebuildChips();
    void SyncEditor();
    void SyncValueEditor(const Keyframe& kf);
    void UpdateCurveCaption();

    void AddKeyAtPlayhead();
    void SeekToKeyframe(int index);
    void RepositionSelected(float newTime);
    void DeleteSelected();

    // The one place `Changed` is emitted. Every branch that touches the timeline ends here.
    void Mutated();

    // Frames or seconds, per AppConfig::timeDisplayFrames. Keyframe::time is always
    // seconds; this is only ever the display layer.
    bool FrameMode() const;
    double Fps() const;
    QString TimeText(float seconds) const;

    Application& m_app;
    ShaderPreset* m_preset = nullptr;
    const int m_index;                     // into ShaderPreset::params
    ShaderParamType m_type = ShaderParamType::Float;
    int m_selected = -1;

    ChipStrip* m_strip = nullptr;
    QLabel* m_empty = nullptr;             // "no keyframes yet", in place of the chips

    QWidget* m_editor = nullptr;           // everything below the strip
    QDoubleSpinBox* m_timeSeconds = nullptr;
    QSpinBox* m_timeFrames = nullptr;
    QPushButton* m_snap = nullptr;
    QPushButton* m_delete = nullptr;

    QSlider* m_slider = nullptr;
    QDoubleSpinBox* m_spin[2] = {nullptr, nullptr};
    QCheckBox* m_check = nullptr;
    QComboBox* m_choice = nullptr;
    QPushButton* m_swatch = nullptr;

    QLabel* m_curveLabel = nullptr;
    QComboBox* m_curve = nullptr;
    BezierEditor* m_bezier = nullptr;
    QLabel* m_caption = nullptr;
};

}  // namespace SP
