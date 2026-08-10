#pragma once

// Theme.h — the single owner of colour, spacing, type and motion for the Qt UI.
//
// Every value here is mirrored literally in resources/shaderplayer.qss, which styles the
// widget vocabulary. Code that needs a token at runtime (a region tint, a glow, an
// animation duration) reads it from here; no call site anywhere else in the UI picks a
// colour, a radius or a duration by hand.

#include <QColor>
#include <QEasingCurve>
#include <QIcon>
#include <QString>

class QApplication;

namespace SP::Theme {

// ---------------------------------------------------------------------------------------
// Colour tokens
//
// These are `inline const QColor` rather than `constexpr QRgb`: QColor has no constexpr
// constructor in a form usable here, and a QRgb constant is an active hazard for the
// translucent tokens, because QColor's QRgb constructor discards the alpha and yields an
// opaque white. As QColor objects the alpha survives and a caller writes `Theme::kPanelFill`
// directly.
// ---------------------------------------------------------------------------------------

// Ground. The canvas is the majority of every screen; the anchor is the far end of the
// canvas gradient and the base of any atmospheric glow.
inline const QColor kCanvas          (0x0B, 0x0B, 0x0B);
inline const QColor kAnchor          (0x16, 0x0B, 0x2A);

// Glass. Alpha is 0-255 here and in QSS, never 0-1.
inline const QColor kPanelFill       (255, 255, 255,  15);  // ~6%,  floating panels
inline const QColor kPanelFillRaised (255, 255, 255,  26);  // ~10%, dialogs, menus, popups
inline const QColor kPanelBorder     (255, 255, 255,  38);  // ~15%, 1px hairline
inline const QColor kInputFill       (  0,   0,   0, 110);  // recessed: darker than its host

// Text.
inline const QColor kTextPrimary     (0xEC, 0xEC, 0xEC);    // body and controls   (16.8:1)
inline const QColor kTextSecondary   (0xB8, 0xBC, 0xC4);    // captions only       (10.2:1)
inline const QColor kTextOnAccent    (0x06, 0x18, 0x1B);    // on any bright accent fill

// Accents. Cyan is reserved for global interactive state (focus, active, primary action,
// selection) and no region may claim it.
inline const QColor kAccentPrimary   (0x3D, 0xE0, 0xF0);    // cyan                (12.3:1)
inline const QColor kAccentSecondary (0x5A, 0xA8, 0xFF);    // azure                (8.0:1)
inline const QColor kAccentTertiary  (0xA3, 0xF0, 0x4A);    // lime                (14.2:1)
inline const QColor kAccentQuaternary(0xFF, 0x5F, 0xD2);    // magenta              (7.3:1)
inline const QColor kStateWarning    (0xFF, 0xB6, 0x3D);    // amber               (11.2:1)
inline const QColor kStateError      (0xFF, 0x5A, 0x52);    // red                  (6.4:1)

// Region hues. One per dock, assigned once, never reused for another region. Carried by the
// panel title text, the panel's top hairline, the region icon tint and the hover glow.
inline const QColor kRegionLibrary   (0x5A, 0xA8, 0xFF);    // azure
inline const QColor kRegionEditor    (0xA9, 0x7C, 0xFF);    // violet
inline const QColor kRegionParams    (0xA3, 0xF0, 0x4A);    // lime
inline const QColor kRegionTransport (0xFF, 0xB6, 0x3D);    // amber
inline const QColor kRegionRecording (0xFF, 0x5A, 0x52);    // red
inline const QColor kRegionAudio     (0xFF, 0x5F, 0xD2);    // magenta
inline const QColor kRegionNoise     (0x2F, 0xD9, 0xA8);    // mint
inline const QColor kRegionSpout     (0xFF, 0x7A, 0x9C);    // rose

// ---------------------------------------------------------------------------------------
// Form, type and motion tokens
// ---------------------------------------------------------------------------------------

inline constexpr int kSpaceUnit    = 8;    // one spacing step, in px
inline constexpr int kPanelGap     = 24;   // between panels: 3 units of visible canvas
inline constexpr int kRadiusPanel  = 10;
inline constexpr int kRadiusControl = 6;
inline constexpr int kRadiusDialog = 14;

inline constexpr const char* kFontUi   = "Tahoma";
inline constexpr const char* kFontMono = "Consolas";   // editor and fixed-width data only
inline constexpr int kFontSizeBody       = 10;         // pt
inline constexpr int kFontSizePanelTitle = 11;         // pt, bold
inline constexpr int kFontSizeCaption    = 9;          // pt, the floor

inline constexpr int kMotionHover     = 140;  // ms, hover and small state shifts
inline constexpr int kMotionBase      = 220;  // ms, transitions, reveals, panel changes
inline constexpr int kMotionCelebrate = 600;  // ms, milestone flourishes, never blocking

inline constexpr QEasingCurve::Type kEaseStandard = QEasingCurve::OutCubic;
inline constexpr QEasingCurve::Type kEaseEntrance = QEasingCurve::OutBack;

// ---------------------------------------------------------------------------------------

// A procedurally painted region glyph tinted with `tint` (pass the matching kRegion* token).
// `name` is one of "library", "editor", "params", "transport", "recording", "audio",
// "noise", "spout"; an unrecognised name paints a plain chip so the miss is visible.
// There are no icon asset files: the glyph is drawn with QPainter, which also lets a single
// glyph be re-tinted per region without a second file.
QIcon RegionIcon(const QString& name, const QColor& tint);

// Fusion style, a palette seeded from the tokens above, then :/shaderplayer.qss.
// Call once on the QApplication before constructing any widget.
void Apply(QApplication& app);

}  // namespace SP::Theme
