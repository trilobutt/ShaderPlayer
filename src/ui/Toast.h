#pragma once

// Toast.h — the transient-notice surface.
//
// A notice is a small glass island in the bottom-right of the central area: it slides in over
// the picture, holds for its own duration, then fades. Deliberately not a banner across the
// top of the window, which is the shape people have learned to look past whatever it says.
//
// Each notice is a frameless top-level rather than a child widget, and the viewport is what
// forces that. ViewportWidget is WA_NativeWindow + WA_PaintOnScreen, so it owns a real child
// HWND, and Windows composites a native child window over everything Qt has painted into the
// parent's client area. An ordinary child widget laid over the picture is therefore invisible
// exactly when a notice matters most, and raise() cannot help: it reorders native siblings,
// and an alien widget is not one. Giving the notice WA_NativeWindow would win it an HWND of
// its own, but a native *child* window cannot blend with the swap-chain pixels behind it —
// no per-pixel alpha, no anti-aliased corner, and no windowOpacity at all, since QWidget
// drops that property for anything that is not a window. There would be nothing left to fade.
//
// The top-level keeps all of it, and the costs a tool window usually carries do not land
// here: Qt::Tool sets WS_EX_TOOLWINDOW, which Windows excludes from both the taskbar and
// Alt-Tab, and the notice is owned by the main window, so it sits above that window (and
// above the viewport inside it) without Qt::WindowStaysOnTopHint and never floats over
// another application. What it does not get for free is following its owner, so ToastStack
// watches the owner and the anchor for geometry changes itself.

#include <QPoint>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QEvent;
class QGraphicsDropShadowEffect;
class QMouseEvent;
class QPropertyAnimation;
class QVariantAnimation;
QT_END_NAMESPACE

namespace SP {

// One notice. Owned by its ToastStack only in the sense that the stack drives it; the widget
// deletes itself when it closes.
class Toast : public QWidget {
    Q_OBJECT
public:
    Toast(const QString& message, QWidget* owner);

    // Where the island's top-left corner belongs, in screen coordinates. The first call is
    // the entrance; every later one is a restack and travels there rather than jumping.
    void PlaceAt(const QPoint& resting);

    // Fade out and close. Idempotent, so an early click and the hold timer cannot race.
    void Leave();

signals:
    // Emitted the moment the notice starts leaving, not when it has gone: the stack closes
    // the gap while this one is still fading, so the reflow and the exit read as one motion.
    void Dismissed(Toast* toast);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void Enter();
    void AnimateLift(bool up);
    void ApplyLift(qreal t);

    QGraphicsDropShadowEffect* m_lift = nullptr;
    QVariantAnimation* m_liftAnim = nullptr;

    // The only writers of `pos` and `windowOpacity`. Kept as members and retargeted rather
    // than allocated per transition, so an entrance interrupted by a restack cannot leave two
    // animations fighting over the same property.
    QPropertyAnimation* m_move = nullptr;
    QPropertyAnimation* m_fade = nullptr;

    QPoint m_resting;
    bool m_shown = false;
    bool m_leaving = false;
};

// The window's notice stack: bottom-right of `anchor`, newest nearest the corner, bounded so
// a burst of messages cannot accumulate windows.
class ToastStack : public QObject {
    Q_OBJECT
public:
    // `owner` is the window the notices belong to; `anchor` is the widget whose corner they
    // hug (the central area, so a notice never covers a dock).
    ToastStack(QWidget* owner, QWidget* anchor);

    void Show(const QString& message, int durationMs);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void Restack();

    QWidget* m_owner = nullptr;
    QWidget* m_anchor = nullptr;
    QVector<Toast*> m_live;   // oldest first; the last one sits nearest the corner
};

}  // namespace SP
