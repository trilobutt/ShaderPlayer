#include "ui/Theme.h"

#include <QApplication>
#include <QBrush>
#include <QDebug>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPointF>
#include <QRectF>

namespace SP::Theme {

namespace {

// GUI thread only; nothing else touches it.
bool g_reducedMotion = false;

// Region glyphs are painted at twice the 16px toolbar size and scaled down by QIcon.
constexpr int kIconPx = 32;

QPainterPath Stroked(const QPainterPath& line, qreal width)
{
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(line);
}

// All glyphs are authored in a 32x32 box with a ~5px margin, as fillable paths (line-based
// shapes are converted by Stroked) so one fill and one bloom pass covers every region.
QPainterPath GlyphPath(const QString& name)
{
    QPainterPath path;

    if (name == QLatin1String("library")) {  // a shelf of entries
        path.addRoundedRect(QRectF(6.0,  6.0, 20.0, 5.0), 2.0, 2.0);
        path.addRoundedRect(QRectF(6.0, 13.5, 20.0, 5.0), 2.0, 2.0);
        path.addRoundedRect(QRectF(6.0, 21.0, 14.0, 5.0), 2.0, 2.0);
        return path;
    }
    if (name == QLatin1String("editor")) {  // angle brackets
        QPainterPath line;
        line.moveTo(13.0,  8.0);  line.lineTo( 6.5, 16.0);  line.lineTo(13.0, 24.0);
        line.moveTo(19.0,  8.0);  line.lineTo(25.5, 16.0);  line.lineTo(19.0, 24.0);
        return Stroked(line, 3.0);
    }
    if (name == QLatin1String("params")) {  // three sliders with knobs
        QPainterPath line;
        line.moveTo(5.5,  9.0);  line.lineTo(26.5,  9.0);
        line.moveTo(5.5, 16.0);  line.lineTo(26.5, 16.0);
        line.moveTo(5.5, 23.0);  line.lineTo(26.5, 23.0);
        path = Stroked(line, 2.5);
        path.addEllipse(QPointF(11.0,  9.0), 3.4, 3.4);
        path.addEllipse(QPointF(20.5, 16.0), 3.4, 3.4);
        path.addEllipse(QPointF(14.5, 23.0), 3.4, 3.4);
        return path;
    }
    if (name == QLatin1String("transport")) {  // play triangle
        path.moveTo(11.0,  6.5);
        path.lineTo(26.0, 16.0);
        path.lineTo(11.0, 25.5);
        path.closeSubpath();
        return path;
    }
    if (name == QLatin1String("recording")) {  // record dot
        path.addEllipse(QPointF(16.0, 16.0), 8.0, 8.0);
        return path;
    }
    if (name == QLatin1String("audio")) {  // level meter
        path.addRoundedRect(QRectF( 5.0, 17.0, 4.0,  9.0), 2.0, 2.0);
        path.addRoundedRect(QRectF(11.0,  9.0, 4.0, 17.0), 2.0, 2.0);
        path.addRoundedRect(QRectF(17.0, 13.0, 4.0, 13.0), 2.0, 2.0);
        path.addRoundedRect(QRectF(23.0, 20.0, 4.0,  6.0), 2.0, 2.0);
        return path;
    }
    if (name == QLatin1String("noise")) {  // fixed scatter: the icon must not shimmer
        static constexpr struct { qreal x, y, r; } kDots[] = {
            { 8.5,  8.0, 2.6}, {16.5,  6.5, 1.7}, {23.5, 10.0, 2.9},
            { 6.5, 16.5, 1.9}, {14.0, 15.0, 3.1}, {22.0, 18.5, 2.0},
            { 9.5, 23.5, 2.7}, {17.5, 24.5, 2.2}, {25.0, 24.0, 1.6},
        };
        for (const auto& dot : kDots) {
            path.addEllipse(QPointF(dot.x, dot.y), dot.r, dot.r);
        }
        return path;
    }
    if (name == QLatin1String("spout")) {  // a frame with the signal leaving it
        QPainterPath line;
        line.moveTo(17.0,  7.5);  line.lineTo( 7.0,  7.5);
        line.lineTo( 7.0, 24.5);  line.lineTo(17.0, 24.5);
        line.moveTo(14.0, 16.0);  line.lineTo(24.0, 16.0);
        path = Stroked(line, 2.6);
        QPainterPath head;
        head.moveTo(21.0, 11.0);
        head.lineTo(27.5, 16.0);
        head.lineTo(21.0, 21.0);
        head.closeSubpath();
        path.addPath(head);
        return path;
    }

    QPainterPath chip;
    chip.addRoundedRect(QRectF(8.0, 8.0, 16.0, 16.0), 4.0, 4.0);
    return Stroked(chip, 2.6);
}

}  // namespace

int Motion(int ms)
{
    return g_reducedMotion ? 0 : ms;
}

void SetReducedMotion(bool on)
{
    g_reducedMotion = on;
}

QIcon RegionIcon(const QString& name, const QColor& tint)
{
    QPainterPath path = GlyphPath(name);
    // Glyphs are unions of overlapping subpaths; odd-even filling would punch holes at
    // every overlap (a slider knob sitting on its track).
    path.setFillRule(Qt::WindingFill);

    QPixmap pixmap(kIconPx, kIconPx);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Bloom: successively tighter, brighter strokes under the fill. QPainter has no cheap
    // blur, and this reads as the soft coloured glow the tokens ask for at icon scale.
    struct Bloom { qreal width; int alpha; };
    static constexpr Bloom kBloom[] = {{6.0, 28}, {4.0, 45}, {2.0, 70}};
    painter.setBrush(Qt::NoBrush);
    for (const Bloom& ring : kBloom) {
        QColor glow = tint;
        glow.setAlpha(ring.alpha);
        painter.setPen(QPen(glow, ring.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }

    // Fill with a vertical gradient and rim the shape: shading and a highlight, rather than
    // a flat monochrome silhouette.
    QLinearGradient body(0.0, 0.0, 0.0, static_cast<qreal>(kIconPx));
    body.setColorAt(0.0, tint.lighter(155));
    body.setColorAt(1.0, tint.darker(125));
    painter.setBrush(body);
    painter.setPen(QPen(tint.lighter(180), 0.8));
    painter.drawPath(path);
    painter.end();

    return QIcon(pixmap);
}

void Apply(QApplication& app)
{
    // Fusion first. On the native Windows style the stylesheet is honoured inconsistently,
    // and Fusion is the style whose palette actually follows the one set below.
    app.setStyle(QStringLiteral("Fusion"));

    // The stylesheet cannot reach everything the style draws (tooltip base, disabled text,
    // placeholder text, some item-view chrome), and Fusion's stock palette is a light grey
    // that would fail the legibility floor against the canvas. Seed those roles from the
    // same tokens. Translucent tokens are pre-composited over the canvas here because a
    // palette brush has nothing behind it to blend with.
    QPalette pal;
    pal.setColor(QPalette::Window,          kCanvas);
    pal.setColor(QPalette::WindowText,      kTextPrimary);
    pal.setColor(QPalette::Base,            QColor(0x06, 0x06, 0x06));  // input-fill on canvas
    pal.setColor(QPalette::AlternateBase,   QColor(0x19, 0x19, 0x19));  // panel-fill on canvas
    pal.setColor(QPalette::Text,            kTextPrimary);
    pal.setColor(QPalette::Button,          QColor(0x24, 0x24, 0x24));  // raised fill on canvas
    pal.setColor(QPalette::ButtonText,      kTextPrimary);
    pal.setColor(QPalette::BrightText,      kStateError);
    pal.setColor(QPalette::ToolTipBase,     QColor(0x24, 0x24, 0x24));
    pal.setColor(QPalette::ToolTipText,     kTextPrimary);
    pal.setColor(QPalette::PlaceholderText, kTextSecondary);
    pal.setColor(QPalette::Highlight,       kAccentPrimary);
    pal.setColor(QPalette::HighlightedText, kTextOnAccent);
    pal.setColor(QPalette::Link,            kAccentSecondary);
    pal.setColor(QPalette::LinkVisited,     kAccentQuaternary);

    // Disabled controls read as inert rather than silently no-op: still legible, clearly
    // desaturated against text-primary.
    const QColor inert(0x7A, 0x7E, 0x86);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, inert);
    pal.setColor(QPalette::Disabled, QPalette::Text,       inert);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, inert);
    pal.setColor(QPalette::Disabled, QPalette::Highlight,  QColor(0x2A, 0x2A, 0x2A));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, inert);
    app.setPalette(pal);

    // The base font is set here rather than in the stylesheet, and that placement is
    // load-bearing. A `font-size` in a QSS rule beats QWidget::setFont() on the widgets it
    // matches, so a `QWidget { font-size: 10pt; }` base rule silently flattens every
    // deliberate type step the panels set in C++ — the empty state's 38pt heading, every
    // panel's "no content yet" title, the keybinding preview — all of them render at 10pt
    // and the whole hierarchy collapses to one size. An application font has no such
    // precedence: it is the inherited default, so a widget that asks for something else
    // still gets it, while the object-name rules in the sheet (#PanelTitle, #Caption,
    // #Mono) keep overriding deliberately.
    app.setFont(QFont(QString::fromLatin1(kFontUi), kFontSizeBody));

    QFile sheet(QStringLiteral(":/shaderplayer.qss"));
    if (sheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(sheet.readAll()));
    } else {
        qWarning("Theme::Apply: :/shaderplayer.qss is missing from the resource bundle; "
                 "the UI falls back to bare Fusion.");
    }
}

}  // namespace SP::Theme
