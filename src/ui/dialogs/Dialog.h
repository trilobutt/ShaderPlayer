#pragma once

// Dialog.h — the elevation shell every ShaderPlayer dialog is built in.
//
// A dialog sits one step above a dock on the elevation stack: the raised panel fill, the
// dialog radius, and a heavier lift shadow beneath it. Qt has no backdrop blur and a
// top-level window has nothing behind it to tint, so the raised fill is composited over the
// canvas by hand and the window itself is frameless and translucent, leaving only the
// rounded island and its shadow on screen.
//
// Frameless also means the shell owns what the OS title bar would have done: the title, the
// close button, and dragging the window (handed to the compositor via startSystemMove, so a
// drag still snaps and still respects the desktop's edges).
//
// The entrance is the one animation this surface earns: a short rise with an overshoot
// settle and a fade, both through Theme::Motion so reduced motion collapses them to an
// instant show rather than to no dialog at all.

#include <QColor>
#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
class QFrame;
class QHBoxLayout;
class QLabel;
class QMouseEvent;
class QShowEvent;
class QVBoxLayout;
QT_END_NAMESPACE

namespace SP {

class Dialog : public QDialog {
    Q_OBJECT
public:
    // `hue` is the dialog's identity colour: the hairline across the top of the island and
    // the title text. Global surfaces (keybindings, workspaces) take the reserved cyan
    // because that is what global interactive state is coloured with everywhere else; a
    // dialog acting on one region takes that region's hue instead.
    Dialog(const QString& title, const QColor& hue, QWidget* parent);

protected:
    // Where a subclass adds its content. Everything above and below it belongs to the shell.
    QVBoxLayout* Body() const { return m_body; }

    // QSS wants an rgba() string and the tokens carry alpha, so every subclass that tints a
    // child needs this; Composite is for the one case a token has to be flattened by hand
    // (a top-level window has nothing behind it to be translucent over).
    static QString Rgba(const QColor& colour);
    static QColor Composite(const QColor& over, const QColor& base);

    // A right-aligned row at the foot of the body, with the gap that separates actions from
    // the content they act on already spent.
    QHBoxLayout* AddButtonRow();

    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QFrame* m_frame = nullptr;      // QFrame#DialogFrame, the island
    QWidget* m_header = nullptr;    // title row; a press anywhere in it drags the window
    QVBoxLayout* m_body = nullptr;
    QLabel* m_title = nullptr;
    bool m_entered = false;         // the entrance runs once, not on every re-show
};

}  // namespace SP
