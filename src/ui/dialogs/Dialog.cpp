#include "ui/dialogs/Dialog.h"

#include "ui/Theme.h"

#include <QAbstractAnimation>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPoint>
#include <QPushButton>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace SP {

namespace {

// One step above a panel's lift: wider, darker and dropped further, so a dialog reads as
// floating over the window rather than as another island in it.
constexpr int kShadowBlur  = 48;
constexpr int kShadowAlpha = 200;
constexpr int kShadowDrop  = 10;

// The transparent margin the shadow is painted into. Anything less and the bloom is clipped
// to the window edge, which reads as a hard grey band rather than as lift.
constexpr int kShadowMargin = 26;

constexpr int kEntranceRise = 20;   // px travelled on entry, settling with an overshoot
constexpr int kHairlineHeight = 2;

}  // namespace

// A translucent token has nothing behind it in a top-level window, so it is composited over
// the canvas by hand. Same reason QMenu and QToolTip carry flat colours in the stylesheet.
QColor Dialog::Composite(const QColor& over, const QColor& base)
{
    const qreal a = over.alphaF();
    return QColor::fromRgbF(over.redF()   * a + base.redF()   * (1.0 - a),
                            over.greenF() * a + base.greenF() * (1.0 - a),
                            over.blueF()  * a + base.blueF()  * (1.0 - a));
}

QString Dialog::Rgba(const QColor& c)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

Dialog::Dialog(const QString& title, const QColor& hue, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("Dialog"));
    setWindowTitle(title);
    setModal(true);
    setAttribute(Qt::WA_TranslucentBackground);

    // The application stylesheet paints QDialog with the canvas, which over a translucent
    // frameless window would be an opaque black rectangle with square corners: the radius
    // and the shadow both live on the island inside, so the window itself carries nothing.
    setStyleSheet(
        QStringLiteral(
            "QDialog#Dialog { background: transparent; }"
            "QWidget#DialogHeader { background: transparent; }"
            "QFrame#DialogFrame { background: %1; border: 1px solid %2;"
            " border-radius: %3px; margin: 0px; }")
            .arg(Composite(Theme::kPanelFillRaised, Theme::kCanvas).name(),
                 Rgba(Theme::kPanelBorder))
            .arg(Theme::kRadiusDialog));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kShadowMargin, kShadowMargin, kShadowMargin, kShadowMargin);
    outer->setSpacing(0);

    m_frame = new QFrame(this);
    m_frame->setObjectName(QStringLiteral("DialogFrame"));
    outer->addWidget(m_frame);

    auto* lift = new QGraphicsDropShadowEffect(m_frame);
    lift->setColor(QColor(0, 0, 0, kShadowAlpha));
    lift->setBlurRadius(kShadowBlur);
    lift->setOffset(0.0, kShadowDrop);
    m_frame->setGraphicsEffect(lift);

    auto* island = new QVBoxLayout(m_frame);
    island->setContentsMargins(Theme::kSpaceUnit * 2 + 4, Theme::kSpaceUnit + 4,
                               Theme::kSpaceUnit * 2 + 4, Theme::kSpaceUnit * 2 + 4);
    island->setSpacing(Theme::kSpaceUnit);

    // ---- identity hairline ---------------------------------------------------------------
    // Present at rest rather than on hover: a dialog is on screen for one decision and has no
    // time to be discovered under a pointer.
    auto* hairline = new QFrame(m_frame);
    hairline->setObjectName(QStringLiteral("DialogHairline"));
    hairline->setFixedHeight(kHairlineHeight);
    hairline->setStyleSheet(QStringLiteral("QFrame#DialogHairline { background: %1;"
                                           " border: none; border-radius: 1px; }")
                                .arg(hue.name()));
    island->addWidget(hairline);

    // ---- header ---------------------------------------------------------------------------
    m_header = new QWidget(m_frame);
    m_header->setObjectName(QStringLiteral("DialogHeader"));
    auto* headerRow = new QHBoxLayout(m_header);
    headerRow->setContentsMargins(0, Theme::kSpaceUnit / 2, 0, 0);
    headerRow->setSpacing(Theme::kSpaceUnit);

    m_title = new QLabel(title, m_header);
    m_title->setObjectName(QStringLiteral("PanelTitle"));
    m_title->setStyleSheet(QStringLiteral("QLabel#PanelTitle { color: %1; }").arg(hue.name()));
    headerRow->addWidget(m_title);
    headerRow->addStretch(1);

    auto* close = new QToolButton(m_header);
    close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    close->setIconSize(QSize(14, 14));
    close->setToolTip(tr("Close"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QToolButton::clicked, this, &QDialog::reject);
    headerRow->addWidget(close);

    island->addWidget(m_header);

    m_body = new QVBoxLayout;
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(Theme::kSpaceUnit);
    island->addLayout(m_body, 1);
}

QHBoxLayout* Dialog::AddButtonRow()
{
    m_body->addSpacing(Theme::kSpaceUnit);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Theme::kSpaceUnit);
    row->addStretch(1);
    m_body->addLayout(row);
    return row;
}

void Dialog::showEvent(QShowEvent* event)
{
    // Base first: QDialog centres the window on its parent from here, so the resting
    // position the entrance animates toward is only known afterwards.
    QDialog::showEvent(event);

    if (m_entered) return;
    m_entered = true;

    const int duration = Theme::Motion(Theme::kMotionBase);
    if (duration <= 0) {
        setWindowOpacity(1.0);   // reduced motion: the dialog is simply there
        return;
    }

    const QPoint resting = pos();
    setWindowOpacity(0.0);
    move(resting + QPoint(0, kEntranceRise));

    auto* entrance = new QParallelAnimationGroup(this);

    auto* fade = new QPropertyAnimation(this, "windowOpacity", entrance);
    fade->setDuration(duration);
    fade->setEasingCurve(Theme::kEaseStandard);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    entrance->addAnimation(fade);

    auto* rise = new QPropertyAnimation(this, "pos", entrance);
    rise->setDuration(duration);
    rise->setEasingCurve(Theme::kEaseEntrance);   // overshoot, then settle
    rise->setStartValue(resting + QPoint(0, kEntranceRise));
    rise->setEndValue(resting);
    entrance->addAnimation(rise);

    entrance->start(QAbstractAnimation::DeleteWhenStopped);
}

void Dialog::mousePressEvent(QMouseEvent* event)
{
    // The header is this window's title bar, so a press in it drags. startSystemMove hands
    // the drag to the compositor, which keeps snapping and the desktop's edges working.
    if (event->button() == Qt::LeftButton && m_header
        && m_header->geometry().contains(m_header->parentWidget()->mapFrom(
               this, event->position().toPoint()))) {
        if (QWindow* handle = windowHandle()) {
            handle->startSystemMove();
            event->accept();
            return;
        }
    }
    QDialog::mousePressEvent(event);
}

}  // namespace SP
