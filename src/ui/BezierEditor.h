#pragma once

// BezierEditor.h — the timing curve of one keyframe, drawn and dragged directly.
//
// The widget is the control: there are no numeric handle fields anywhere, so the curve has
// to be legible at rest and grabbable without a tooltip explaining it. Both handles are
// drawn as solid objects tied to their endpoint by a tangent, the grab target is wider than
// the disc that is painted, and the curve redraws under the cursor rather than on release.
//
// Only InterpolationMode::CubicBezier is editable. Linear and EaseInOut draw the shape they
// evaluate to (so switching modes shows what changed) and answer a press with
// CustomCurveRequested rather than moving anything themselves: what that means for the
// keyframe is the owner's decision, not this widget's.

#include "Common.h"

#include <QPointF>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QVariantAnimation;
QT_END_NAMESPACE

namespace SP {

// The cubic handles that trace the smoothstep EaseInOut actually evaluates, and so also
// what a keyframe is seeded with when it converts from EaseInOut to a custom curve: the
// shape does not jump on the frame the mode changes.
inline constexpr BezierHandles kSmoothstepHandles{0.42f, 0.0f, 0.58f, 1.0f};

class BezierEditor : public QWidget {
    Q_OBJECT
public:
    explicit BezierEditor(QWidget* parent = nullptr);

    // Draw this curve. Never emits: it is how the owner pushes state in.
    void SetCurve(InterpolationMode mode, const BezierHandles& handles);

    QSize sizeHint() const override;

signals:
    // Live, on every step of a drag. The curve is the value being edited, so it lands in
    // the keyframe as the handle moves and not when the button comes up.
    void HandlesMoved(const BezierHandles& handles);

    // A press on a curve that is not custom yet.
    void CustomCurveRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // The unit square in widget coordinates. Inset so a handle parked in a corner is drawn
    // whole rather than half-clipped by the frame.
    QRectF Plot() const;
    QPointF ToWidget(qreal x, qreal y) const;

    // 0 = the handle leaving this keyframe, 1 = the handle entering the next one.
    QPointF HandlePos(int handle) const;
    int HandleAt(const QPointF& pos) const;

    void SetHovered(int handle);
    void ApplyDrag(const QPointF& pos);
    void RefreshCursor(const QPointF& pos);

    InterpolationMode m_mode = InterpolationMode::Linear;
    BezierHandles m_handles;

    int m_hovered = -1;
    int m_dragging = -1;
    QPointF m_grab;              // handle centre minus press point, so the disc does not jump
    qreal m_hoverT = 0.0;        // 0..1 grow of the live handle
    QVariantAnimation* m_hoverAnim = nullptr;
};

}  // namespace SP
