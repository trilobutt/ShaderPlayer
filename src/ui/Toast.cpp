#include "ui/Toast.h"

#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

#include <QEnterEvent>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QRect>
#include <QSize>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace SP {

namespace {

// The island's lift. One step below a dialog's: a notice floats over the window, but it is
// not the thing the window is waiting on.
constexpr qreal kLiftBlur       = 34.0;
constexpr qreal kLiftBlurHover  = 46.0;
constexpr int   kLiftAlpha      = 180;
constexpr int   kLiftAlphaHover = 215;
constexpr qreal kLiftDrop       = 8.0;
constexpr qreal kLiftDropHover  = 11.0;

// The transparent gutter the shadow is painted into. Anything less and the bloom is clipped
// to the window edge, which reads as a hard band rather than as lift. Every position in this
// file is computed for the island inside, so this is subtracted back out when the window is
// moved.
constexpr int kShadowMargin = 26;

// Travelled on entry, from the right: the notice arrives from off the corner it will rest in
// rather than materialising in place.
constexpr int kEntranceSlide = 28;

constexpr int kAccentBarWidth = 3;
constexpr int kTextMaxWidth   = 360;

// At most four islands at once. A burst of compile errors must not become a wall of windows,
// and four is already more than anyone reads.
constexpr int kMaxToasts = 4;

// A translucent token has nothing behind it in a top-level window, so it is composited over
// the canvas by hand. Here that is a legibility requirement rather than a formality: real
// glass over arbitrary video gives the text no guaranteed ground to sit on.
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

}  // namespace

// =======================================================================================
// Toast
// =======================================================================================

Toast::Toast(const QString& message, QWidget* owner)
    : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
{
    setObjectName(QStringLiteral("Toast"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);   // the whole island is the dismiss target

    // The application stylesheet paints every QWidget with the canvas, which over a
    // translucent frameless window would be an opaque black rectangle with square corners:
    // the radius, the fill and the lift all belong to the island inside.
    setStyleSheet(
        QStringLiteral(
            "QWidget#Toast { background: transparent; }"
            "QFrame#ToastFrame { background: %1; border: 1px solid %2;"
            " border-radius: %3px; }"
            "QFrame#ToastAccent { background: %4; border: none; border-radius: %5px; }")
            .arg(Composite(Theme::kPanelFillRaised, Theme::kCanvas).name(),
                 Rgba(Theme::kPanelBorder))
            .arg(Theme::kRadiusDialog)
            .arg(Theme::kAccentPrimary.name())
            .arg(kAccentBarWidth / 2 + 1));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kShadowMargin, kShadowMargin, kShadowMargin, kShadowMargin);
    outer->setSpacing(0);

    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("ToastFrame"));
    outer->addWidget(frame);

    m_lift = new QGraphicsDropShadowEffect(frame);
    m_lift->setOffset(0.0, kLiftDrop);
    frame->setGraphicsEffect(m_lift);

    auto* island = new QHBoxLayout(frame);
    island->setContentsMargins(Theme::kSpaceUnit + 4, Theme::kSpaceUnit + 2,
                               Theme::kSpaceUnit, Theme::kSpaceUnit + 2);
    island->setSpacing(Theme::kSpaceUnit + 2);

    // The one coloured element, and it is present at rest rather than on hover: cyan is this
    // product's global-state accent, and it is what marks the island as the application
    // speaking rather than a piece of the picture underneath.
    auto* accent = new QFrame(frame);
    accent->setObjectName(QStringLiteral("ToastAccent"));
    accent->setFixedWidth(kAccentBarWidth);
    island->addWidget(accent);

    auto* label = new QLabel(message, frame);
    label->setWordWrap(true);
    label->setMaximumWidth(kTextMaxWidth);
    island->addWidget(label, 1);

    // Click-to-dismiss is a convention, not an affordance, so the glyph is what makes it
    // discoverable. Pressing anywhere on the island does the same thing.
    auto* dismiss = new QToolButton(frame);
    dismiss->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    dismiss->setIconSize(QSize(12, 12));
    dismiss->setToolTip(tr("Dismiss"));
    dismiss->setCursor(Qt::PointingHandCursor);
    dismiss->setFocusPolicy(Qt::NoFocus);
    connect(dismiss, &QToolButton::clicked, this, &Toast::Leave);
    island->addWidget(dismiss, 0, Qt::AlignTop);

    m_move = new QPropertyAnimation(this, "pos", this);
    m_fade = new QPropertyAnimation(this, "windowOpacity", this);
    connect(m_fade, &QPropertyAnimation::finished, this, [this] {
        if (m_leaving) close();
    });

    m_liftAnim = new QVariantAnimation(this);
    m_liftAnim->setEasingCurve(Theme::kEaseStandard);
    connect(m_liftAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { ApplyLift(value.toReal()); });

    ApplyLift(0.0);

    // The stack lays out from the island's measured size, so it has to be final before the
    // first PlaceAt rather than at the first show.
    adjustSize();
}

void Toast::PlaceAt(const QPoint& resting)
{
    if (m_shown && m_resting == resting) return;
    m_resting = resting;

    if (!m_shown) {
        Enter();
        return;
    }

    // A restack: the islands below step toward the corner (or away from it, to make room for
    // an arrival) instead of snapping to their new slots.
    const int duration = Theme::kMotionBase;
    m_move->stop();
    m_move->setDuration(duration);
    m_move->setEasingCurve(Theme::kEaseStandard);
    m_move->setStartValue(pos());
    m_move->setEndValue(m_resting);
    m_move->start();
}

void Toast::Enter()
{
    m_shown = true;

    const int duration = Theme::kMotionBase;

    const QPoint from = m_resting + QPoint(kEntranceSlide, 0);
    move(from);
    setWindowOpacity(0.0);
    show();

    m_fade->stop();
    m_fade->setDuration(duration);
    m_fade->setEasingCurve(Theme::kEaseStandard);
    m_fade->setStartValue(0.0);
    m_fade->setEndValue(1.0);
    m_fade->start();

    m_move->stop();
    m_move->setDuration(duration);
    m_move->setEasingCurve(Theme::kEaseEntrance);   // past the corner, then a settle back
    m_move->setStartValue(from);
    m_move->setEndValue(m_resting);
    m_move->start();
}

void Toast::Leave()
{
    if (m_leaving) return;
    m_leaving = true;
    m_move->stop();
    m_liftAnim->stop();

    // Before the fade, so the gap closes underneath this one rather than after it.
    emit Dismissed(this);

    const int duration = Theme::kMotionBase;
    m_fade->stop();
    m_fade->setDuration(duration);
    m_fade->setEasingCurve(Theme::kEaseStandard);
    m_fade->setStartValue(windowOpacity());
    m_fade->setEndValue(0.0);
    m_fade->start();
}

void Toast::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    if (!m_leaving) AnimateLift(true);
}

void Toast::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (!m_leaving) AnimateLift(false);
}

void Toast::mousePressEvent(QMouseEvent* event)
{
    // Accepted rather than ignored: an unaccepted press would be delivered to the window
    // underneath, and dismissing a notice must not also click on the shader library.
    event->accept();
    Leave();
}

void Toast::AnimateLift(bool up)
{
    const qreal target = up ? 1.0 : 0.0;
    const qreal current = m_liftAnim->currentValue().isValid()
                              ? m_liftAnim->currentValue().toReal()
                              : (up ? 0.0 : 1.0);

    m_liftAnim->stop();
    m_liftAnim->setDuration(Theme::kMotionHover);
    m_liftAnim->setStartValue(current);
    m_liftAnim->setEndValue(target);
    m_liftAnim->start();
}

void Toast::ApplyLift(qreal t)
{
    const qreal blur  = kLiftBlur + (kLiftBlurHover - kLiftBlur) * t;
    const qreal drop  = kLiftDrop + (kLiftDropHover - kLiftDrop) * t;
    const qreal alpha = kLiftAlpha + (kLiftAlphaHover - kLiftAlpha) * t;

    m_lift->setBlurRadius(blur);
    m_lift->setOffset(0.0, drop);
    m_lift->setColor(QColor(0, 0, 0, static_cast<int>(std::lround(alpha))));
}

// =======================================================================================
// ToastStack
// =======================================================================================

ToastStack::ToastStack(QWidget* owner, QWidget* anchor)
    : QObject(owner), m_owner(owner), m_anchor(anchor)
{
    // A top-level does not follow its owner, and the anchor's corner moves for two unrelated
    // reasons: the window moving or resizing, and the docks around the central area being
    // dragged, hidden or resized. The first reaches the owner, the second only the anchor.
    if (m_owner != nullptr) m_owner->installEventFilter(this);
    if (m_anchor != nullptr && m_anchor != m_owner) m_anchor->installEventFilter(this);
}

bool ToastStack::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Move || event->type() == QEvent::Resize) Restack();
    return QObject::eventFilter(watched, event);
}

void ToastStack::Show(const QString& message, int durationMs)
{
    if (m_owner == nullptr) return;

    // Bounded. Leave() takes the oldest out of m_live as it emits, so this terminates.
    while (m_live.size() >= kMaxToasts) m_live.first()->Leave();

    auto* toast = new Toast(message, m_owner);
    connect(toast, &Toast::Dismissed, this, [this](Toast* leaving) {
        m_live.removeAll(leaving);
        Restack();
    });
    // The owner outliving its notices is the normal case; the reverse (the window going down
    // with notices up) would leave dangling entries here for the moment before this object
    // follows it, and Restack would walk them.
    connect(toast, &QObject::destroyed, this, [this, toast] { m_live.removeAll(toast); });

    m_live.append(toast);

    // Places every island, which both shows this one (its first PlaceAt is the entrance) and
    // steps the older ones up out of the corner it is arriving in.
    Restack();

    QTimer::singleShot(std::max(durationMs, 0), toast, [toast] { toast->Leave(); });
}

void ToastStack::Restack()
{
    if (m_live.isEmpty() || m_anchor == nullptr) return;

    // The central area, not the window: the corner has to be over the picture rather than
    // over the transport below it or the library beside it.
    const QRect area(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size());

    int islandBottom = area.bottom() - Theme::kPanelGap;
    for (int i = m_live.size() - 1; i >= 0; --i) {
        Toast* toast = m_live[i];
        const QSize window = toast->size();
        const int islandWidth  = window.width()  - kShadowMargin * 2;
        const int islandHeight = window.height() - kShadowMargin * 2;

        const int top = std::max(islandBottom - islandHeight, area.top() + Theme::kPanelGap);
        const int left = std::max(area.right() - Theme::kPanelGap - islandWidth,
                                  area.left() + Theme::kPanelGap);

        toast->PlaceAt(QPoint(left - kShadowMargin, top - kShadowMargin));

        // One spacing unit of visible canvas between islands: they are one group.
        islandBottom = top - Theme::kSpaceUnit;
    }
}

}  // namespace SP
