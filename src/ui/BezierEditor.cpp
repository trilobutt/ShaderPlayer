#include "ui/BezierEditor.h"

#include "ui/Theme.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace SP {

namespace {

// The plot is inset from the frame by more than a handle's radius, so a handle dragged to
// a corner is still drawn as a whole disc.
constexpr qreal kInset        = 15.0;
constexpr qreal kHandleRadius = 5.5;

// The grab target is wider than the disc: 9px each way is an 18px target, comfortably over
// the 12px floor, and it is what makes the handles feel caught rather than chased.
constexpr qreal kGrabRadius   = 9.0;

constexpr int kMinWidth  = 200;
constexpr int kMinHeight = 140;
constexpr int kHintWidth = 220;   // preferred; a wider plot only flattens the slope
constexpr int kHintHeight = 150;

qreal Clamp01(qreal v)
{
    return std::clamp(v, 0.0, 1.0);
}

}  // namespace

BezierEditor::BezierEditor(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("BezierEditor"));
    setMinimumSize(kMinWidth, kMinHeight);
    setMaximumHeight(kHintHeight + 20);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMouseTracking(true);   // the handles answer the pointer before the press
    setFocusPolicy(Qt::NoFocus);

    m_hoverAnim = new QVariantAnimation(this);
    m_hoverAnim->setEasingCurve(Theme::kEaseStandard);
    connect(m_hoverAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { m_hoverT = value.toReal(); update(); });
}

QSize BezierEditor::sizeHint() const
{
    return QSize(kHintWidth, kHintHeight);
}

void BezierEditor::SetCurve(InterpolationMode mode, const BezierHandles& handles)
{
    m_mode = mode;
    m_handles = handles;
    if (m_mode != InterpolationMode::CubicBezier) {
        m_dragging = -1;
        SetHovered(-1);
    }
    setCursor(m_mode == InterpolationMode::CubicBezier ? Qt::ArrowCursor
                                                       : Qt::PointingHandCursor);
    update();
}

// ---------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------

QRectF BezierEditor::Plot() const
{
    return QRectF(rect()).adjusted(kInset, kInset, -kInset, -kInset);
}

QPointF BezierEditor::ToWidget(qreal x, qreal y) const
{
    const QRectF plot = Plot();
    return QPointF(plot.left() + x * plot.width(),
                   plot.bottom() - y * plot.height());   // y is up, as a timing curve reads
}

QPointF BezierEditor::HandlePos(int handle) const
{
    return (handle == 0) ? ToWidget(m_handles.outX, m_handles.outY)
                         : ToWidget(m_handles.inX, m_handles.inY);
}

int BezierEditor::HandleAt(const QPointF& pos) const
{
    if (m_mode != InterpolationMode::CubicBezier) return -1;

    int best = -1;
    qreal bestDist = kGrabRadius * kGrabRadius;
    for (int handle = 0; handle < 2; ++handle) {
        const QPointF delta = pos - HandlePos(handle);
        const qreal dist = delta.x() * delta.x() + delta.y() * delta.y();
        if (dist <= bestDist) { bestDist = dist; best = handle; }
    }
    return best;
}

void BezierEditor::SetHovered(int handle)
{
    if (m_hovered == handle) return;
    m_hovered = handle;

    // A drag tracks the cursor and must not be animated; only the arrival and departure of
    // the hover state is, and Theme::Motion collapses that to nothing under reduced motion.
    const int duration = Theme::Motion(Theme::kMotionHover);
    const qreal target = (handle >= 0) ? 1.0 : 0.0;
    m_hoverAnim->stop();
    if (duration <= 0) {
        m_hoverT = target;
        update();
        return;
    }
    m_hoverAnim->setDuration(duration);
    m_hoverAnim->setStartValue(m_hoverT);
    m_hoverAnim->setEndValue(target);
    m_hoverAnim->start();
}

void BezierEditor::RefreshCursor(const QPointF& pos)
{
    if (m_mode != InterpolationMode::CubicBezier) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    if (m_dragging >= 0)          setCursor(Qt::ClosedHandCursor);
    else if (HandleAt(pos) >= 0)  setCursor(Qt::OpenHandCursor);
    else                          setCursor(Qt::ArrowCursor);
}

void BezierEditor::ApplyDrag(const QPointF& pos)
{
    const QRectF plot = Plot();
    if (plot.width() <= 0.0 || plot.height() <= 0.0) return;

    const QPointF centre = pos + m_grab;
    // Both axes are clamped to the unit square. An overshooting handle would evaluate fine
    // but would be drawn outside the plot, and a control the user cannot see is not one
    // they can take back.
    const qreal x = Clamp01((centre.x() - plot.left()) / plot.width());
    const qreal y = Clamp01((plot.bottom() - centre.y()) / plot.height());

    if (m_dragging == 0) {
        m_handles.outX = static_cast<float>(x);
        m_handles.outY = static_cast<float>(y);
    } else {
        m_handles.inX = static_cast<float>(x);
        m_handles.inY = static_cast<float>(y);
    }
    update();
    emit HandlesMoved(m_handles);
}

// ---------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------

void BezierEditor::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (m_mode != InterpolationMode::CubicBezier) {
        emit CustomCurveRequested();
        return;
    }

    const int handle = HandleAt(event->position());
    if (handle < 0) return;   // a stray press leaves the curve alone

    m_dragging = handle;
    m_grab = HandlePos(handle) - event->position();
    SetHovered(handle);
    RefreshCursor(event->position());
    update();
}

void BezierEditor::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging >= 0) {
        ApplyDrag(event->position());
        return;
    }
    SetHovered(HandleAt(event->position()));
    RefreshCursor(event->position());
}

void BezierEditor::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = -1;
    SetHovered(HandleAt(event->position()));
    RefreshCursor(event->position());
    update();
}

void BezierEditor::leaveEvent(QEvent* event)
{
    if (m_dragging < 0) SetHovered(-1);
    QWidget::leaveEvent(event);
}

// ---------------------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------------------

void BezierEditor::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // Recessed, like every other field in the product: darker than the panel hosting it,
    // tight radius, no lift.
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::kInputFill);
    painter.drawRoundedRect(frame, Theme::kRadiusControl, Theme::kRadiusControl);

    QColor grid = Theme::kPanelBorder;
    grid.setAlpha(26);
    painter.setPen(QPen(grid, 1.0));
    for (int i = 1; i < 4; ++i) {
        const qreal t = i / 4.0;
        painter.drawLine(ToWidget(t, 0.0), ToWidget(t, 1.0));
        painter.drawLine(ToWidget(0.0, t), ToWidget(1.0, t));
    }

    // The straight line the curve is being measured against.
    QColor reference = Theme::kTextPrimary;
    reference.setAlpha(48);
    painter.setPen(QPen(reference, 1.0, Qt::DashLine));
    painter.drawLine(ToWidget(0.0, 0.0), ToWidget(1.0, 1.0));

    painter.setPen(QPen(Theme::kPanelBorder, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frame, Theme::kRadiusControl, Theme::kRadiusControl);

    // ---- the curve -------------------------------------------------------------------
    float cx1 = 0.0f, cy1 = 0.0f, cx2 = 1.0f, cy2 = 1.0f;
    if (m_mode == InterpolationMode::EaseInOut) {
        cx1 = kSmoothstepHandles.outX; cy1 = kSmoothstepHandles.outY;
        cx2 = kSmoothstepHandles.inX;  cy2 = kSmoothstepHandles.inY;
    } else if (m_mode == InterpolationMode::CubicBezier) {
        cx1 = m_handles.outX; cy1 = m_handles.outY;
        cx2 = m_handles.inX;  cy2 = m_handles.inY;
    }

    QPainterPath curve(ToWidget(0.0, 0.0));
    curve.cubicTo(ToWidget(cx1, cy1), ToWidget(cx2, cy2), ToWidget(1.0, 1.0));

    // The panel's own hue, with a wide translucent pass under it for the bloom: the light
    // in this widget is colour, not a grey fill.
    QColor bloom = Theme::kRegionParams;
    bloom.setAlpha(55);
    painter.setPen(QPen(bloom, 6.0, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(curve);
    painter.setPen(QPen(Theme::kRegionParams, 2.2, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(curve);

    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::kTextPrimary);
    painter.drawEllipse(ToWidget(0.0, 0.0), 3.0, 3.0);
    painter.drawEllipse(ToWidget(1.0, 1.0), 3.0, 3.0);

    if (m_mode != InterpolationMode::CubicBezier) {
        // The one thing a fixed curve has to say: it can become an editable one.
        QFont hint(QString::fromLatin1(Theme::kFontUi));
        hint.setPointSize(Theme::kFontSizeCaption);
        painter.setFont(hint);
        painter.setPen(Theme::kTextSecondary);
        painter.drawText(rect().adjusted(6, 0, -6, -5),
                         Qt::AlignBottom | Qt::AlignHCenter,
                         tr("Click to shape this curve"));
        return;
    }

    // ---- handles ---------------------------------------------------------------------
    for (int handle = 0; handle < 2; ++handle) {
        const QPointF anchor = (handle == 0) ? ToWidget(0.0, 0.0) : ToWidget(1.0, 1.0);
        const QPointF pos = HandlePos(handle);
        const bool live = (m_dragging == handle)
                       || (m_dragging < 0 && m_hovered == handle);
        const qreal grow = live ? m_hoverT * 2.2 : 0.0;

        QColor tangent = Theme::kAccentPrimary;
        tangent.setAlpha(live ? 200 : 110);
        painter.setPen(QPen(tangent, 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(anchor, pos);

        if (live && m_hoverT > 0.0) {
            QColor halo = Theme::kAccentPrimary;
            halo.setAlpha(static_cast<int>(75.0 * m_hoverT));
            painter.setPen(Qt::NoPen);
            painter.setBrush(halo);
            const qreal r = kHandleRadius + 5.0 + grow;
            painter.drawEllipse(pos, r, r);
        }

        QColor disc = Theme::kAccentPrimary;
        disc.setAlpha(live ? 255 : 215);
        painter.setPen(QPen(disc, 1.8));
        painter.setBrush(Theme::kCanvas);
        painter.drawEllipse(pos, kHandleRadius + grow, kHandleRadius + grow);

        painter.setPen(Qt::NoPen);
        painter.setBrush(disc);
        const qreal core = (kHandleRadius + grow) * 0.46;
        painter.drawEllipse(pos, core, core);
    }
}

}  // namespace SP
