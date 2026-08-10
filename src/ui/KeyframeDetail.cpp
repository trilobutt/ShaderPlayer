#include "ui/KeyframeDetail.h"

#include "Application.h"
#include "ui/BezierEditor.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace SP {

namespace {

constexpr int kChipGap     = 6;
constexpr int kChipHeight  = 22;
constexpr int kLabelColumn = 46;    // "Time", "Value", "Curve"
constexpr int kValueColumn = 86;    // the spin box beside a slider, and the Point2D pair
constexpr int kKeyTicks    = 1000;  // Float slider resolution for a keyframe value
constexpr int kSwatchWidth = 60;
constexpr int kSwatchHeight = 20;

QString Rgba(const QColor& c)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

float Clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

int DecimalsFor(float step)
{
    if (!(step > 0.0f)) return 3;
    const double places = std::ceil(-std::log10(static_cast<double>(step)));
    return std::clamp(static_cast<int>(places), 0, 6);
}

double TickToValue(int tick, const ShaderParam& param)
{
    const double t = static_cast<double>(tick) / kKeyTicks;
    return static_cast<double>(param.min)
         + t * (static_cast<double>(param.max) - static_cast<double>(param.min));
}

int ValueToTick(double v, const ShaderParam& param)
{
    const double range = static_cast<double>(param.max) - static_cast<double>(param.min);
    if (!(range > 0.0)) return 0;
    const double t = (v - static_cast<double>(param.min)) / range;
    return std::clamp(static_cast<int>(std::lround(t * kKeyTicks)), 0, kKeyTicks);
}

QColor ColourOf(const float values[4])
{
    return QColor::fromRgbF(Clamp01(values[0]), Clamp01(values[1]),
                            Clamp01(values[2]), Clamp01(values[3]));
}

// A checker under the colour, because alpha is opacity everywhere a shader reads it and a
// flat chip would show a half-transparent colour as a darker shade of itself.
QPixmap SwatchPixmap(const QColor& colour, qreal dpr)
{
    const QSize size(kSwatchWidth, kSwatchHeight);
    QPixmap pixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0.5, 0.5, size.width() - 1.0, size.height() - 1.0),
                        Theme::kRadiusControl, Theme::kRadiusControl);
    painter.setClipPath(clip);
    painter.fillRect(QRectF(QPointF(0, 0), QSizeF(size)), Theme::kCanvas);
    constexpr int kCheck = 5;
    for (int y = 0; y < size.height(); y += kCheck) {
        for (int x = 0; x < size.width(); x += kCheck) {
            if (((x / kCheck) + (y / kCheck)) % 2 == 0) continue;
            painter.fillRect(QRect(x, y, kCheck, kCheck), Theme::kPanelBorder);
        }
    }
    painter.fillRect(QRectF(QPointF(0, 0), QSizeF(size)), colour);

    painter.setClipping(false);
    painter.setPen(QPen(Theme::kPanelBorder, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(clip);
    return pixmap;
}

}  // namespace

// =======================================================================================
// ChipStrip
// =======================================================================================

ChipStrip::ChipStrip(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("KeyframeChips"));
}

void ChipStrip::Clear()
{
    for (QPushButton* chip : m_chips) {
        // Picking a keyframe rebuilds the strip, so a chip is cleared from inside its own
        // clicked() handler and cannot be destroyed here: QAbstractButton is still
        // unwinding its mouse release when the slot returns. Hidden and disconnected now,
        // deleted once the event loop is back.
        chip->hide();
        chip->disconnect();
        chip->deleteLater();
    }
    m_chips.clear();
    m_height = 0;
    setFixedHeight(0);
}

void ChipStrip::Add(QPushButton* chip)
{
    chip->setParent(this);
    chip->show();
    m_chips.append(chip);
}

void ChipStrip::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    Reflow();
}

void ChipStrip::Reflow()
{
    int x = 0;
    int y = 0;
    int rowHeight = 0;

    // Chips are laid out the moment they are built, which is before the strip has been
    // given a width. Wrapping against a width of zero would stack every chip on its own
    // row and then unstack them on the first resize, so an unsized strip lays out on one
    // row and the resize that follows does the real wrap.
    int available = width();
    if (available <= 1 && parentWidget()) available = parentWidget()->width();
    if (available <= 1) available = std::numeric_limits<int>::max();

    for (QPushButton* chip : m_chips) {
        const QSize hint = chip->sizeHint();
        if (x > 0 && x + hint.width() > available) {
            x = 0;
            y += rowHeight + kChipGap;
            rowHeight = 0;
        }
        chip->setGeometry(x, y, hint.width(), hint.height());
        x += hint.width() + kChipGap;
        rowHeight = std::max(rowHeight, hint.height());
    }

    // Owning the height is what keeps a wrapped second row from being clipped. It only
    // ever changes when the wrap does, so this settles after one extra resize.
    const int needed = m_chips.isEmpty() ? 0 : y + rowHeight;
    if (needed != m_height) {
        m_height = needed;
        setFixedHeight(needed);
    }
}

// =======================================================================================
// KeyframeDetail
// =======================================================================================

KeyframeDetail::KeyframeDetail(Application& app, ShaderPreset* preset, int paramIndex,
                               QWidget* parent)
    : QWidget(parent)
    , m_app(app)
    , m_preset(preset)
    , m_index(paramIndex)
{
    setObjectName(QStringLiteral("KeyframeDetail"));

    if (const ShaderParam* param = Param()) m_type = param->type;

    // ParamsPanel paints its named containers transparent so they do not lay opaque black
    // over the panel's glass; this widget is a container of the same kind and needs the
    // same opt-out, plus the chip vocabulary, which exists nowhere else in the product.
    const QColor accent = Theme::kAccentPrimary;
    QColor accentEdge = accent;
    accentEdge.setAlpha(150);
    setStyleSheet(QStringLiteral(
        "QWidget#KeyframeDetail, QWidget#KeyframeStrip, QWidget#KeyframeChips,"
        "QWidget#KeyframeEditor, QWidget#KeyframeCell { background: transparent; }"
        "QWidget#KeyframeChips QPushButton {"
        " background: %1; border: 1px solid %2; border-radius: %3px;"
        " padding: 1px 9px; font-size: %4pt; min-height: 0px; }"
        "QWidget#KeyframeChips QPushButton:hover {"
        " background: %5; border-color: %6; }"
        "QWidget#KeyframeChips QPushButton:checked {"
        " background: %7; border-color: %7; color: %8; font-weight: bold; }")
        .arg(Rgba(Theme::kPanelFillRaised))
        .arg(Rgba(Theme::kPanelBorder))
        .arg(Theme::kRadiusControl)
        .arg(Theme::kFontSizeCaption)
        .arg(Rgba(QColor(255, 255, 255, 45)))
        .arg(Rgba(accentEdge))
        .arg(accent.name())
        .arg(Theme::kTextOnAccent.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, Theme::kSpaceUnit);
    layout->setSpacing(Theme::kSpaceUnit);

    BuildStrip(layout);
    BuildEditor(layout);

    Refresh();
}

// ---------------------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------------------

ShaderParam* KeyframeDetail::Param() const
{
    if (!m_preset) return nullptr;
    if (m_index < 0 || m_index >= static_cast<int>(m_preset->params.size())) return nullptr;
    return &m_preset->params[static_cast<size_t>(m_index)];
}

KeyframeTimeline* KeyframeDetail::Timeline() const
{
    ShaderParam* param = Param();
    if (!param || !param->timeline.has_value()) return nullptr;
    return &param->timeline.value();
}

Keyframe* KeyframeDetail::Selected() const
{
    KeyframeTimeline* timeline = Timeline();
    if (!timeline) return nullptr;
    if (m_selected < 0 || m_selected >= static_cast<int>(timeline->keyframes.size())) {
        return nullptr;
    }
    return &timeline->keyframes[static_cast<size_t>(m_selected)];
}

bool KeyframeDetail::FrameMode() const
{
    return m_app.GetConfig().timeDisplayFrames && Fps() > 0.0;
}

double KeyframeDetail::Fps() const
{
    return m_app.GetDecoder().GetFPS();
}

QString KeyframeDetail::TimeText(float seconds) const
{
    if (FrameMode()) {
        return tr("%1f").arg(static_cast<int>(seconds * Fps() + 0.5));
    }
    return tr("%1s").arg(seconds, 0, 'f', 1);
}

// ---------------------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------------------

void KeyframeDetail::BuildStrip(QVBoxLayout* layout)
{
    auto* strip = new QWidget(this);
    strip->setObjectName(QStringLiteral("KeyframeStrip"));
    auto* row = new QHBoxLayout(strip);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Theme::kSpaceUnit);

    auto* addKey = new QPushButton(tr("+ Key"), strip);
    addKey->setToolTip(tr("Record this parameter's current value as a keyframe at the "
                          "playhead."));
    addKey->setCursor(Qt::PointingHandCursor);
    connect(addKey, &QPushButton::clicked, this, &KeyframeDetail::AddKeyAtPlayhead);
    row->addWidget(addKey, 0, Qt::AlignTop);

    auto* chips = new QWidget(strip);
    chips->setObjectName(QStringLiteral("KeyframeCell"));
    auto* chipLayout = new QVBoxLayout(chips);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(2);

    m_strip = new ChipStrip(chips);
    chipLayout->addWidget(m_strip);

    // A track that is on but empty is a normal state on the way to a first keyframe, and
    // saying so beats an unexplained gap where the chips will be.
    m_empty = new QLabel(tr("No keyframes yet. + Key records the value at the playhead."),
                         chips);
    m_empty->setObjectName(QStringLiteral("Caption"));
    m_empty->setWordWrap(true);
    chipLayout->addWidget(m_empty);

    row->addWidget(chips, 1);
    layout->addWidget(strip);
}

void KeyframeDetail::BuildEditor(QVBoxLayout* layout)
{
    m_editor = new QWidget(this);
    m_editor->setObjectName(QStringLiteral("KeyframeEditor"));
    auto* block = new QVBoxLayout(m_editor);
    block->setContentsMargins(0, 0, 0, 0);
    block->setSpacing(Theme::kSpaceUnit);

    // A real separation: above the line is the track, below it is the one keyframe.
    auto* rule = new QFrame(m_editor);
    rule->setObjectName(QStringLiteral("PanelHairline"));
    block->addWidget(rule);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(Theme::kSpaceUnit);
    grid->setVerticalSpacing(Theme::kSpaceUnit);
    grid->setColumnMinimumWidth(0, kLabelColumn);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);
    block->addLayout(grid);

    // ---- time -------------------------------------------------------------------------
    auto* timeLabel = new QLabel(tr("Time"), m_editor);
    grid->addWidget(timeLabel, 0, 0);

    auto* timeCell = new QWidget(m_editor);
    timeCell->setObjectName(QStringLiteral("KeyframeCell"));
    auto* time = new QHBoxLayout(timeCell);
    time->setContentsMargins(0, 0, 0, 0);
    time->setSpacing(Theme::kSpaceUnit);
    grid->addWidget(timeCell, 0, 1);

    const QString timeTip = tr("Where this keyframe sits on the video's timeline.");

    m_timeSeconds = new QDoubleSpinBox(timeCell);
    m_timeSeconds->setDecimals(2);
    m_timeSeconds->setRange(0.0, 1.0e6);
    m_timeSeconds->setSingleStep(0.1);
    m_timeSeconds->setSuffix(tr(" s"));
    m_timeSeconds->setKeyboardTracking(false);   // one reposition per committed edit
    m_timeSeconds->setFixedWidth(kValueColumn);
    m_timeSeconds->setToolTip(timeTip);
    time->addWidget(m_timeSeconds, 0);

    m_timeFrames = new QSpinBox(timeCell);
    m_timeFrames->setRange(0, 100000000);
    m_timeFrames->setSuffix(tr(" f"));
    m_timeFrames->setKeyboardTracking(false);
    m_timeFrames->setFixedWidth(kValueColumn);
    m_timeFrames->setToolTip(timeTip);
    time->addWidget(m_timeFrames, 0);

    m_snap = new QPushButton(tr("To Playhead"), timeCell);
    m_snap->setToolTip(tr("Move this keyframe to where the transport is now."));
    m_snap->setCursor(Qt::PointingHandCursor);
    time->addWidget(m_snap, 0);
    time->addStretch(1);

    m_delete = new QPushButton(tr("Delete"), timeCell);
    m_delete->setObjectName(QStringLiteral("Danger"));
    m_delete->setToolTip(tr("Remove this keyframe. The track keeps the rest."));
    m_delete->setCursor(Qt::PointingHandCursor);
    time->addWidget(m_delete, 0);

    connect(m_timeSeconds, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
        RepositionSelected(static_cast<float>(std::max(0.0, seconds)));
    });
    connect(m_timeFrames, &QSpinBox::valueChanged, this, [this](int frame) {
        const double fps = Fps();
        if (!(fps > 0.0)) return;
        RepositionSelected(static_cast<float>(std::max(0, frame) / fps));
    });
    connect(m_snap, &QPushButton::clicked, this, [this] {
        RepositionSelected(m_app.GetPlaybackTime());
    });
    connect(m_delete, &QPushButton::clicked, this, &KeyframeDetail::DeleteSelected);

    // ---- value ------------------------------------------------------------------------
    grid->addWidget(new QLabel(tr("Value"), m_editor), 1, 0);

    auto* valueCell = new QWidget(m_editor);
    valueCell->setObjectName(QStringLiteral("KeyframeCell"));
    grid->addWidget(valueCell, 1, 1);
    BuildValueEditor(valueCell);

    // ---- curve ------------------------------------------------------------------------
    m_curveLabel = new QLabel(tr("Curve"), m_editor);
    grid->addWidget(m_curveLabel, 2, 0);

    m_curve = new QComboBox(m_editor);
    m_curve->addItems(QStringList{tr("Linear"), tr("Ease In/Out"), tr("Custom Bezier")});
    m_curve->setToolTip(tr("How the value travels from this keyframe to the next one."));
    grid->addWidget(m_curve, 2, 1, Qt::AlignLeft);

    m_bezier = new BezierEditor(m_editor);
    grid->addWidget(m_bezier, 3, 1, Qt::AlignLeft);

    m_caption = new QLabel(m_editor);
    m_caption->setObjectName(QStringLiteral("Caption"));
    m_caption->setWordWrap(true);
    grid->addWidget(m_caption, 4, 1);

    connect(m_curve, &QComboBox::currentIndexChanged, this, [this](int index) {
        Keyframe* kf = Selected();
        if (!kf || index < 0) return;
        const auto mode = static_cast<InterpolationMode>(index);
        if (mode == kf->mode) return;
        // Converting from the fixed ease seeds the handles with the shape it was already
        // drawing, so the curve does not jump on the frame the mode changes.
        if (mode == InterpolationMode::CubicBezier
            && kf->mode == InterpolationMode::EaseInOut) {
            kf->handles = kSmoothstepHandles;
        }
        kf->mode = mode;
        m_bezier->SetCurve(kf->mode, kf->handles);
        UpdateCurveCaption();
        Mutated();
    });

    connect(m_bezier, &BezierEditor::HandlesMoved, this, [this](const BezierHandles& h) {
        Keyframe* kf = Selected();
        if (!kf) return;
        kf->handles = h;
        Mutated();
    });

    connect(m_bezier, &BezierEditor::CustomCurveRequested, this, [this] {
        Keyframe* kf = Selected();
        if (!kf || kf->mode == InterpolationMode::CubicBezier) return;
        if (kf->mode == InterpolationMode::EaseInOut) kf->handles = kSmoothstepHandles;
        kf->mode = InterpolationMode::CubicBezier;
        { const QSignalBlocker block(m_curve);
          m_curve->setCurrentIndex(static_cast<int>(kf->mode)); }
        m_bezier->SetCurve(kf->mode, kf->handles);
        UpdateCurveCaption();
        Mutated();
    });

    m_editor->setVisible(false);
    layout->addWidget(m_editor);
}

void KeyframeDetail::BuildValueEditor(QWidget* cell)
{
    const ShaderParam* param = Param();
    if (!param) return;

    auto* layout = new QHBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kSpaceUnit);

    const int decimals = DecimalsFor(param->step);

    switch (m_type) {

    case ShaderParamType::Float: {
        m_slider = new QSlider(Qt::Horizontal, cell);
        m_slider->setRange(0, kKeyTicks);
        layout->addWidget(m_slider, 1);

        m_spin[0] = new QDoubleSpinBox(cell);
        m_spin[0]->setDecimals(decimals);
        m_spin[0]->setRange(param->min, param->max);
        m_spin[0]->setSingleStep(param->step > 0.0f ? param->step : 0.01);
        m_spin[0]->setKeyboardTracking(false);
        m_spin[0]->setFixedWidth(kValueColumn);
        layout->addWidget(m_spin[0], 0);

        connect(m_slider, &QSlider::valueChanged, this, [this](int tick) {
            Keyframe* kf = Selected();
            const ShaderParam* p = Param();
            if (!kf || !p) return;
            kf->values[0] = static_cast<float>(
                std::clamp(TickToValue(tick, *p), static_cast<double>(p->min),
                           static_cast<double>(p->max)));
            { const QSignalBlocker block(m_spin[0]); m_spin[0]->setValue(kf->values[0]); }
            Mutated();
        });
        connect(m_spin[0], &QDoubleSpinBox::valueChanged, this, [this](double v) {
            Keyframe* kf = Selected();
            const ShaderParam* p = Param();
            if (!kf || !p) return;
            kf->values[0] = static_cast<float>(v);
            { const QSignalBlocker block(m_slider);
              m_slider->setValue(ValueToTick(v, *p)); }
            Mutated();
        });
        break;
    }

    case ShaderParamType::Bool: {
        m_check = new QCheckBox(cell);
        layout->addWidget(m_check, 0);
        layout->addStretch(1);

        connect(m_check, &QCheckBox::toggled, this, [this](bool on) {
            Keyframe* kf = Selected();
            if (!kf) return;
            kf->values[0] = on ? 1.0f : 0.0f;
            Mutated();
        });
        break;
    }

    case ShaderParamType::Long: {
        m_choice = new QComboBox(cell);
        const int entries = static_cast<int>(std::max(param->longValues.size(),
                                                      param->longLabels.size()));
        for (int i = 0; i < entries; ++i) {
            m_choice->addItem((i < static_cast<int>(param->longLabels.size()))
                ? QString::fromStdString(param->longLabels[static_cast<size_t>(i)])
                : QString::number(param->longValues[static_cast<size_t>(i)]));
        }
        m_choice->setEnabled(entries > 0);
        layout->addWidget(m_choice, 1);

        connect(m_choice, &QComboBox::currentIndexChanged, this, [this](int i) {
            Keyframe* kf = Selected();
            const ShaderParam* p = Param();
            if (!kf || !p || i < 0) return;
            kf->values[0] = static_cast<float>(
                (i < static_cast<int>(p->longValues.size()))
                    ? p->longValues[static_cast<size_t>(i)]
                    : i);
            Mutated();
        });
        break;
    }

    case ShaderParamType::Color: {
        m_swatch = new QPushButton(cell);
        m_swatch->setFixedSize(kSwatchWidth + Theme::kSpaceUnit * 2,
                               kSwatchHeight + Theme::kSpaceUnit);
        m_swatch->setIconSize(QSize(kSwatchWidth, kSwatchHeight));
        m_swatch->setCursor(Qt::PointingHandCursor);
        layout->addWidget(m_swatch, 0);
        layout->addStretch(1);

        connect(m_swatch, &QPushButton::clicked, this, [this] {
            Keyframe* kf = Selected();
            const ShaderParam* p = Param();
            if (!kf || !p) return;
            const QColor picked = QColorDialog::getColor(
                ColourOf(kf->values), this,
                tr("Keyframe colour for %1").arg(QString::fromStdString(
                    p->label.empty() ? p->name : p->label)),
                QColorDialog::ShowAlphaChannel);
            if (!picked.isValid()) return;
            kf->values[0] = static_cast<float>(picked.redF());
            kf->values[1] = static_cast<float>(picked.greenF());
            kf->values[2] = static_cast<float>(picked.blueF());
            kf->values[3] = static_cast<float>(picked.alphaF());
            SyncValueEditor(*kf);
            Mutated();
        });
        break;
    }

    case ShaderParamType::Point2D: {
        static const char* const kAxis[2] = {"X ", "Y "};
        for (int axis = 0; axis < 2; ++axis) {
            auto* spin = new QDoubleSpinBox(cell);
            spin->setDecimals(decimals);
            spin->setRange(param->min, param->max);
            spin->setSingleStep(param->step > 0.0f ? param->step : 0.01);
            spin->setKeyboardTracking(false);
            spin->setPrefix(QString::fromLatin1(kAxis[axis]));
            spin->setMinimumWidth(kValueColumn);
            layout->addWidget(spin, 1);
            m_spin[axis] = spin;

            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, axis](double v) {
                Keyframe* kf = Selected();
                if (!kf) return;
                kf->values[axis] = static_cast<float>(v);
                Mutated();
            });
        }
        break;
    }

    case ShaderParamType::Event:
    case ShaderParamType::AudioBand:
        // Neither is keyframeable, and ParamsPanel gives neither a KF toggle, so this
        // widget is never built for one.
        break;
    }
}

// ---------------------------------------------------------------------------------------
// Sync
// ---------------------------------------------------------------------------------------

void KeyframeDetail::SetSelectedKeyframe(int keyframeIndex)
{
    const KeyframeTimeline* timeline = Timeline();
    const int count = timeline ? static_cast<int>(timeline->keyframes.size()) : 0;
    m_selected = (keyframeIndex >= 0 && keyframeIndex < count) ? keyframeIndex : -1;
    Refresh();
}

void KeyframeDetail::Refresh()
{
    RebuildChips();
    SyncEditor();
}

void KeyframeDetail::RebuildChips()
{
    m_strip->Clear();

    const KeyframeTimeline* timeline = Timeline();
    const int count = timeline ? static_cast<int>(timeline->keyframes.size()) : 0;

    m_empty->setVisible(count == 0);
    m_strip->setVisible(count > 0);

    for (int i = 0; i < count; ++i) {
        const Keyframe& kf = timeline->keyframes[static_cast<size_t>(i)];
        auto* chip = new QPushButton(TimeText(kf.time), m_strip);
        chip->setCheckable(true);
        chip->setChecked(i == m_selected);
        chip->setFixedHeight(kChipHeight);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setToolTip(tr("Seek to %1 and edit this keyframe.").arg(TimeText(kf.time)));
        connect(chip, &QPushButton::clicked, this, [this, i] { SeekToKeyframe(i); });
        m_strip->Add(chip);
    }
    m_strip->Reflow();
}

void KeyframeDetail::SyncEditor()
{
    const Keyframe* kf = Selected();
    m_editor->setVisible(kf != nullptr);
    if (!kf) return;

    const bool frames = FrameMode();
    m_timeFrames->setVisible(frames);
    m_timeSeconds->setVisible(!frames);
    {
        const QSignalBlocker blockFrames(m_timeFrames);
        const QSignalBlocker blockSeconds(m_timeSeconds);
        m_timeFrames->setValue(static_cast<int>(kf->time * Fps() + 0.5));
        m_timeSeconds->setValue(kf->time);
    }
    m_snap->setEnabled(m_app.GetDecoder().IsOpen());

    SyncValueEditor(*kf);

    // Interpolation belongs to the segment leaving a keyframe, so the last one has nothing
    // to reach and its curve does nothing at all. An inert control says so by reading inert.
    const KeyframeTimeline* timeline = Timeline();
    const int count = timeline ? static_cast<int>(timeline->keyframes.size()) : 0;
    const bool hasNext = (m_selected >= 0) && (m_selected < count - 1);
    m_curveLabel->setEnabled(hasNext);
    m_curve->setEnabled(hasNext);
    m_bezier->setEnabled(hasNext);

    { const QSignalBlocker block(m_curve);
      m_curve->setCurrentIndex(static_cast<int>(kf->mode)); }
    m_bezier->SetCurve(kf->mode, kf->handles);
    UpdateCurveCaption();
}

void KeyframeDetail::SyncValueEditor(const Keyframe& kf)
{
    const ShaderParam* param = Param();
    if (!param) return;

    switch (m_type) {

    case ShaderParamType::Float: {
        const QSignalBlocker blockSlider(m_slider);
        const QSignalBlocker blockSpin(m_spin[0]);
        m_slider->setValue(ValueToTick(kf.values[0], *param));
        m_spin[0]->setValue(kf.values[0]);
        break;
    }

    case ShaderParamType::Bool: {
        const QSignalBlocker block(m_check);
        m_check->setChecked(kf.values[0] > 0.5f);
        m_check->setText(kf.values[0] > 0.5f ? tr("On") : tr("Off"));
        break;
    }

    case ShaderParamType::Long: {
        const QSignalBlocker block(m_choice);
        const int current = static_cast<int>(kf.values[0]);
        int index = param->longValues.empty() ? current : 0;
        for (int i = 0; i < static_cast<int>(param->longValues.size()); ++i) {
            if (param->longValues[static_cast<size_t>(i)] == current) { index = i; break; }
        }
        if (index >= 0 && index < m_choice->count()) m_choice->setCurrentIndex(index);
        break;
    }

    case ShaderParamType::Color: {
        const QColor colour = ColourOf(kf.values);
        m_swatch->setIcon(QIcon(SwatchPixmap(colour, devicePixelRatioF())));
        m_swatch->setToolTip(tr("%1 at %2% opacity. Click to change this keyframe's "
                                "colour.")
                                 .arg(colour.name(QColor::HexRgb).toUpper())
                                 .arg(static_cast<int>(Clamp01(kf.values[3]) * 100.0f + 0.5f)));
        break;
    }

    case ShaderParamType::Point2D:
        for (int axis = 0; axis < 2; ++axis) {
            const QSignalBlocker block(m_spin[axis]);
            m_spin[axis]->setValue(kf.values[axis]);
        }
        break;

    case ShaderParamType::Event:
    case ShaderParamType::AudioBand:
        break;
    }
}

void KeyframeDetail::UpdateCurveCaption()
{
    const Keyframe* kf = Selected();
    if (!kf) return;

    const KeyframeTimeline* timeline = Timeline();
    const int count = timeline ? static_cast<int>(timeline->keyframes.size()) : 0;
    if (m_selected >= count - 1) {
        m_caption->setText(tr("Last keyframe on the track: there is nothing after it to "
                              "travel towards, so the curve is idle."));
        return;
    }

    QString text;
    switch (kf->mode) {
    case InterpolationMode::Linear:
        text = tr("A straight run to the next keyframe at a constant rate.");
        break;
    case InterpolationMode::EaseInOut:
        text = tr("Leaves this keyframe slowly and arrives at the next one slowly.");
        break;
    case InterpolationMode::CubicBezier:
        text = tr("Drag either handle to shape the timing.");
        break;
    }

    // Both types snap on the way through, so the curve still decides when each step lands
    // even though the value never sits between two of them.
    if (m_type == ShaderParamType::Bool || m_type == ShaderParamType::Long) {
        text += QLatin1Char(' ');
        text += tr("The value steps rather than sliding, so the curve sets when each step "
                   "lands.");
    }
    m_caption->setText(text);
}

// ---------------------------------------------------------------------------------------
// Mutation
//
// Everything below changes the timeline, and every one of them ends in Mutated().
// ---------------------------------------------------------------------------------------

void KeyframeDetail::Mutated()
{
    emit Changed();
}

void KeyframeDetail::AddKeyAtPlayhead()
{
    ShaderParam* param = Param();
    KeyframeTimeline* timeline = Timeline();
    if (!param || !timeline) return;

    Keyframe kf;
    kf.time = m_app.GetPlaybackTime();   // a fresh local: nothing is in the vector yet
    std::copy(param->values, param->values + 4, kf.values);

    m_selected = timeline->AddKeyframe(kf);
    Refresh();
    emit SelectionRequested(m_index, m_selected);
    Mutated();
}

void KeyframeDetail::SeekToKeyframe(int index)
{
    const KeyframeTimeline* timeline = Timeline();
    if (!timeline || index < 0 || index >= static_cast<int>(timeline->keyframes.size())) {
        return;
    }
    const float time = timeline->keyframes[static_cast<size_t>(index)].time;

    m_selected = index;
    Refresh();
    emit SelectionRequested(m_index, index);
    m_app.SeekTo(time);
    // No Mutated(): seeking and selecting change nothing in the timeline.
}

void KeyframeDetail::RepositionSelected(float newTime)
{
    KeyframeTimeline* timeline = Timeline();
    const Keyframe* selected = Selected();
    if (!timeline || !selected) return;

    newTime = std::max(0.0f, newTime);
    if (selected->time == newTime) return;   // nothing moved, so nothing to report

    // The sorted order of KeyframeTimeline::keyframes is maintained only through
    // RemoveKeyframe / AddKeyframe. Writing a time into a keyframe still sitting in that
    // vector leaves it out of order and the evaluator's binary search reads the wrong
    // segment, so the move goes through a copy and the index AddKeyframe hands back.
    Keyframe moved = *selected;
    moved.time = newTime;
    timeline->RemoveKeyframe(m_selected);
    m_selected = timeline->AddKeyframe(moved);

    Refresh();
    emit SelectionRequested(m_index, m_selected);
    Mutated();
}

void KeyframeDetail::DeleteSelected()
{
    KeyframeTimeline* timeline = Timeline();
    if (!timeline || !Selected()) return;

    timeline->RemoveKeyframe(m_selected);
    m_selected = -1;

    Refresh();
    // Nothing is selected anywhere now, which is what the outgoing panel left behind too:
    // the parameter loses the selection along with the keyframe that carried it.
    emit SelectionRequested(-1, -1);
    Mutated();
}

}  // namespace SP
