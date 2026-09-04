#include "ui/TransportPanel.h"

#include "Application.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace SP {

namespace {

// ---- track geometry ---------------------------------------------------------------------
// The groove sits high in the widget and the keyframe lane hangs beneath it, so the two data
// layers occupy separate bands: nothing a keyframe draws can ever sit on top of the
// playhead, and a press lands unambiguously in one band or the other.
constexpr int   kSideInset  = 14;    // room for the knob to be whole at either end
constexpr qreal kGrooveY    = 17.0;  // centre of the groove, from the top
constexpr qreal kGrooveH    = 8.0;
constexpr qreal kLaneY      = 35.0;  // centre of the keyframe lane
constexpr qreal kDiamond    = 5.0;   // half-width of a resting diamond
constexpr qreal kKeyGrab    = 9.0;   // an 18px target, over the 12px floor
constexpr qreal kKnobR      = 7.5;
constexpr qreal kKnobGrow   = 2.4;   // fully hovered
constexpr qreal kKnobGive   = 1.6;   // pressed: the knob takes the weight
constexpr qreal kKnobGrab   = 14.0;

constexpr int kMinTrackWidth  = 180;
constexpr int kTrackHint      = 360;
constexpr int kTrackHeight    = 50;

constexpr int kVolumeSteps = 100;
constexpr int kVolumeWidth = 116;

// Said on every control a render has taken over, so whichever one the pointer lands on
// gives the same answer.
QString RenderLockHint()
{
    return TransportPanel::tr("Rendering to file. Stop the recording to take the "
                              "transport back.");
}

// The one number the eye goes to, so it is the largest thing in the dock.
constexpr int kClockPoints = Theme::kFontSizePanelTitle + 2;

// Generative output sizes, exactly the list the outgoing transport offered.
struct ResPreset { const char* label; int w; int h; };
constexpr ResPreset kResPresets[] = {
    { "1280 x 720",   1280,  720 },
    { "1920 x 1080",  1920, 1080 },
    { "2560 x 1440",  2560, 1440 },
    { "3840 x 2160",  3840, 2160 },
    { "1080 x 1080",  1080, 1080 },
    { "2048 x 2048",  2048, 2048 },
    { "Custom",          0,    0 },
};
constexpr int kResCount  = static_cast<int>(std::size(kResPresets));
constexpr int kResCustom = kResCount - 1;

// mm:ss.cc, growing an hours field only when there is one to show. Digits that tick in
// place want a monospace face and a stable width; both are set on the label.
QString ClockText(double seconds)
{
    if (!(seconds > 0.0)) seconds = 0.0;
    const int total = static_cast<int>(seconds);
    const int hours = total / 3600;
    const int mins  = (total / 60) % 60;
    const double secs = seconds - static_cast<double>(total - total % 60);

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(mins, 2, 10, QLatin1Char('0'))
            .arg(secs, 5, 'f', 2, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(mins, 2, 10, QLatin1Char('0'))
        .arg(secs, 5, 'f', 2, QLatin1Char('0'));
}

}  // namespace

// =========================================================================================
// Scrubber
// =========================================================================================

Scrubber::Scrubber(Application& app, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
{
    setObjectName(QStringLiteral("TransportScrubber"));
    setMinimumSize(kMinTrackWidth, kTrackHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);          // the knob answers the pointer before the press
    setFocusPolicy(Qt::StrongFocus); // and the track is reachable without one

    m_knobAnim = new QVariantAnimation(this);
    m_knobAnim->setEasingCurve(Theme::kEaseStandard);
    connect(m_knobAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { m_knobT = value.toReal(); update(); });
}

QSize Scrubber::sizeHint() const        { return QSize(kTrackHint, kTrackHeight); }
QSize Scrubber::minimumSizeHint() const { return QSize(kMinTrackWidth, kTrackHeight); }

// ---- geometry ---------------------------------------------------------------------------

QRectF Scrubber::Groove() const
{
    const qreal w = std::max(0.0, static_cast<qreal>(width()) - 2.0 * kSideInset);
    return QRectF(kSideInset, kGrooveY - kGrooveH * 0.5, w, kGrooveH);
}

double Scrubber::Duration() const { return m_app.GetDecoder().GetDuration(); }
double Scrubber::Fps() const      { return m_app.GetDecoder().GetFPS(); }
bool   Scrubber::FrameMode() const { return m_app.GetConfig().timeDisplayFrames; }

qreal Scrubber::XFor(float seconds) const
{
    const QRectF groove = Groove();
    const double duration = Duration();
    if (!(duration > 0.0)) return groove.left();
    const double t = std::clamp(static_cast<double>(seconds) / duration, 0.0, 1.0);
    return groove.left() + t * groove.width();
}

qreal Scrubber::KnobX() const { return XFor(m_time); }

float Scrubber::TimeAt(qreal x) const
{
    const QRectF groove = Groove();
    const double duration = Duration();
    if (!(duration > 0.0) || groove.width() <= 0.0) return 0.0f;

    // Clamped rather than rejected: a drag that leaves the widget still means "the end of
    // the track", and a release out there should commit that rather than snap back.
    const double t = std::clamp((x - groove.left()) / groove.width(), 0.0, 1.0);
    double seconds = t * duration;

    // Frame mode is a display unit, and a scrub in it lands on whole frames the way the
    // outgoing frame-number slider did.
    const double fps = Fps();
    if (FrameMode() && fps > 0.0) seconds = std::round(seconds * fps) / fps;

    return static_cast<float>(std::clamp(seconds, 0.0, duration));
}

// ---- selection and keyframe cache -------------------------------------------------------

KeyframeTimeline* Scrubber::SelectedTimeline() const
{
    if (m_selParam < 0) return nullptr;
    ShaderPreset* preset = m_app.GetShaderManager().GetActivePreset();
    if (!preset || m_selParam >= static_cast<int>(preset->params.size())) return nullptr;

    ShaderParam& param = preset->params[static_cast<size_t>(m_selParam)];
    if (!param.timeline || !param.timeline->enabled) return nullptr;
    return &*param.timeline;
}

bool Scrubber::RefreshKeyframes()
{
    const KeyframeTimeline* timeline = SelectedTimeline();
    const int count = timeline ? static_cast<int>(timeline->keyframes.size()) : 0;

    bool changed = (count != m_keyTimes.size());
    if (!changed) {
        for (int i = 0; i < count; ++i) {
            if (m_keyTimes[i] != timeline->keyframes[static_cast<size_t>(i)].time) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) return false;

    m_keyTimes.resize(count);
    for (int i = 0; i < count; ++i) {
        m_keyTimes[i] = timeline->keyframes[static_cast<size_t>(i)].time;
    }
    if (m_hoverKey >= count) m_hoverKey = -1;
    return true;
}

void Scrubber::SetKeyframeSelection(int paramIndex, int keyframeIndex)
{
    m_selParam = paramIndex;
    m_selKey = keyframeIndex;
    RefreshKeyframes();
    update();
}

void Scrubber::SetFollowArmed(bool armed)
{
    if (m_followArmed == armed) return;
    m_followArmed = armed;
    update();
}

bool Scrubber::Following() const
{
    if (!(m_followArmed || m_shiftHeld)) return false;
    return m_selParam >= 0 && m_selKey >= 0 && SelectedTimeline() != nullptr;
}

// ---- hover ------------------------------------------------------------------------------

void Scrubber::SetKnobHovered(bool on)
{
    if (m_hoverKnob == on) return;
    m_hoverKnob = on;

    const qreal target = on ? 1.0 : 0.0;
    m_knobAnim->stop();
    m_knobAnim->setDuration(Theme::kMotionHover);
    m_knobAnim->setStartValue(m_knobT);
    m_knobAnim->setEndValue(target);
    m_knobAnim->start();
}

int Scrubber::KeyframeAt(const QPointF& pos) const
{
    // The lane only: a press on the groove is a scrub even when a diamond sits under it.
    if (pos.y() < Groove().bottom()) return -1;
    if (std::abs(pos.y() - kLaneY) > kKeyGrab) return -1;

    int best = -1;
    qreal bestDist = kKeyGrab;
    for (int i = 0; i < m_keyTimes.size(); ++i) {
        const qreal dist = std::abs(pos.x() - XFor(m_keyTimes[i]));
        if (dist <= bestDist) { bestDist = dist; best = i; }
    }
    return best;
}

void Scrubber::RefreshCursor(const QPointF& pos)
{
    if (m_dragging)                     setCursor(Qt::ClosedHandCursor);
    else if (KeyframeAt(pos) >= 0)      setCursor(Qt::PointingHandCursor);
    else if (m_hoverKnob)               setCursor(Qt::OpenHandCursor);
    else                                setCursor(Qt::ArrowCursor);
}

// ---- scrubbing --------------------------------------------------------------------------

void Scrubber::ScrubTo(float seconds)
{
    m_time = seconds;
    m_lastX = XFor(seconds);
    m_app.SeekTo(static_cast<double>(seconds));
    if (Following()) MoveSelectedKeyframe(seconds);
    update();
}

void Scrubber::MoveSelectedKeyframe(float seconds)
{
    KeyframeTimeline* timeline = SelectedTimeline();
    if (!timeline) return;
    if (m_selKey < 0 || m_selKey >= static_cast<int>(timeline->keyframes.size())) return;

    const Keyframe& current = timeline->keyframes[static_cast<size_t>(m_selKey)];
    if (current.time == seconds) return;   // nothing moved, so nothing to report

    // KeyframeTimeline keeps its vector sorted only through RemoveKeyframe / AddKeyframe.
    // Writing a new time into a keyframe still sitting in that vector leaves it out of
    // order and the evaluator's binary search reads the wrong segment, so the move goes
    // through a local copy and the index AddKeyframe hands back. Same sequence as
    // KeyframeDetail::RepositionSelected, for the same reason.
    Keyframe moved = current;
    moved.time = seconds;
    timeline->RemoveKeyframe(m_selKey);
    m_selKey = timeline->AddKeyframe(moved);

    RefreshKeyframes();
    m_app.OnParamChanged();
    emit KeyframeMoved(m_selParam, m_selKey);
}

// ---- input ------------------------------------------------------------------------------

void Scrubber::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (!(Duration() > 0.0)) return;

    setFocus(Qt::MouseFocusReason);

    const QPointF pos = event->position();
    const int key = KeyframeAt(pos);
    if (key >= 0 && key < m_keyTimes.size()) {
        // A diamond is a destination, not a handle: clicking it puts the playhead exactly
        // on that keyframe and hands the selection to it.
        m_time = m_keyTimes[key];
        m_lastX = XFor(m_time);
        m_selKey = key;
        m_app.SeekTo(static_cast<double>(m_time));
        emit KeyframeActivated(m_selParam, key);
        update();
        return;
    }

    // Shift arms follow mode for this drag alone, exactly as the outgoing slider read it.
    m_shiftHeld = event->modifiers().testFlag(Qt::ShiftModifier);
    m_dragging = true;
    SetKnobHovered(true);
    ScrubTo(TimeAt(pos.x()));
    RefreshCursor(pos);
}

void Scrubber::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();

    if (m_dragging) {
        ScrubTo(TimeAt(pos.x()));
        return;
    }

    m_hoverTrack = true;
    const int key = KeyframeAt(pos);
    const bool overKnob = (key < 0) && (std::abs(pos.x() - KnobX()) <= kKnobGrab)
                       && (pos.y() <= Groove().bottom() + kKnobGrab);
    if (key != m_hoverKey) { m_hoverKey = key; update(); }
    SetKnobHovered(overKnob);
    RefreshCursor(pos);
    update();
}

void Scrubber::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    m_shiftHeld = false;

    // Released outside the widget: the last clamped position stands, and the hover state
    // simply retires.
    const QPointF pos = event->position();
    const bool inside = rect().contains(pos.toPoint());
    m_hoverKey = inside ? KeyframeAt(pos) : -1;
    SetKnobHovered(inside && std::abs(pos.x() - KnobX()) <= kKnobGrab);
    m_hoverTrack = inside;
    RefreshCursor(pos);
    update();
}

void Scrubber::leaveEvent(QEvent* event)
{
    if (!m_dragging) {
        m_hoverTrack = false;
        m_hoverKey = -1;
        SetKnobHovered(false);
        update();
    }
    QWidget::leaveEvent(event);
}

void Scrubber::keyPressEvent(QKeyEvent* event)
{
    const double duration = Duration();
    if (!(duration > 0.0)) {
        QWidget::keyPressEvent(event);
        return;
    }

    // One frame per press where there is a frame rate to step by, a quarter second where
    // there is not; a page is a second either way. Shift is deliberately not a modifier
    // here, because Shift already means "carry the keyframe" on this widget.
    const double fps = Fps();
    const double step = (fps > 0.0) ? 1.0 / fps : 0.25;
    double target = static_cast<double>(m_time);

    switch (event->key()) {
    case Qt::Key_Left:     target -= step;  break;
    case Qt::Key_Right:    target += step;  break;
    case Qt::Key_PageDown: target -= 1.0;   break;
    case Qt::Key_PageUp:   target += 1.0;   break;
    case Qt::Key_Home:     target = 0.0;    break;
    case Qt::Key_End:      target = duration; break;
    default:
        QWidget::keyPressEvent(event);   // Space stays the application's play/pause
        return;
    }

    ScrubTo(static_cast<float>(std::clamp(target, 0.0, duration)));
    event->accept();
}

// ---- frame tick -------------------------------------------------------------------------

void Scrubber::Tick()
{
    bool dirty = RefreshKeyframes();

    if (!m_dragging) {
        const float now = static_cast<float>(m_app.GetDecoder().GetCurrentTime());
        const qreal x = XFor(now);
        // A repaint per frame for a playhead that has not moved a visible amount is work
        // nobody can see; half a pixel is the threshold at which it can be.
        if (m_lastX < 0.0 || std::abs(x - m_lastX) >= 0.5) {
            m_time = now;
            m_lastX = x;
            dirty = true;
        }
    }

    if (dirty) update();
}

// ---- paint ------------------------------------------------------------------------------

void Scrubber::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF groove = Groove();
    const qreal radius = kGrooveH * 0.5;
    const qreal knobX = KnobX();
    const bool live = m_hoverTrack || m_dragging || hasFocus();

    // The groove is recessed, like every other field in the product.
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::kInputFill);
    painter.drawRoundedRect(groove, radius, radius);

    // Filled to the playhead in the interactive accent: the one measure of how far in the
    // clip the user is that reads without being read.
    if (knobX > groove.left()) {
        QPainterPath clip;
        clip.addRoundedRect(groove, radius, radius);
        painter.save();
        painter.setClipPath(clip);
        painter.setBrush(Theme::kAccentPrimary);
        painter.drawRect(QRectF(groove.left(), groove.top(),
                                knobX - groove.left(), groove.height()));
        painter.restore();
    }

    QColor edge = Theme::kPanelBorder;
    if (live) edge.setAlpha(90);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(edge, 1.0));
    painter.drawRoundedRect(groove.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    if (hasFocus()) {
        QColor ring = Theme::kAccentPrimary;
        ring.setAlpha(150);
        painter.setPen(QPen(ring, 1.0));
        painter.drawRoundedRect(groove.adjusted(-3.0, -3.0, 3.0, 3.0),
                                radius + 3.0, radius + 3.0);
    }

    // ---- keyframe diamonds ---------------------------------------------------------------
    // They carry the Shader Parameters region hue rather than the transport's own, because
    // they are that region's data visiting this track; and they hang in their own lane, so
    // a second data layer never competes with the playhead for the same pixels.
    const bool following = m_followArmed && m_selParam >= 0 && m_selKey >= 0;
    for (int i = 0; i < m_keyTimes.size(); ++i) {
        const qreal kx = XFor(m_keyTimes[i]);
        const bool selected = (i == m_selKey);
        const bool hovered  = (i == m_hoverKey);
        const qreal size = kDiamond + (hovered ? 2.0 : 0.0) + (selected ? 1.0 : 0.0);

        QColor tether = Theme::kRegionParams;
        tether.setAlpha(selected ? 120 : 55);
        painter.setPen(QPen(tether, 1.0));
        painter.drawLine(QPointF(kx, groove.bottom() + 1.0), QPointF(kx, kLaneY - size));

        const QPolygonF diamond({QPointF(kx, kLaneY - size), QPointF(kx + size, kLaneY),
                                 QPointF(kx, kLaneY + size), QPointF(kx - size, kLaneY)});

        QColor body = Theme::kRegionParams;
        body.setAlpha(selected ? 255 : (hovered ? 235 : 160));
        painter.setPen(Qt::NoPen);
        painter.setBrush(body);
        painter.drawPolygon(diamond);

        if (selected) {
            // The ring says "this is the one the detail editor is editing"; filled solid,
            // it would still be only a brightness step away from its neighbours.
            const qreal outer = size + (following ? 4.5 : 3.0);
            QColor ringColour = Theme::kRegionParams;
            ringColour.setAlpha(following ? 230 : 130);
            painter.setPen(QPen(ringColour, following ? 1.6 : 1.2));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(QPolygonF({QPointF(kx, kLaneY - outer),
                                           QPointF(kx + outer, kLaneY),
                                           QPointF(kx, kLaneY + outer),
                                           QPointF(kx - outer, kLaneY)}));
        }
    }

    // ---- the knob ------------------------------------------------------------------------
    const qreal knobY = groove.center().y();
    const qreal grow = m_knobT * kKnobGrow;
    const qreal give = m_dragging ? kKnobGive : 0.0;
    const qreal knobR = kKnobR + grow - give;

    if (m_knobT > 0.0) {
        QColor halo = Theme::kAccentPrimary;
        halo.setAlpha(static_cast<int>(80.0 * m_knobT));
        painter.setPen(Qt::NoPen);
        painter.setBrush(halo);
        painter.drawEllipse(QPointF(knobX, knobY), knobR + 6.0, knobR + 6.0);
    }

    // Pressed, the knob inverts: the same object, taking the weight of the hand on it.
    painter.setPen(QPen(Theme::kAccentPrimary, 1.8));
    painter.setBrush(m_dragging ? QBrush(Theme::kAccentPrimary) : QBrush(Theme::kCanvas));
    painter.drawEllipse(QPointF(knobX, knobY), knobR, knobR);

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_dragging ? QBrush(Theme::kCanvas) : QBrush(Theme::kAccentPrimary));
    const qreal core = knobR * 0.45;
    painter.drawEllipse(QPointF(knobX, knobY), core, core);
}

// =========================================================================================
// TransportPanel
// =========================================================================================

TransportPanel::TransportPanel(Application& app, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
{
    setObjectName(QStringLiteral("TransportPanel"));

    // The application stylesheet paints every QWidget with the canvas, which would lay an
    // opaque black slab over this dock's glass. Named containers opt out; the controls keep
    // their own fills. The clock is the one place monospace is right in this panel: the
    // digits tick in place and must not reflow as they do.
    setStyleSheet(QStringLiteral(
        "QWidget#TransportPanel, QWidget#TransportStack, QWidget#TransportPage,"
        "QWidget#TransportControls, QWidget#TransportClockBlock, QWidget#TransportAudio,"
        "QWidget#TransportGenCustom, QWidget#TransportScrubber { background: transparent; }"
        "QLabel#TransportClock { font-family: %1; font-size: %2pt; font-weight: bold;"
        " color: %3; }"
        "QLabel#TransportDuration { font-family: %1; font-size: %4pt; color: %5; }")
        .arg(QString::fromLatin1(Theme::kFontMono))
        .arg(kClockPoints)
        .arg(Theme::kTextPrimary.name())
        .arg(Theme::kFontSizeBody)
        .arg(Theme::kTextSecondary.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kSpaceUnit);

    BuildTrack(layout);
    BuildControls(layout);
    layout->addStretch(1);

    SyncMuteButton();
    SyncResolution();
    SyncClock();
}

void TransportPanel::BuildTrack(QVBoxLayout* layout)
{
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("TransportStack"));

    // ---- the scrubbing track (index kPageScrub) -----------------------------------------
    m_scrubber = new Scrubber(m_app, m_stack);
    m_scrubber->setToolTip(tr("Drag to scrub. Click a diamond to seek to that keyframe.\n"
                              "Hold Shift while scrubbing to carry the selected keyframe "
                              "with the playhead."));
    connect(m_scrubber, &Scrubber::KeyframeMoved, this, [this](int param, int key) {
        m_selKey = key;
        emit KeyframeMoved(param, key);
    });
    connect(m_scrubber, &Scrubber::KeyframeActivated, this, [this](int param, int key) {
        m_selKey = key;
        emit KeyframeActivated(param, key);
    });
    m_stack->addWidget(m_scrubber);

    // ---- live capture (index kPageLive) --------------------------------------------------
    auto* livePage = new QWidget(m_stack);
    livePage->setObjectName(QStringLiteral("TransportPage"));
    BuildLivePage(livePage);
    m_stack->addWidget(livePage);

    // ---- generative (index kPageGenerative) ----------------------------------------------
    auto* genPage = new QWidget(m_stack);
    genPage->setObjectName(QStringLiteral("TransportPage"));
    BuildGenerativePage(genPage);
    m_stack->addWidget(genPage);

    // ---- nothing loaded (index kPageIdle) ------------------------------------------------
    auto* idlePage = new QWidget(m_stack);
    idlePage->setObjectName(QStringLiteral("TransportPage"));
    BuildIdlePage(idlePage);
    m_stack->addWidget(idlePage);

    m_stack->setCurrentIndex(kPageIdle);
    layout->addWidget(m_stack);
}

void TransportPanel::BuildLivePage(QWidget* page)
{
    auto* row = new QHBoxLayout(page);
    row->setContentsMargins(0, Theme::kSpaceUnit, 0, 0);
    row->setSpacing(Theme::kSpaceUnit);

    auto* badge = new QLabel(tr("LIVE"), page);
    badge->setObjectName(QStringLiteral("StatusError"));
    badge->setToolTip(tr("A camera or stream is feeding the pipeline. There is nothing to "
                         "scrub: the source has no end."));
    row->addWidget(badge);

    m_liveClock = new QLabel(page);
    m_liveClock->setObjectName(QStringLiteral("TransportClock"));
    m_liveClock->setToolTip(tr("Elapsed since the capture opened (wall clock)."));
    row->addWidget(m_liveClock);

    row->addStretch(1);

    auto* stop = new QPushButton(tr("Stop Capture"), page);
    stop->setObjectName(QStringLiteral("Danger"));
    stop->setCursor(Qt::PointingHandCursor);
    stop->setToolTip(tr("Close the live source and release the device."));
    connect(stop, &QPushButton::clicked, this, [this] { m_app.CloseVideo(); });
    row->addWidget(stop);
}

void TransportPanel::BuildGenerativePage(QWidget* page)
{
    auto* row = new QHBoxLayout(page);
    row->setContentsMargins(0, Theme::kSpaceUnit, 0, 0);
    row->setSpacing(Theme::kSpaceUnit);

    auto* label = new QLabel(tr("Output"), page);
    row->addWidget(label);

    m_genPreset = new QComboBox(page);
    for (const ResPreset& preset : kResPresets) {
        m_genPreset->addItem(QString::fromLatin1(preset.label));
    }
    m_genPreset->setToolTip(tr("Output resolution the shader renders at while no video "
                               "is open."));
    connect(m_genPreset, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        if (index == kResCustom) {
            m_genCustomLatched = true;    // reveal the pair and leave the size alone
        } else {
            m_genCustomLatched = false;
            AppConfig& cfg = m_app.GetConfig();
            cfg.generativeWidth  = kResPresets[index].w;
            cfg.generativeHeight = kResPresets[index].h;
            m_app.ApplyGenerativeResolution();
        }
        SyncResolution();
    });
    row->addWidget(m_genPreset);

    // The custom pair appears only when no preset matches, exactly as the outgoing inline
    // drags did.
    m_genCustom = new QWidget(page);
    m_genCustom->setObjectName(QStringLiteral("TransportGenCustom"));
    auto* custom = new QHBoxLayout(m_genCustom);
    custom->setContentsMargins(0, 0, 0, 0);
    custom->setSpacing(Theme::kSpaceUnit / 2);

    m_genWidth = new QSpinBox(m_genCustom);
    m_genWidth->setRange(1, 7680);
    m_genWidth->setKeyboardTracking(false);
    m_genWidth->setToolTip(tr("Output width in pixels."));
    custom->addWidget(m_genWidth);

    auto* by = new QLabel(QStringLiteral("x"), m_genCustom);
    by->setObjectName(QStringLiteral("Caption"));
    custom->addWidget(by);

    m_genHeight = new QSpinBox(m_genCustom);
    m_genHeight->setRange(1, 4320);
    m_genHeight->setKeyboardTracking(false);
    m_genHeight->setToolTip(tr("Output height in pixels."));
    custom->addWidget(m_genHeight);

    const auto applyCustom = [this] {
        AppConfig& cfg = m_app.GetConfig();
        cfg.generativeWidth  = m_genWidth->value();
        cfg.generativeHeight = m_genHeight->value();
        m_app.ApplyGenerativeResolution();
    };
    connect(m_genWidth,  &QSpinBox::valueChanged, this, applyCustom);
    connect(m_genHeight, &QSpinBox::valueChanged, this, applyCustom);
    row->addWidget(m_genCustom);

    row->addStretch(1);

    auto* elapsed = new QLabel(tr("Elapsed"), page);
    elapsed->setObjectName(QStringLiteral("Caption"));
    row->addWidget(elapsed);

    m_genClock = new QLabel(page);
    m_genClock->setObjectName(QStringLiteral("TransportClock"));
    m_genClock->setToolTip(tr("Time the shader has been running (wall clock)."));
    row->addWidget(m_genClock);
}

void TransportPanel::BuildIdlePage(QWidget* page)
{
    // Not a disabled transport: a line that says what this dock is waiting for.
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, Theme::kSpaceUnit, 0, 0);
    column->setSpacing(2);

    auto* heading = new QLabel(tr("Nothing to play yet"), page);
    QFont headingFont(QString::fromLatin1(Theme::kFontUi));
    headingFont.setPointSize(Theme::kFontSizePanelTitle);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    column->addWidget(heading);

    auto* hint = new QLabel(tr("Open a video or a live capture, or activate a shader, and "
                               "the timeline appears here."), page);
    hint->setObjectName(QStringLiteral("Caption"));
    hint->setWordWrap(true);
    column->addWidget(hint);

    column->addStretch(1);
}

void TransportPanel::BuildControls(QVBoxLayout* layout)
{
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("TransportControls"));

    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Theme::kSpaceUnit);

    m_play = new QPushButton(tr("Play"), bar);
    m_play->setObjectName(QStringLiteral("Primary"));
    m_play->setMinimumWidth(88);
    m_play->setCursor(Qt::PointingHandCursor);
    m_play->setToolTip(tr("Play or pause (Space)."));
    connect(m_play, &QPushButton::clicked, this, [this] {
        m_app.TogglePlayback();
        SyncPlayButton();
    });
    row->addWidget(m_play);

    m_stop = new QPushButton(tr("Stop"), bar);
    m_stop->setMinimumWidth(68);
    m_stop->setCursor(Qt::PointingHandCursor);
    m_stop->setToolTip(tr("Stop playback and return to the start."));
    connect(m_stop, &QPushButton::clicked, this, [this] {
        m_app.Stop();
        SyncPlayButton();
    });
    row->addWidget(m_stop);

    // ---- clock ---------------------------------------------------------------------------
    m_clockBlock = new QWidget(bar);
    m_clockBlock->setObjectName(QStringLiteral("TransportClockBlock"));
    auto* clockRow = new QHBoxLayout(m_clockBlock);
    clockRow->setContentsMargins(Theme::kSpaceUnit, 0, 0, 0);
    clockRow->setSpacing(Theme::kSpaceUnit / 2);

    m_clock = new QLabel(m_clockBlock);
    m_clock->setObjectName(QStringLiteral("TransportClock"));
    m_clock->setToolTip(tr("Playhead position."));
    clockRow->addWidget(m_clock);

    m_duration = new QLabel(m_clockBlock);
    m_duration->setObjectName(QStringLiteral("TransportDuration"));
    m_duration->setToolTip(tr("Total length of the clip."));
    clockRow->addWidget(m_duration);

    m_units = new QToolButton(m_clockBlock);
    m_units->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_units->setCursor(Qt::PointingHandCursor);
    connect(m_units, &QToolButton::clicked, this, [this] {
        AppConfig& cfg = m_app.GetConfig();
        cfg.timeDisplayFrames = !cfg.timeDisplayFrames;
        m_app.SaveConfig();
        SyncClock();
        m_scrubber->update();
    });
    clockRow->addWidget(m_units);
    m_clockBlock->setVisible(false);      // the idle page it opens on carries no clock
    row->addWidget(m_clockBlock);

    row->addStretch(1);

    // ---- keyframe follow -----------------------------------------------------------------
    m_follow = new QPushButton(tr("Follow"), bar);
    m_follow->setCheckable(true);
    m_follow->setCursor(Qt::PointingHandCursor);
    m_follow->setEnabled(false);
    m_follow->setToolTip(
        tr("Select a keyframe in Shader Parameters to link it to the playhead."));
    connect(m_follow, &QPushButton::toggled, this, [this](bool on) {
        m_scrubber->SetFollowArmed(on);
        SyncFollowButton();
    });
    row->addWidget(m_follow);

    // ---- audio ---------------------------------------------------------------------------
    m_audioRow = new QWidget(bar);
    m_audioRow->setObjectName(QStringLiteral("TransportAudio"));
    auto* audio = new QHBoxLayout(m_audioRow);
    audio->setContentsMargins(Theme::kSpaceUnit, 0, 0, 0);
    audio->setSpacing(Theme::kSpaceUnit);

    m_mute = new QPushButton(tr("Mute"), m_audioRow);
    m_mute->setMinimumWidth(72);
    m_mute->setCursor(Qt::PointingHandCursor);
    connect(m_mute, &QPushButton::clicked, this, [this] {
        m_app.SetAudioMute(!m_app.GetConfig().muteAudio);
        m_app.SaveConfig();
        SyncMuteButton();
    });
    audio->addWidget(m_mute);

    m_volume = new QSlider(Qt::Horizontal, m_audioRow);
    m_volume->setRange(0, kVolumeSteps);
    m_volume->setFixedWidth(kVolumeWidth);
    connect(m_volume, &QSlider::valueChanged, this, [this](int value) {
        const float volume = static_cast<float>(value) / static_cast<float>(kVolumeSteps);
        m_app.SetAudioVolume(volume);
        m_volume->setToolTip(tr("Volume %1%").arg(value));
        // One config write per gesture rather than one per frame of a drag: the wheel and
        // the arrow keys commit immediately, a drag commits when the hand lets go.
        if (!m_volume->isSliderDown()) m_app.SaveConfig();
    });
    connect(m_volume, &QSlider::sliderReleased, this, [this] { m_app.SaveConfig(); });
    audio->addWidget(m_volume);

    m_audioRow->setVisible(false);
    row->addWidget(m_audioRow);

    layout->addWidget(bar);
}

// ---- selection --------------------------------------------------------------------------

void TransportPanel::SetKeyframeSelection(int paramIndex, int keyframeIndex)
{
    m_selParam = paramIndex;
    m_selKey = keyframeIndex;
    m_scrubber->SetKeyframeSelection(paramIndex, keyframeIndex);

    // A toggle pointing at nothing is a trap: the next keyframe selected would start moving
    // under a scrub the user never armed. The selection resetting disarms it.
    if (paramIndex < 0 || keyframeIndex < 0) {
        const QSignalBlocker block(m_follow);
        m_follow->setChecked(false);
        m_scrubber->SetFollowArmed(false);
    }
    SyncFollowButton();
}

// ---- per-frame sync ---------------------------------------------------------------------

void TransportPanel::Tick()
{
    SyncPage();
    SyncRenderLock();
    SyncResolutionLock();
    SyncPlayButton();
    SyncAudioRow();
    SyncFollowButton();
    SyncClock();

    if (m_page == kPageScrub) m_scrubber->Tick();
}

void TransportPanel::SyncPage()
{
    VideoDecoder& decoder = m_app.GetDecoder();
    const ShaderPreset* active = m_app.GetShaderManager().GetActivePreset();

    // The same split MainWindow::Tick makes for the viewport, and it has to be the same or
    // the dock contradicts the picture: any active shader draws with no video open and its
    // wall clock runs, so any active shader gets the clock and the pause verb. Gating this
    // on isGenerative left an audio visualiser rendering and animating above a panel saying
    // there was nothing to play, with Play and Stop greyed out.
    int page = kPageIdle;
    if (decoder.IsOpen() && decoder.IsLiveCapture())      page = kPageLive;
    else if (decoder.IsOpen())                            page = kPageScrub;
    else if (active != nullptr)                           page = kPageGenerative;

    if (page == m_page) return;
    m_page = page;
    m_stack->setCurrentIndex(page);

    const bool havePicture = (page != kPageIdle) && !m_app.IsOfflineRender();
    m_play->setEnabled(havePicture);
    m_stop->setEnabled(havePicture);

    // The clock block belongs to the seekable track; the other pages carry their own.
    m_clockBlock->setVisible(page == kPageScrub);

    if (page == kPageGenerative) SyncResolution();
}

void TransportPanel::SyncRenderLock()
{
    // While a render is walking the file into the encoder, the transport is not what is
    // driving playback, and Application ignores it. Greying it out says so before the user
    // presses something that appears to do nothing.
    const bool locked = m_app.IsOfflineRender();
    if (locked == m_renderLocked) return;
    m_renderLocked = locked;

    m_play->setEnabled(!locked && m_page != kPageIdle);
    m_stop->setEnabled(!locked && m_page != kPageIdle);
    m_scrubber->setEnabled(!locked);

    m_stop->setToolTip(locked ? RenderLockHint()
                              : tr("Stop playback and return to the start."));
    m_scrubber->setToolTip(locked ? RenderLockHint() : QString());
    ApplyPlayButton();
}

void TransportPanel::SyncResolutionLock()
{
    // FFmpeg sized the encoder from this resolution when the recording started, and every
    // frame after a change arrives at a size it refuses. This control is the one place
    // that can say so, and a greyed control carrying the reason reads better than a
    // recording that silently stops accepting frames.
    const bool locked = m_app.GetEncoder().IsRecording();
    if (locked == m_resolutionLocked) return;
    m_resolutionLocked = locked;

    m_genPreset->setEnabled(!locked);
    m_genWidth->setEnabled(!locked);
    m_genHeight->setEnabled(!locked);

    const QString hint = tr("The output resolution is fixed while recording.");
    m_genPreset->setToolTip(locked ? hint
                                   : tr("Output resolution the shader renders at while no "
                                        "video is open."));
    m_genWidth->setToolTip(locked ? hint : tr("Output width in pixels."));
    m_genHeight->setToolTip(locked ? hint : tr("Output height in pixels."));
}

void TransportPanel::ApplyPlayButton()
{
    m_play->setText(m_playing ? tr("Pause") : tr("Play"));
    m_play->setToolTip(m_renderLocked ? RenderLockHint()
                       : m_playing    ? tr("Pause playback (Space).")
                                      : tr("Play (Space)."));
}

void TransportPanel::SyncPlayButton()
{
    const bool playing = (m_app.GetPlaybackState() == PlaybackState::Playing);
    if (playing == m_playing) return;
    m_playing = playing;
    ApplyPlayButton();
}

void TransportPanel::SyncClock()
{
    VideoDecoder& decoder = m_app.GetDecoder();

    if (m_page == kPageLive) {
        m_liveClock->setText(ClockText(m_app.GetPlaybackTime()));
        return;
    }
    if (m_page == kPageGenerative) {
        m_genClock->setText(ClockText(m_app.GetPlaybackTime()));
        return;
    }

    const QString unitText = FrameMode() ? tr("frames") : tr("sec");
    if (m_units->text() != unitText) {
        m_units->setText(unitText);
        m_units->setToolTip(FrameMode()
            ? tr("Showing frame numbers. Click for seconds.")
            : tr("Showing seconds. Click for frame numbers."));
    }

    const QString clock = TimeText(decoder.IsOpen() ? decoder.GetCurrentTime() : 0.0);
    if (clock != m_clockText) {
        m_clockText = clock;
        m_clock->setText(clock);
    }

    const QString duration = DurationText();
    if (duration != m_durationText) {
        m_durationText = duration;
        m_duration->setText(duration);
    }
}

void TransportPanel::SyncAudioRow()
{
    const bool hasAudio = m_app.GetDecoder().HasAudio();
    if (hasAudio == m_hasAudio) return;
    m_hasAudio = hasAudio;

    if (hasAudio) {
        const AppConfig& cfg = m_app.GetConfig();
        const QSignalBlocker block(m_volume);
        m_volume->setValue(static_cast<int>(std::lround(
            std::clamp(cfg.audioVolume, 0.0f, 1.0f) * kVolumeSteps)));
        m_volume->setToolTip(tr("Volume %1%").arg(m_volume->value()));
        SyncMuteButton();
    }
    m_audioRow->setVisible(hasAudio);
}

void TransportPanel::SyncFollowButton()
{
    const bool enabled = (m_page == kPageScrub) && m_selParam >= 0 && m_selKey >= 0;
    const bool checked = m_follow->isChecked();

    if (!enabled && checked) {
        const QSignalBlocker block(m_follow);
        m_follow->setChecked(false);
        m_scrubber->SetFollowArmed(false);
    }
    if (enabled == m_followEnabled) return;
    m_followEnabled = enabled;

    m_follow->setEnabled(enabled);
    m_follow->setToolTip(enabled
        ? tr("Link the selected keyframe to the playhead: scrubbing carries it with you.\n"
             "Holding Shift while scrubbing does the same without arming anything.")
        : tr("Select a keyframe in Shader Parameters to link it to the playhead."));
}

void TransportPanel::SyncMuteButton()
{
    const bool muted = m_app.GetConfig().muteAudio;
    m_mute->setText(muted ? tr("Muted") : tr("Mute"));
    m_mute->setToolTip(muted ? tr("Audio is muted. Click to hear it again.")
                             : tr("Silence playback audio."));

    // Muted is a state worth seeing across the room, so it takes the danger fill rather
    // than the accent every other toggled control uses.
    const QString name = muted ? QStringLiteral("Danger") : QString();
    if (m_mute->objectName() != name) {
        m_mute->setObjectName(name);
        m_mute->style()->unpolish(m_mute);
        m_mute->style()->polish(m_mute);
    }
}

void TransportPanel::SyncResolution()
{
    const AppConfig& cfg = m_app.GetConfig();

    int current = kResCustom;
    for (int i = 0; i < kResCustom; ++i) {
        if (kResPresets[i].w == cfg.generativeWidth
            && kResPresets[i].h == cfg.generativeHeight) {
            current = i;
            break;
        }
    }
    const bool custom = (current == kResCustom) || m_genCustomLatched;
    if (custom) current = kResCustom;

    {
        const QSignalBlocker block(m_genPreset);
        m_genPreset->setCurrentIndex(current);
    }
    {
        const QSignalBlocker blockW(m_genWidth);
        const QSignalBlocker blockH(m_genHeight);
        m_genWidth->setValue(cfg.generativeWidth);
        m_genHeight->setValue(cfg.generativeHeight);
    }
    m_genCustom->setVisible(custom);
}

// ---- display units ----------------------------------------------------------------------

bool TransportPanel::FrameMode() const { return m_app.GetConfig().timeDisplayFrames; }
double TransportPanel::Fps() const     { return m_app.GetDecoder().GetFPS(); }

QString TransportPanel::TimeText(double seconds) const
{
    const double fps = Fps();
    if (FrameMode() && fps > 0.0) {
        return tr("f %1").arg(static_cast<long long>(std::llround(seconds * fps)));
    }
    return ClockText(seconds);
}

QString TransportPanel::DurationText() const
{
    VideoDecoder& decoder = m_app.GetDecoder();
    const double fps = Fps();
    const long long frames = static_cast<long long>(decoder.GetFrameCount());

    if (FrameMode() && fps > 0.0 && frames > 0) {
        return tr("/ %1 f").arg(frames - 1);
    }
    return tr("/ %1").arg(ClockText(decoder.GetDuration()));
}

}  // namespace SP
