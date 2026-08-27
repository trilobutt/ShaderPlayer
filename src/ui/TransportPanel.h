#pragma once

// TransportPanel.h — the body of the Transport dock: the control a user touches more than
// any other, so its feel is held to a higher bar than the panels around it.
//
// The track is a hand-painted widget rather than a QSlider, for two reasons that a styled
// slider cannot answer:
//
//   It carries a second data layer. The selected parameter's keyframes are drawn as
//   diamonds in a lane under the groove, in the Shader Parameters region hue, so they read
//   as that region's data visiting this one rather than competing with the cyan playhead.
//   Every diamond is clickable: pressing one seeks to it and selects it.
//
//   The knob is a physical object. It grows and glows as the pointer arrives, gives under
//   the press, and then tracks the cursor with no animation at all, because an eased
//   playhead during a scrub is lag with a nice name. Only the arrival and departure of the
//   hover state is animated.
//
// Keyframe follow mode lives here too, both halves of it: the toggle, and the behaviour it
// drives (scrubbing carries the selected keyframe with the playhead, also reachable by
// holding Shift without arming anything). The move goes through copy / RemoveKeyframe /
// AddKeyframe and takes the index AddKeyframe hands back, because KeyframeTimeline keeps
// its vector sorted only through that pair; writing a time into a keyframe still sitting in
// the vector leaves the evaluator's binary search reading the wrong segment.
//
// The selection itself is owned by ParamsPanel. This panel is told about it
// (SetKeyframeSelection) and asks for changes to it (KeyframeMoved, KeyframeActivated);
// it never keeps a second copy of the truth.

#include "Common.h"

#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QEvent;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QComboBox;
class QToolButton;
class QVBoxLayout;
class QVariantAnimation;
QT_END_NAMESPACE

namespace SP {

class Application;

// The track: groove, playhead, and the selected parameter's keyframe diamonds.
class Scrubber : public QWidget {
    Q_OBJECT
public:
    explicit Scrubber(Application& app, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Pushed down from ParamsPanel through TransportPanel. Never emits.
    void SetKeyframeSelection(int paramIndex, int keyframeIndex);

    // The follow toggle's state. Shift held during a drag arms it for that drag alone.
    void SetFollowArmed(bool armed);
    bool Following() const;

    // Once per frame: the playhead from the decoder (unless the user is holding it), and
    // the keyframe times, which change under the panel without any signal when a track is
    // edited elsewhere. Repaints only when one of them actually moved.
    void Tick();

signals:
    // The selected keyframe was carried to a new time and AddKeyframe returned this index.
    void KeyframeMoved(int paramIndex, int keyframeIndex);

    // A diamond was clicked: seek there and make it the selection.
    void KeyframeActivated(int paramIndex, int keyframeIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRectF Groove() const;
    qreal KnobX() const;
    qreal XFor(float seconds) const;
    float TimeAt(qreal x) const;          // clamped to [0, duration], snapped in frame mode
    double Duration() const;
    double Fps() const;
    bool FrameMode() const;

    int KeyframeAt(const QPointF& pos) const;
    void SetKnobHovered(bool on);
    void RefreshCursor(const QPointF& pos);

    // Seek, and carry the selected keyframe along when following.
    void ScrubTo(float seconds);
    void MoveSelectedKeyframe(float seconds);

    KeyframeTimeline* SelectedTimeline() const;
    bool RefreshKeyframes();              // true when the cached times changed

    Application& m_app;

    // The keyframe times of the selected parameter, cached so paint walks a flat array
    // rather than chasing an optional through a preset every repaint.
    QVector<float> m_keyTimes;

    int m_selParam = -1;
    int m_selKey = -1;

    bool m_followArmed = false;           // the toggle
    bool m_shiftHeld = false;             // Shift at the moment of the press
    bool m_dragging = false;
    int m_hoverKey = -1;
    bool m_hoverKnob = false;
    bool m_hoverTrack = false;

    float m_time = 0.0f;                  // the painted playhead, in seconds
    qreal m_lastX = -1.0;                 // where the knob was last painted
    qreal m_knobT = 0.0;                  // 0 dormant, 1 fully bloomed
    QVariantAnimation* m_knobAnim = nullptr;
};

class TransportPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransportPanel(Application& app, QWidget* parent = nullptr);

    // From ParamsPanel::KeyframeSelectionChanged. Clearing the selection also disarms
    // follow mode: a toggle pointing at nothing would silently move the next keyframe
    // selected instead.
    void SetKeyframeSelection(int paramIndex, int keyframeIndex);

    // Once per frame: playhead, clock, and the live state that decides which page of the
    // track stack is showing. Nothing here rebuilds a widget tree.
    void Tick();

signals:
    void KeyframeMoved(int paramIndex, int keyframeIndex);
    void KeyframeActivated(int paramIndex, int keyframeIndex);

private:
    // What the track area is showing. Live state, picked in Tick, never a rebuild.
    enum Page { kPageScrub = 0, kPageLive, kPageGenerative, kPageIdle };

    void BuildTrack(QVBoxLayout* layout);
    void BuildControls(QVBoxLayout* layout);
    void BuildLivePage(QWidget* page);
    void BuildGenerativePage(QWidget* page);
    void BuildIdlePage(QWidget* page);

    void SyncPage();
    void SyncPlayButton();
    void SyncClock();
    void SyncAudioRow();
    void SyncFollowButton();
    void SyncMuteButton();
    void SyncRenderLock();                // greys the transport while a render owns it
    void ApplyPlayButton();               // text and tooltip from m_playing + m_renderLocked
    void SyncResolution();                // combo selection from AppConfig, and the custom pair

    bool FrameMode() const;
    double Fps() const;

    // Frames or seconds, per AppConfig::timeDisplayFrames. Playback time is always stored
    // in seconds; this is the only place it becomes anything else.
    QString TimeText(double seconds) const;
    QString DurationText() const;

    Application& m_app;

    QStackedWidget* m_stack = nullptr;
    Scrubber* m_scrubber = nullptr;

    QPushButton* m_play = nullptr;
    QPushButton* m_stop = nullptr;
    QWidget* m_clockBlock = nullptr;      // hidden on the pages that carry their own clock
    QLabel* m_clock = nullptr;
    QLabel* m_duration = nullptr;
    QToolButton* m_units = nullptr;
    QPushButton* m_follow = nullptr;

    QWidget* m_audioRow = nullptr;
    QPushButton* m_mute = nullptr;
    QSlider* m_volume = nullptr;

    QLabel* m_liveClock = nullptr;
    QLabel* m_genClock = nullptr;
    QComboBox* m_genPreset = nullptr;
    QWidget* m_genCustom = nullptr;
    QSpinBox* m_genWidth = nullptr;
    QSpinBox* m_genHeight = nullptr;

    // The user asked for a custom size and has not picked a preset since. Without it the
    // combo bounces straight back off "Custom", because the config it reads still matches
    // whichever preset was last applied.
    bool m_genCustomLatched = false;

    int m_selParam = -1;
    int m_selKey = -1;

    // Last applied live state, so the per-frame sync is a compare rather than a restyle.
    int m_page = -1;
    bool m_playing = false;
    bool m_renderLocked = false;
    bool m_hasAudio = false;
    bool m_followEnabled = false;
    QString m_clockText;
    QString m_durationText;
};

}  // namespace SP
