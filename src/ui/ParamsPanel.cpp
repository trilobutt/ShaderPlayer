#include "ui/ParamsPanel.h"

#include "Application.h"
#include "ui/KeyframeDetail.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace SP {

namespace {

// The label column is what makes fifteen rows scannable: it is the same width whatever the
// row's type, so every control starts on one vertical edge and the eye runs down names on
// the left and values on the right without either column wandering.
constexpr int kLabelColumn  = 118;
constexpr int kValueColumn  = 86;   // the spin box beside a slider, and the Point2D pair
constexpr int kSwatchWidth  = 68;
constexpr int kSwatchHeight = 22;
constexpr int kButtonColumn = 30;   // R and KF
constexpr int kResetColumn  = 22;   // the reset arrow: narrower, because it is the quietest
constexpr int kMeterSteps   = 1000; // QProgressBar is integral; the bands are [0,1]

// Video blend amount is [0,1] and reads to two decimals, so a hundred stops is exactly the
// resolution the readout can show.
constexpr int kBlendTicks   = 100;

// Grid columns, in the order a row reads left to right.
enum Column { kColLabel = 0, kColCell, kColReset, kColRandomise, kColKeyframe, kColCount };

// Beyond this the slider is finer than the screen and the extra ticks only cost precision
// in the round trip through integers.
constexpr int kMaxTicks     = 10000;
constexpr int kFreeTicks    = 1000; // a parameter that declared no STEP

// A beat is an event rather than a level, so it takes a colour the steady bands do not.
constexpr float kBeatVisible = 0.1f;

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

int TicksFor(const ShaderParam& param)
{
    const double range = static_cast<double>(param.max) - static_cast<double>(param.min);
    if (!(range > 0.0)) return 1;             // a degenerate range: the slider is inert
    if (param.step > 0.0f) {
        const long steps = std::lround(range / static_cast<double>(param.step));
        return std::clamp(static_cast<int>(steps), 1, kMaxTicks);
    }
    return kFreeTicks;
}

double SnapToStep(double v, const ShaderParam& param)
{
    if (param.step > 0.0f) {
        v = std::round(v / static_cast<double>(param.step)) * static_cast<double>(param.step);
    }
    return std::clamp(v, static_cast<double>(param.min), static_cast<double>(param.max));
}

double TickToValue(int tick, int ticks, const ShaderParam& param)
{
    const double t = (ticks > 0) ? static_cast<double>(tick) / ticks : 0.0;
    return static_cast<double>(param.min)
         + t * (static_cast<double>(param.max) - static_cast<double>(param.min));
}

int ValueToTick(double v, int ticks, const ShaderParam& param)
{
    const double range = static_cast<double>(param.max) - static_cast<double>(param.min);
    if (!(range > 0.0)) return 0;
    const double t = (v - static_cast<double>(param.min)) / range;
    return std::clamp(static_cast<int>(std::lround(t * ticks)), 0, ticks);
}

QColor ColourOf(const ShaderParam& param)
{
    return QColor::fromRgbF(Clamp01(param.values[0]), Clamp01(param.values[1]),
                            Clamp01(param.values[2]), Clamp01(param.values[3]));
}

float LiveBand(const AudioData& audio, const std::string& band)
{
    if (band == "bass")     return audio.bass;
    if (band == "mid")      return audio.mid;
    if (band == "high")     return audio.high;
    if (band == "rms")      return audio.rms;
    if (band == "beat")     return audio.beat;
    if (band == "centroid") return audio.spectralCentroid;
    return 0.0f;
}

// The one line the whole panel repeats: what a parameter's range actually is, in the words
// the shader author wrote it in.
QString RangeTip(const ShaderParam& param)
{
    return QObject::tr("%1  ·  range %2 to %3")
        .arg(QString::fromStdString(param.name))
        .arg(param.min, 0, 'g', 4)
        .arg(param.max, 0, 'g', 4);
}

} // namespace

// ---------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------

ParamsPanel::ParamsPanel(Application& app, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
{
    setObjectName(QStringLiteral("ParamsPanel"));

    // The application stylesheet paints every QWidget with the canvas, which is right for
    // the window and wrong for a container sitting inside a glass panel: it would lay an
    // opaque black rectangle over the panel fill. Named containers opt out; the controls
    // keep their own recessed fills from the application stylesheet untouched.
    //
    // The reset arrow is the quietest control on a row, and on a fifteen-row shader four
    // buttons abreast at full strength would be all the eye sees. Disabled (which is most
    // of the time, because most parameters sit at their default) it recedes to a hint of
    // itself rather than taking the ordinary disabled grey the other controls use.
    QColor resetDim = Theme::kTextPrimary;
    resetDim.setAlpha(55);
    setStyleSheet(QStringLiteral(
        "QWidget#ParamsPanel, QWidget#ParamsHeader, QWidget#ParamsStack, QWidget#ParamsPage,"
        "QWidget#ParamsViewport, QWidget#ParamsContent, QWidget#ParamCell,"
        "QWidget#ParamDetailHost, QWidget#ParamsBlend, QWidget#ParamsBlendRow"
        " { background: transparent; }"
        "QScrollArea#ParamsScroll { background: transparent; border: none; }"
        "QToolButton#ParamReset { padding: 2px; }"
        "QToolButton#ParamReset:disabled { color: rgba(%1, %2, %3, %4); }")
        .arg(resetDim.red()).arg(resetDim.green()).arg(resetDim.blue()).arg(resetDim.alpha()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kSpaceUnit);

    // ---- header ---------------------------------------------------------------------
    m_header = new QWidget(this);
    m_header->setObjectName(QStringLiteral("ParamsHeader"));
    auto* headerLayout = new QVBoxLayout(m_header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(Theme::kSpaceUnit);

    m_summary = new QLabel(m_header);
    m_summary->setObjectName(QStringLiteral("Caption"));
    headerLayout->addWidget(m_summary);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(Theme::kSpaceUnit);

    auto* randomiseAll = new QPushButton(tr("Randomise All"), m_header);
    randomiseAll->setToolTip(tr("Roll every parameter inside the range its own control "
                                "offers.\nKeyframed parameters and colour alpha are left "
                                "alone."));
    connect(randomiseAll, &QPushButton::clicked, this, &ParamsPanel::RandomiseAll);
    actions->addWidget(randomiseAll);

    auto* resetAll = new QPushButton(tr("Reset to Defaults"), m_header);
    resetAll->setToolTip(tr("Restore every parameter to the default the shader declares."));
    connect(resetAll, &QPushButton::clicked, this, &ParamsPanel::ResetAll);
    actions->addWidget(resetAll);

    actions->addStretch(1);

    // A shortcut out of this panel rather than a third thing to do inside it, so it sits
    // apart from the two panel actions and carries the Noise dock's own hue and glyph: the
    // button looks like the place it opens before the label is read.
    m_noiseShortcut = new QPushButton(tr("Noise Generator..."), m_header);
    m_noiseShortcut->setIcon(Theme::RegionIcon(QStringLiteral("noise"), Theme::kRegionNoise));
    m_noiseShortcut->setCursor(Qt::PointingHandCursor);
    m_noiseShortcut->setToolTip(tr("This shader samples the global noise texture (t1).\n"
                                   "Open the Noise Generator to change its scale and "
                                   "regenerate it."));
    m_noiseShortcut->setVisible(false);
    connect(m_noiseShortcut, &QPushButton::clicked, this,
            [this] { emit NoiseGeneratorRequested(); });
    actions->addWidget(m_noiseShortcut);

    headerLayout->addLayout(actions);
    layout->addWidget(m_header);

    // ---- the rows, and what stands in for them ---------------------------------------
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("ParamsStack"));

    m_scroll = new QScrollArea(m_stack);
    m_scroll->setObjectName(QStringLiteral("ParamsScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setObjectName(QStringLiteral("ParamsViewport"));
    m_stack->addWidget(m_scroll);                      // kPageParams

    // Passthrough is a state the user chose, not a failure, so it reads as one.
    auto* passthrough = new QWidget(m_stack);
    passthrough->setObjectName(QStringLiteral("ParamsPage"));
    {
        auto* page = new QVBoxLayout(passthrough);
        page->setContentsMargins(Theme::kSpaceUnit, Theme::kSpaceUnit,
                                 Theme::kSpaceUnit, Theme::kSpaceUnit);
        page->setSpacing(0);
        page->addStretch(2);

        auto* heading = new QLabel(tr("No shader active"), passthrough);
        QFont headingFont(QString::fromLatin1(Theme::kFontUi));
        headingFont.setPointSize(Theme::kFontSizePanelTitle + 3);
        headingFont.setBold(true);
        heading->setFont(headingFont);
        heading->setAlignment(Qt::AlignCenter);
        heading->setWordWrap(true);
        page->addWidget(heading);
        page->addSpacing(Theme::kSpaceUnit);

        auto* blurb = new QLabel(
            tr("The picture is passing through untouched. Choose a shader in the Shader "
               "Library and its controls arrive here."), passthrough);
        blurb->setObjectName(QStringLiteral("Caption"));
        blurb->setAlignment(Qt::AlignCenter);
        blurb->setWordWrap(true);
        page->addWidget(blurb);
        page->addStretch(3);
    }
    m_stack->addWidget(passthrough);                   // kPagePassthrough

    // A shader with no INPUTS block is a working shader, and this says so rather than
    // leaving the panel blank enough to read as broken.
    auto* noParams = new QWidget(m_stack);
    noParams->setObjectName(QStringLiteral("ParamsPage"));
    {
        auto* page = new QVBoxLayout(noParams);
        page->setContentsMargins(Theme::kSpaceUnit, Theme::kSpaceUnit,
                                 Theme::kSpaceUnit, Theme::kSpaceUnit);
        page->setSpacing(0);
        page->addStretch(2);

        m_noParams = new QLabel(noParams);
        QFont headingFont(QString::fromLatin1(Theme::kFontUi));
        headingFont.setPointSize(Theme::kFontSizePanelTitle + 3);
        headingFont.setBold(true);
        m_noParams->setFont(headingFont);
        m_noParams->setAlignment(Qt::AlignCenter);
        m_noParams->setWordWrap(true);
        page->addWidget(m_noParams);
        page->addSpacing(Theme::kSpaceUnit);

        auto* blurb = new QLabel(
            tr("It declares no INPUTS, so there is nothing to dial. Add an ISF INPUTS "
               "block in the Shader Editor and the controls appear here on the next "
               "compile."), noParams);
        blurb->setObjectName(QStringLiteral("Caption"));
        blurb->setAlignment(Qt::AlignCenter);
        blurb->setWordWrap(true);
        page->addWidget(blurb);
        page->addStretch(3);
    }
    m_stack->addWidget(noParams);                      // kPageNoParams

    layout->addWidget(m_stack, 1);

    BuildBlendSection(layout);

    SetPreset(nullptr);
}

// ---------------------------------------------------------------------------------------
// Video blend
//
// Not a parameter: the mode and amount belong to the preset and reach the shader through
// D3D11Renderer::SetVideoBlend, which Application reads from the active preset every frame.
// So this block writes the preset and persists through SaveConfig, and never through the
// Changed() route the parameter rows share.
//
// It is shown whenever a video or live source is open, whatever the shader is. A generative
// pattern over footage and an audio visualiser over footage are both things people do here,
// so gating this on the shader's type would take the feature away from the shaders that use
// it most.
// ---------------------------------------------------------------------------------------

void ParamsPanel::BuildBlendSection(QVBoxLayout* layout)
{
    m_blend = new QWidget(this);
    m_blend->setObjectName(QStringLiteral("ParamsBlend"));
    auto* block = new QVBoxLayout(m_blend);
    block->setContentsMargins(0, Theme::kSpaceUnit, Theme::kSpaceUnit, 0);
    block->setSpacing(Theme::kSpaceUnit);

    // A real divider, because a real separation exists: everything above this line is the
    // shader's own vocabulary and everything below it is how that output meets the video.
    auto* rule = new QFrame(m_blend);
    rule->setObjectName(QStringLiteral("PanelHairline"));
    block->addWidget(rule);

    auto* title = new QLabel(tr("VIDEO BLEND"), m_blend);
    title->setObjectName(QStringLiteral("SectionTitle"));
    block->addWidget(title);

    // ---- mode ------------------------------------------------------------------------
    auto* modeRow = new QWidget(m_blend);
    modeRow->setObjectName(QStringLiteral("ParamsBlendRow"));
    auto* mode = new QHBoxLayout(modeRow);
    mode->setContentsMargins(0, 0, 0, 0);
    mode->setSpacing(Theme::kSpaceUnit);

    auto* modeLabel = new QLabel(tr("Mode"), modeRow);
    modeLabel->setFixedWidth(kLabelColumn);
    mode->addWidget(modeLabel, 0);

    m_blendMode = new QComboBox(modeRow);
    // The eleven the compositor implements, in the order D3D11Renderer indexes them:
    // ShaderPreset::blendMode stores the index, so a reordering here changes what a saved
    // config means.
    m_blendMode->addItems(QStringList{
        tr("Off"), tr("Normal"), tr("Add"), tr("Multiply"), tr("Screen"), tr("Overlay"),
        tr("Soft Light"), tr("Difference"), tr("Exclusion"), tr("Darken"), tr("Lighten")});
    m_blendMode->setToolTip(tr("How the shader's output meets the video underneath it.\n"
                               "Off draws the shader straight to the display."));
    mode->addWidget(m_blendMode, 1);
    block->addWidget(modeRow);

    // ---- amount ----------------------------------------------------------------------
    m_blendAmountRow = new QWidget(m_blend);
    m_blendAmountRow->setObjectName(QStringLiteral("ParamsBlendRow"));
    auto* amount = new QHBoxLayout(m_blendAmountRow);
    amount->setContentsMargins(0, 0, 0, 0);
    amount->setSpacing(Theme::kSpaceUnit);

    auto* amountLabel = new QLabel(tr("Amount"), m_blendAmountRow);
    amountLabel->setFixedWidth(kLabelColumn);
    amount->addWidget(amountLabel, 0);

    const QString amountTip = tr("How far the video is carried toward the blended result, "
                                 "0 to 1.\nThe shader's own output alpha scales this per "
                                 "pixel.");

    m_blendAmount = new QSlider(Qt::Horizontal, m_blendAmountRow);
    m_blendAmount->setRange(0, kBlendTicks);
    m_blendAmount->setToolTip(amountTip);
    amount->addWidget(m_blendAmount, 1);

    m_blendAmountSpin = new QDoubleSpinBox(m_blendAmountRow);
    m_blendAmountSpin->setDecimals(2);
    m_blendAmountSpin->setRange(0.0, 1.0);
    m_blendAmountSpin->setSingleStep(0.01);
    m_blendAmountSpin->setKeyboardTracking(false);
    m_blendAmountSpin->setFixedWidth(kValueColumn);
    m_blendAmountSpin->setToolTip(amountTip);
    amount->addWidget(m_blendAmountSpin, 0);
    block->addWidget(m_blendAmountRow);

    connect(m_blendMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_preset || index < 0) return;
        m_preset->blendMode = index;
        // Off has no amount to set, and an inert slider beside it would only invite the
        // question of why it does nothing.
        m_blendAmountRow->setVisible(index > 0);
        SaveBlend();
    });

    connect(m_blendAmount, &QSlider::valueChanged, this, [this](int tick) {
        if (!m_preset) return;
        m_preset->blendAmount = static_cast<float>(tick) / kBlendTicks;
        { const QSignalBlocker block(m_blendAmountSpin);
          m_blendAmountSpin->setValue(m_preset->blendAmount); }
        // The value applies live (the next frame reads it off the preset), but the config
        // is written once the drag settles: the outgoing panel saved on every frame of a
        // drag, which is a file write per frame for as long as the mouse is down.
        if (!m_blendAmount->isSliderDown()) SaveBlend();
    });
    connect(m_blendAmount, &QSlider::sliderReleased, this, &ParamsPanel::SaveBlend);

    connect(m_blendAmountSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!m_preset) return;
        m_preset->blendAmount = static_cast<float>(std::clamp(v, 0.0, 1.0));
        { const QSignalBlocker block(m_blendAmount);
          m_blendAmount->setValue(static_cast<int>(std::lround(m_preset->blendAmount
                                                              * kBlendTicks))); }
        SaveBlend();
    });

    m_blend->setVisible(false);
    layout->addWidget(m_blend);
}

void ParamsPanel::SaveBlend()
{
    m_app.SaveConfig();
}

void ParamsPanel::SyncBlend()
{
    if (!m_preset) return;

    const int mode = std::clamp(m_preset->blendMode, 0, m_blendMode->count() - 1);
    {
        const QSignalBlocker blockMode(m_blendMode);
        const QSignalBlocker blockSlider(m_blendAmount);
        const QSignalBlocker blockSpin(m_blendAmountSpin);
        m_blendMode->setCurrentIndex(mode);
        m_blendAmount->setValue(
            static_cast<int>(std::lround(Clamp01(m_preset->blendAmount) * kBlendTicks)));
        m_blendAmountSpin->setValue(Clamp01(m_preset->blendAmount));
    }
    m_blendAmountRow->setVisible(mode > 0);
}

void ParamsPanel::UpdateBlendVisibility()
{
    // Exactly the gate CLAUDE.md states: a source is open. A preset is needed too only
    // because the mode and amount are stored on one, not as a second condition on the
    // kind of shader it is.
    const bool show = (m_preset != nullptr) && m_app.GetDecoder().IsOpen();
    if (show == m_blendShown) return;   // isVisible() would also answer false for a closed dock
    m_blendShown = show;
    m_blend->setVisible(show);          // a hidden widget takes no space and no layout spacing
}

// ---------------------------------------------------------------------------------------
// Rebuild
// ---------------------------------------------------------------------------------------

ShaderParam* ParamsPanel::Param(int index) const
{
    if (!m_preset) return nullptr;
    if (index < 0 || index >= static_cast<int>(m_preset->params.size())) return nullptr;
    return &m_preset->params[static_cast<size_t>(index)];
}

void ParamsPanel::SetPreset(ShaderPreset* preset)
{
    // A recompile hands back the same shader with a fresh parameter vector, and losing the
    // reader's place in a fifteen-row panel on every compile would be its own small tax.
    // A different shader starts at the top, with no selection carried over.
    const QString name = preset ? QString::fromStdString(preset->name) : QString();
    const bool sameShader = (preset != nullptr) && (name == m_shownName)
                         && (preset == m_preset);
    const int scrolled = sameShader ? m_scroll->verticalScrollBar()->value() : 0;

    m_preset = preset;
    m_shownName = name;

    if (!sameShader) {
        m_selectedKeyframeParam = -1;
        m_selectedKeyframeIndex = -1;
        emit KeyframeSelectionChanged(-1, -1);
    }

    BuildRows();

    // Both of these follow the preset rather than its parameter list, so they are settled
    // before the two pages that have no rows return early.
    m_noiseShortcut->setVisible(
        preset != nullptr && preset->source.find("noiseTexture") != std::string::npos);
    SyncBlend();
    UpdateBlendVisibility();

    if (!preset) {
        m_header->setVisible(false);
        m_stack->setCurrentIndex(kPagePassthrough);
        return;
    }
    if (preset->params.empty()) {
        m_header->setVisible(false);
        m_noParams->setText(name.isEmpty() ? tr("This shader has no parameters")
                                           : tr("%1 has no parameters").arg(name));
        m_stack->setCurrentIndex(kPageNoParams);
        return;
    }

    const int count = static_cast<int>(preset->params.size());
    m_summary->setText(name.isEmpty()
        ? tr("%n parameter(s)", nullptr, count)
        : tr("%1  ·  %n parameter(s)", nullptr, count).arg(name));
    m_header->setVisible(true);
    m_stack->setCurrentIndex(kPageParams);

    // Clamp a stale selection the same way the outgoing panel did, rather than indexing a
    // different shader's vector with it.
    if (m_selectedKeyframeParam >= count) {
        m_selectedKeyframeParam = -1;
        m_selectedKeyframeIndex = -1;
        emit KeyframeSelectionChanged(-1, -1);
    }

    SyncValues();

    m_content->adjustSize();
    m_scroll->verticalScrollBar()->setValue(scrolled);
}

void ParamsPanel::BuildRows()
{
    m_rows.clear();

    // Wholesale replacement: a partial teardown leaves stale connections pointing at rows
    // whose parameter index no longer means the same thing.
    m_content = new QWidget(m_scroll);
    m_content->setObjectName(QStringLiteral("ParamsContent"));

    auto* grid = new QGridLayout(m_content);
    grid->setContentsMargins(0, 0, Theme::kSpaceUnit, Theme::kSpaceUnit);
    grid->setHorizontalSpacing(Theme::kSpaceUnit);
    grid->setVerticalSpacing(Theme::kSpaceUnit);
    grid->setColumnMinimumWidth(kColLabel, kLabelColumn);
    grid->setColumnStretch(kColLabel, 0);
    grid->setColumnStretch(kColCell, 1);

    if (m_preset) {
        const int count = static_cast<int>(m_preset->params.size());
        m_rows.reserve(count);
        for (int i = 0; i < count; ++i) BuildRow(grid, i, i * 2);
    }
    grid->setRowStretch(grid->rowCount(), 1);   // rows keep their height; the slack is below

    m_scroll->setWidget(m_content);             // takes ownership, deletes the old content

    // Horizontal scrolling is off by design: a parameters list that scrolls sideways hides
    // the controls it exists to show. The consequence is that the panel has to refuse to be
    // narrower than one row needs, because a QScrollArea with no horizontal bar simply
    // clips the overflow instead — and the column that goes first is the rightmost, which
    // is the KF toggle. At the shipped dock width that toggle was cut in half by the
    // vertical scrollbar and there was no width at which it could be reached.
    //
    // The width is taken from the layout rather than from a hand-summed constant so it
    // cannot drift when a column is added or resized, and the scrollbar's own extent is
    // added because setWidgetResizable(true) sizes the content to the viewport, which is
    // already that much narrower than the panel.
    const int scrollbar = m_scroll->verticalScrollBar()->sizeHint().width();
    m_scroll->setMinimumWidth(m_content->minimumSizeHint().width() + scrollbar);
}

void ParamsPanel::BuildRow(QGridLayout* grid, int index, int gridRow)
{
    ShaderParam& param = m_preset->params[static_cast<size_t>(index)];

    Row row;
    row.index = index;
    row.type = param.type;

    const QString text = QString::fromStdString(param.label.empty() ? param.name
                                                                    : param.label);
    row.label = new QLabel(text, m_content);
    row.label->setWordWrap(true);
    row.label->setToolTip(RangeTip(param));
    row.label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(row.label, gridRow, kColLabel);

    row.cell = new QWidget(m_content);
    row.cell->setObjectName(QStringLiteral("ParamCell"));
    auto* cell = new QHBoxLayout(row.cell);
    cell->setContentsMargins(0, 0, 0, 0);
    cell->setSpacing(Theme::kSpaceUnit);
    grid->addWidget(row.cell, gridRow, kColCell);

    const int decimals = DecimalsFor(param.step);

    switch (param.type) {

    case ShaderParamType::Float: {
        row.ticks = TicksFor(param);

        row.slider = new QSlider(Qt::Horizontal, row.cell);
        row.slider->setRange(0, row.ticks);
        row.slider->setToolTip(RangeTip(param));
        cell->addWidget(row.slider, 1);

        // The number lives beside the slider and is editable there: a slider alone cannot
        // be read to four decimals, and a tooltip is not a readout.
        row.spin[0] = new QDoubleSpinBox(row.cell);
        row.spin[0]->setDecimals(decimals);
        row.spin[0]->setRange(param.min, param.max);
        row.spin[0]->setSingleStep(param.step > 0.0f ? param.step : 0.01);
        row.spin[0]->setKeyboardTracking(false);
        row.spin[0]->setFixedWidth(kValueColumn);
        row.spin[0]->setToolTip(RangeTip(param));
        cell->addWidget(row.spin[0], 0);

        QSlider* slider = row.slider;
        QDoubleSpinBox* spin = row.spin[0];
        const int ticks = row.ticks;

        connect(slider, &QSlider::valueChanged, this, [this, index, ticks, spin](int tick) {
            ShaderParam* p = Param(index);
            if (!p) return;
            const double v = SnapToStep(TickToValue(tick, ticks, *p), *p);
            p->values[0] = static_cast<float>(v);
            { const QSignalBlocker block(spin); spin->setValue(v); }
            Changed();
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, index, ticks, slider](double v) {
            ShaderParam* p = Param(index);
            if (!p) return;
            p->values[0] = static_cast<float>(SnapToStep(v, *p));
            { const QSignalBlocker block(slider);
              slider->setValue(ValueToTick(p->values[0], ticks, *p)); }
            Changed();
        });
        break;
    }

    case ShaderParamType::Bool: {
        row.check = new QCheckBox(row.cell);
        row.check->setToolTip(tr("%1  ·  on or off")
                                  .arg(QString::fromStdString(param.name)));
        cell->addWidget(row.check, 0);
        cell->addStretch(1);

        connect(row.check, &QCheckBox::toggled, this, [this, index](bool on) {
            ShaderParam* p = Param(index);
            if (!p) return;
            p->values[0] = on ? 1.0f : 0.0f;
            Changed();
        });
        break;
    }

    case ShaderParamType::Long: {
        row.combo = new QComboBox(row.cell);
        // longValues holds the values the shader sees and longLabels the words the user
        // reads: parallel arrays, and the combo stores the value, never the index.
        const int entries = static_cast<int>(std::max(param.longValues.size(),
                                                      param.longLabels.size()));
        for (int i = 0; i < entries; ++i) {
            const QString entry = (i < static_cast<int>(param.longLabels.size()))
                ? QString::fromStdString(param.longLabels[static_cast<size_t>(i)])
                : QString::number(param.longValues[static_cast<size_t>(i)]);
            row.combo->addItem(entry);
        }
        if (entries == 0) {
            // An ISF `long` written with MIN/MAX but no VALUES parses into an empty list,
            // and an empty combo with no explanation is indistinguishable from a bug.
            row.combo->setEnabled(false);
            row.combo->setToolTip(tr("This dropdown declares no VALUES, so it has nothing "
                                     "to offer. Add a VALUES array to the shader's ISF "
                                     "block."));
        } else {
            row.combo->setToolTip(QString::fromStdString(param.name));
        }
        cell->addWidget(row.combo, 1);

        connect(row.combo, &QComboBox::currentIndexChanged, this, [this, index](int i) {
            ShaderParam* p = Param(index);
            if (!p || i < 0) return;
            p->values[0] = static_cast<float>(
                (i < static_cast<int>(p->longValues.size()))
                    ? p->longValues[static_cast<size_t>(i)]
                    : i);
            Changed();
        });
        break;
    }

    case ShaderParamType::Color: {
        row.swatch = new QPushButton(row.cell);
        row.swatch->setFixedSize(kSwatchWidth + Theme::kSpaceUnit * 2,
                                 kSwatchHeight + Theme::kSpaceUnit);
        row.swatch->setIconSize(QSize(kSwatchWidth, kSwatchHeight));
        row.swatch->setCursor(Qt::PointingHandCursor);
        cell->addWidget(row.swatch, 0);

        // The hex reads at a glance and does not move; alpha is spelled out because a
        // shader treats it as opacity and a swatch alone hides that.
        row.swatchText = new QLabel(row.cell);
        row.swatchText->setObjectName(QStringLiteral("Mono"));
        cell->addWidget(row.swatchText, 0);
        cell->addStretch(1);

        connect(row.swatch, &QPushButton::clicked, this, [this, index] {
            ShaderParam* p = Param(index);
            if (!p) return;
            const QColor picked = QColorDialog::getColor(
                ColourOf(*p), this,
                tr("Choose %1").arg(QString::fromStdString(
                    p->label.empty() ? p->name : p->label)),
                QColorDialog::ShowAlphaChannel);
            if (!picked.isValid()) return;
            p->values[0] = static_cast<float>(picked.redF());
            p->values[1] = static_cast<float>(picked.greenF());
            p->values[2] = static_cast<float>(picked.blueF());
            p->values[3] = static_cast<float>(picked.alphaF());
            SyncValues();
            Changed();
        });
        break;
    }

    case ShaderParamType::Point2D: {
        static const char* const kAxis[2] = { "X ", "Y " };
        for (int axis = 0; axis < 2; ++axis) {
            auto* spin = new QDoubleSpinBox(row.cell);
            spin->setDecimals(decimals);
            spin->setRange(param.min, param.max);
            spin->setSingleStep(param.step > 0.0f ? param.step : 0.01);
            spin->setKeyboardTracking(false);
            spin->setPrefix(QString::fromLatin1(kAxis[axis]));
            spin->setMinimumWidth(kValueColumn);
            spin->setToolTip(RangeTip(param));
            cell->addWidget(spin, 1);
            row.spin[axis] = spin;

            connect(spin, &QDoubleSpinBox::valueChanged, this,
                    [this, index, axis](double v) {
                ShaderParam* p = Param(index);
                if (!p) return;
                p->values[axis] = static_cast<float>(
                    std::clamp(v, static_cast<double>(p->min), static_cast<double>(p->max)));
                Changed();
            });
        }
        break;
    }

    case ShaderParamType::Event: {
        row.trigger = new QPushButton(tr("Trigger"), row.cell);
        row.trigger->setToolTip(tr("Fire %1 for one frame.")
                                    .arg(QString::fromStdString(param.name)));
        cell->addWidget(row.trigger, 0);
        cell->addStretch(1);

        connect(row.trigger, &QPushButton::clicked, this, [this, index] {
            ShaderParam* p = Param(index);
            if (!p) return;
            // Application clears it after the next RenderFrame; clearing it here would
            // race that and the shader would never see the pulse.
            p->values[0] = 1.0f;
            Changed();
        });
        break;
    }

    case ShaderParamType::AudioBand: {
        row.meter = new QProgressBar(row.cell);
        row.meter->setRange(0, kMeterSteps);
        row.meter->setTextVisible(false);
        row.meter->setToolTip(tr("Live %1 from the audio analyser. Read-only: the signal "
                                 "sets it, not you.")
                                  .arg(QString::fromStdString(param.audioBand)));
        cell->addWidget(row.meter, 1);

        // A live number that changes sixty times a second has to hold its column, which is
        // the one place in this panel monospace is the right answer.
        row.meterValue = new QLabel(row.cell);
        row.meterValue->setObjectName(QStringLiteral("Mono"));
        row.meterValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row.meterValue->setFixedWidth(kValueColumn / 2);
        cell->addWidget(row.meterValue, 0);
        break;
    }

    } // switch

    // Event fires rather than holds and AudioBand is written by the analyser, so neither
    // has a value to roll or to animate.
    const bool userDriven = (param.type != ShaderParamType::Event)
                         && (param.type != ShaderParamType::AudioBand);
    if (userDriven) {
        // An arrow drawn by the style, not a glyph from the font: the UI font is ASCII-only
        // and a Unicode arrow would land as a box on the one machine it is not.
        row.reset = new QToolButton(m_content);
        row.reset->setObjectName(QStringLiteral("ParamReset"));
        row.reset->setArrowType(Qt::LeftArrow);
        row.reset->setFixedWidth(kResetColumn);
        row.reset->setToolTip(tr("Reset to default"));
        grid->addWidget(row.reset, gridRow, kColReset);

        connect(row.reset, &QToolButton::clicked, this, [this, index] {
            ShaderParam* p = Param(index);
            if (!p) return;
            std::copy(p->defaultValues, p->defaultValues + 4, p->values);
            SyncValues();
            Changed();
        });

        row.randomise = new QToolButton(m_content);
        row.randomise->setText(tr("R"));
        row.randomise->setFixedWidth(kButtonColumn);
        row.randomise->setToolTip(tr("Randomise this parameter inside its range."));
        grid->addWidget(row.randomise, gridRow, kColRandomise);

        connect(row.randomise, &QToolButton::clicked, this, [this, index] {
            ShaderParam* p = Param(index);
            if (!p) return;
            if (RandomiseParam(*p)) {
                SyncValues();
                Changed();
            }
        });

        row.keyframe = new QToolButton(m_content);
        row.keyframe->setText(tr("KF"));
        row.keyframe->setCheckable(true);
        row.keyframe->setFixedWidth(kButtonColumn);
        row.keyframe->setToolTip(tr("Animate this parameter over the video's timeline."));
        grid->addWidget(row.keyframe, gridRow, kColKeyframe);

        // The keyframe track: full width, beneath the row it belongs to, and visible for
        // exactly as long as this parameter's KF toggle is on.
        row.detailHost = new QWidget(m_content);
        row.detailHost->setObjectName(QStringLiteral("ParamDetailHost"));
        auto* host = new QVBoxLayout(row.detailHost);
        host->setContentsMargins(Theme::kSpaceUnit * 2, 0, 0, 0);
        host->setSpacing(Theme::kSpaceUnit);
        grid->addWidget(row.detailHost, gridRow + 1, kColLabel, 1, kColCount);

        row.detail = new KeyframeDetail(m_app, m_preset, index, row.detailHost);
        host->addWidget(row.detail);

        // Every mutation inside the track ends here, which is the same one route the
        // parameter widgets take to Application::OnParamChanged.
        connect(row.detail, &KeyframeDetail::Changed, this, &ParamsPanel::Changed);
        connect(row.detail, &KeyframeDetail::SelectionRequested,
                this, &ParamsPanel::SetKeyframeSelection);

        const bool animated = param.timeline.has_value() && param.timeline->enabled;
        row.detailHost->setVisible(animated);
        row.detail->SetSelectedKeyframe(
            (m_selectedKeyframeParam == index) ? m_selectedKeyframeIndex : -1);

        QWidget* detailHost = row.detailHost;
        KeyframeDetail* detail = row.detail;

        connect(row.keyframe, &QToolButton::toggled, this,
                [this, index, detailHost, detail](bool on) {
            ShaderParam* p = Param(index);
            if (!p) return;
            if (!p->timeline.has_value()) p->timeline.emplace();
            p->timeline->enabled = on;
            if (!on && m_selectedKeyframeParam == index) {
                m_selectedKeyframeParam = -1;
                m_selectedKeyframeIndex = -1;
                emit KeyframeSelectionChanged(-1, -1);
            }
            detail->SetSelectedKeyframe(on ? m_selectedKeyframeIndex : -1);
            detailHost->setVisible(on);
            SyncValues();
        });
    }

    // Whatever each widget ended up explaining about itself, remembered so the disabled
    // state can say why instead and then hand the explanation back.
    for (QWidget* widget : { static_cast<QWidget*>(row.label),
                             static_cast<QWidget*>(row.slider),
                             static_cast<QWidget*>(row.spin[0]),
                             static_cast<QWidget*>(row.spin[1]),
                             static_cast<QWidget*>(row.check),
                             static_cast<QWidget*>(row.combo),
                             static_cast<QWidget*>(row.swatch),
                             static_cast<QWidget*>(row.randomise) }) {
        if (widget) row.tips.append(Row::Tip{widget, widget->toolTip()});
    }

    m_rows.append(row);
}

// ---------------------------------------------------------------------------------------
// Value sync
// ---------------------------------------------------------------------------------------

bool ParamsPanel::IsDriven(const ShaderParam& param) const
{
    return param.timeline.has_value() && param.timeline->enabled
        && !param.timeline->keyframes.empty()
        && m_app.GetPlaybackState() == PlaybackState::Playing;
}

void ParamsPanel::SetRowKeyframed(Row& row, bool driven)
{
    if (row.keyframed == driven) return;
    row.keyframed = driven;

    // Reduced contrast is what says inert; the tooltip is what says why. Qt shows a tooltip
    // on a disabled widget, so this needs none of the workaround the immediate-mode panel
    // carried, and every widget in the row explains itself rather than only the first one.
    row.label->setEnabled(!driven);
    row.cell->setEnabled(!driven);
    if (row.randomise) row.randomise->setEnabled(!driven);

    const QString reason = tr("The keyframe timeline is driving this while the video plays. "
                              "Pause playback to set it by hand.");
    for (const Row::Tip& tip : row.tips) {
        tip.widget->setToolTip(driven ? reason : tip.text);
    }
}

void ParamsPanel::UpdateResetStates()
{
    for (Row& row : m_rows) {
        if (!row.reset) continue;
        const ShaderParam* param = Param(row.index);
        if (!param) continue;

        // Bytes, not floats: values round-trip through the same storage the defaults were
        // parsed into, so the comparison is exact, and `==` on four floats would report a
        // difference for a value that is bit-identical in every way that matters.
        const bool atDefault =
            (std::memcmp(param->values, param->defaultValues, 4 * sizeof(float)) == 0);

        // Also inert while the timeline owns the value: the reset would be overwritten on
        // the next frame, and a button flickering enabled sixty times a second is noise.
        row.reset->setEnabled(!atDefault && !IsDriven(*param));
    }
}

void ParamsPanel::PaintSwatch(Row& row, const ShaderParam& param)
{
    const QColor colour = ColourOf(param);
    const qreal dpr = devicePixelRatioF();
    const QSize size(kSwatchWidth, kSwatchHeight);

    QPixmap pixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPainterPath clip;
        clip.addRoundedRect(QRectF(0.5, 0.5, size.width() - 1.0, size.height() - 1.0),
                            Theme::kRadiusControl, Theme::kRadiusControl);
        painter.setClipPath(clip);

        // A checker under the colour, so a partly transparent colour reads as transparent
        // rather than as a darker shade of itself. Opacity is what alpha means here.
        painter.fillRect(QRectF(QPointF(0, 0), QSizeF(size)), Theme::kCanvas);
        constexpr int kCheck = 6;
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
    }

    row.swatch->setIcon(QIcon(pixmap));

    const int opacity = static_cast<int>(Clamp01(param.values[3]) * 100.0f + 0.5f);
    row.swatchText->setText(QStringLiteral("%1 %2%")
                                .arg(colour.name(QColor::HexRgb).toUpper())
                                .arg(opacity));

    // The swatch's own tooltip moves with the value, so the remembered copy moves with it;
    // applying it now would talk over the disabled state's explanation.
    const QString tip = tr("%1\nRGBA %2, %3, %4, %5\nAlpha is opacity: the video shows "
                           "through where the shader writes less than 1.")
                            .arg(QString::fromStdString(param.name))
                            .arg(param.values[0], 0, 'f', 3)
                            .arg(param.values[1], 0, 'f', 3)
                            .arg(param.values[2], 0, 'f', 3)
                            .arg(param.values[3], 0, 'f', 3);
    for (Row::Tip& entry : row.tips) {
        if (entry.widget == row.swatch) entry.text = tip;
    }
    if (!row.keyframed) row.swatch->setToolTip(tip);
}

void ParamsPanel::SyncValues()
{
    // Every write below goes through a QSignalBlocker: this runs every frame under keyframe
    // playback, and an unblocked write would come straight back as a user edit and feed
    // OnParamChanged in a loop.
    for (Row& row : m_rows) {
        ShaderParam* param = Param(row.index);
        if (!param) continue;

        switch (row.type) {

        case ShaderParamType::Float: {
            const QSignalBlocker blockSlider(row.slider);
            const QSignalBlocker blockSpin(row.spin[0]);
            row.slider->setValue(ValueToTick(param->values[0], row.ticks, *param));
            row.spin[0]->setValue(param->values[0]);
            break;
        }

        case ShaderParamType::Bool: {
            const QSignalBlocker block(row.check);
            row.check->setChecked(param->values[0] > 0.5f);
            break;
        }

        case ShaderParamType::Long: {
            const QSignalBlocker block(row.combo);
            const int current = static_cast<int>(param->values[0]);
            int index = 0;
            for (int i = 0; i < static_cast<int>(param->longValues.size()); ++i) {
                if (param->longValues[static_cast<size_t>(i)] == current) { index = i; break; }
            }
            if (param->longValues.empty()) index = current;
            if (index >= 0 && index < row.combo->count()) row.combo->setCurrentIndex(index);
            break;
        }

        case ShaderParamType::Color:
            PaintSwatch(row, *param);
            break;

        case ShaderParamType::Point2D:
            for (int axis = 0; axis < 2; ++axis) {
                const QSignalBlocker block(row.spin[axis]);
                row.spin[axis]->setValue(param->values[axis]);
            }
            break;

        case ShaderParamType::Event:
        case ShaderParamType::AudioBand:
            break;   // a button holds nothing, and a meter is written by Tick

        }

        if (row.keyframe) {
            const bool animated = param->timeline.has_value() && param->timeline->enabled;
            { const QSignalBlocker block(row.keyframe);
              row.keyframe->setChecked(animated); }
            // The toggle is also set from here (a preset arriving with a saved timeline
            // never passes through the button's own handler), so the track's visibility
            // follows the data rather than only the click.
            row.detailHost->setVisible(animated);
        }
        SetRowKeyframed(row, IsDriven(*param));
    }

    UpdateResetStates();
}

void ParamsPanel::UpdateMeters()
{
    const AudioData& audio = m_app.GetAudioData();

    for (Row& row : m_rows) {
        if (row.type != ShaderParamType::AudioBand) continue;
        const ShaderParam* param = Param(row.index);
        if (!param) continue;

        const float level = Clamp01(LiveBand(audio, param->audioBand));
        row.meter->setValue(static_cast<int>(level * kMeterSteps));
        row.meterValue->setText(QString::number(level, 'f', 2));

        // Only the beat band changes colour, only while it is firing, and only when the
        // state flips: restyling a widget every frame recomputes the stylesheet every frame.
        const bool hot = (param->audioBand == "beat") && (level > kBeatVisible);
        if (hot != row.beatHot) {
            row.beatHot = hot;
            row.meter->setStyleSheet(hot
                ? QStringLiteral("QProgressBar::chunk { background: %1; border-radius: 5px;"
                                 " margin: 1px; }").arg(Theme::kStateWarning.name())
                : QString());
        }
    }
}

void ParamsPanel::Tick()
{
    // A video can open or close without any preset change, so the blend block cannot be
    // settled by SetPreset alone. Two pointer reads and a bool compare, ahead of the early
    // return, because a shader with no parameter rows can still blend against footage.
    UpdateBlendVisibility();

    if (m_rows.isEmpty()) return;

    UpdateMeters();

    // Keyframe playback rewrites values in Application::EvaluateKeyframes every frame, so
    // the widgets have to follow without a rebuild. The trailing sync is what re-enables
    // the row on the frame playback stops.
    bool driven = false;
    for (const Row& row : m_rows) {
        const ShaderParam* param = Param(row.index);
        if (param && IsDriven(*param)) { driven = true; break; }
    }
    if (driven || m_wasDriven) SyncValues();
    m_wasDriven = driven;
}

// ---------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------

bool ParamsPanel::RandomiseParam(ShaderParam& p)
{
    // Every branch draws from exactly the range the corresponding widget exposes, so a
    // randomised value is always one the user could have dialled in by hand.
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    auto randomInRange = [&](float lo, float hi, float step) {
        if (hi < lo) std::swap(lo, hi);
        float v = lo + unit(m_rng) * (hi - lo);
        if (step > 0.0f) v = std::round(v / step) * step;
        return std::clamp(v, lo, hi);
    };

    switch (p.type) {
    case ShaderParamType::Float:
        p.values[0] = randomInRange(p.min, p.max, p.step);
        return true;

    case ShaderParamType::Bool:
        p.values[0] = (unit(m_rng) < 0.5f) ? 0.0f : 1.0f;
        return true;

    case ShaderParamType::Long: {
        if (p.longValues.empty()) return false;   // combo has no selectable entries
        std::uniform_int_distribution<size_t> pick(0, p.longValues.size() - 1);
        p.values[0] = static_cast<float>(p.longValues[pick(m_rng)]);
        return true;
    }

    case ShaderParamType::Color:
        // RGB only. Alpha is an opacity in every shader that reads it, and rolling it
        // at random would silently make the effect invisible.
        for (int i = 0; i < 3; ++i) p.values[i] = unit(m_rng);
        return true;

    case ShaderParamType::Point2D:
        // Both axes map onto [min, max], so both components use that range.
        p.values[0] = randomInRange(p.min, p.max, 0.0f);
        p.values[1] = randomInRange(p.min, p.max, 0.0f);
        return true;

    case ShaderParamType::Event:
    case ShaderParamType::AudioBand:
        return false;   // a trigger and a live meter — neither holds a value to roll
    }
    return false;
}

void ParamsPanel::RandomiseAll()
{
    if (!m_preset) return;

    bool any = false;
    for (ShaderParam& param : m_preset->params) {
        // Skip parameters under keyframe control — the timeline owns their value and would
        // overwrite the roll on the next frame.
        if (param.timeline && param.timeline->enabled && !param.timeline->keyframes.empty()) {
            continue;
        }
        if (RandomiseParam(param)) any = true;
    }
    if (!any) return;

    SyncValues();
    Changed();
}

void ParamsPanel::ResetAll()
{
    if (!m_preset) return;

    for (ShaderParam& param : m_preset->params) {
        std::copy(param.defaultValues, param.defaultValues + 4, param.values);
    }
    SyncValues();
    Changed();
}

void ParamsPanel::Changed()
{
    // Every mutation passes here, which makes it the one place the reset arrows can learn
    // that a value has left (or returned to) its default without a rebuild.
    UpdateResetStates();
    m_app.OnParamChanged();
}

// ---------------------------------------------------------------------------------------
// A9 seam
// ---------------------------------------------------------------------------------------

QWidget* ParamsPanel::KeyframeDetailHost(int paramIndex) const
{
    for (const Row& row : m_rows) {
        if (row.index == paramIndex) return row.detailHost;
    }
    return nullptr;
}

void ParamsPanel::SetKeyframeSelection(int paramIndex, int keyframeIndex)
{
    if (m_selectedKeyframeParam == paramIndex && m_selectedKeyframeIndex == keyframeIndex) {
        return;
    }
    m_selectedKeyframeParam = paramIndex;
    m_selectedKeyframeIndex = keyframeIndex;

    // Exactly one track can hold the selection, so the others are told they have lost it.
    // The track that asked for it has already moved, and telling it again costs a rebuild
    // of its own chips and nothing else.
    for (Row& row : m_rows) {
        if (!row.detail) continue;
        row.detail->SetSelectedKeyframe((row.index == paramIndex) ? keyframeIndex : -1);
    }

    emit KeyframeSelectionChanged(paramIndex, keyframeIndex);
}

} // namespace SP
