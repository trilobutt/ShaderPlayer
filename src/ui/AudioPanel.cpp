#include "ui/AudioPanel.h"

#include "Application.h"
#include "VideoDecoder.h"
#include "ui/Theme.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>
#include <QProgressBar>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace SP {

namespace {

// The bands are [0,1] and QProgressBar is integral.
constexpr int kMeterSteps = 1000;

// A beat is an event rather than a level, so it takes a colour the steady bands do not.
// Same threshold and same colour as the AudioBand meters in the parameters panel.
constexpr float kBeatVisible = 0.1f;

// Below this the field is silence rather than signal, and is said in words instead.
constexpr float kSilenceFloor = 0.005f;

// Peak trace decay per repaint. At the frame rate this panel ticks at, a transient's peak
// takes roughly half a second to fall back to the live profile, which is long enough to be
// read and short enough not to smear one hit into the next.
constexpr float kPeakDecay = 0.94f;

constexpr int kSpectrumHeight = 104;
constexpr int kLabelColumn = 46;
constexpr int kValueColumn = 38;
constexpr int kSettingLabelColumn = 118;

// The DSP ranges the outgoing panel offered, to two decimals.
constexpr int kSettingTicks = 100;

constexpr float kBeatSensitivityMin = 1.0f;
constexpr float kBeatSensitivityMax = 4.0f;
constexpr float kBeatDecayMin       = 0.5f;
constexpr float kBeatDecayMax       = 0.99f;
constexpr float kSmoothingMin       = 0.0f;
constexpr float kSmoothingMax       = 0.95f;

float Clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

int TicksFor(float value, float min, float max)
{
    if (!(max > min)) return 0;
    const float t = (value - min) / (max - min);
    return std::clamp(static_cast<int>(std::lround(t * kSettingTicks)), 0, kSettingTicks);
}

float ValueFor(int ticks, float min, float max)
{
    return min + (static_cast<float>(ticks) / kSettingTicks) * (max - min);
}

}  // namespace

// =========================================================================================
// SpectrumView
// =========================================================================================

SpectrumView::SpectrumView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SpectrumView"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(tr("The live magnitude of each of the 256 analysis bins, low frequencies at "
                  "the left. The faint line above is a decaying peak trace."));
}

QSize SpectrumView::minimumSizeHint() const
{
    return QSize(kLabelColumn * 3, kSpectrumHeight);
}

void SpectrumView::SetSpectrum(const float* bins, int count, bool silent)
{
    m_silent = silent;

    if (!bins || count <= 0) {
        m_bins.clear();
        m_peaks.clear();
        update();
        return;
    }

    if (m_bins.size() != count) {
        m_bins.fill(0.0f, count);
        m_peaks.fill(0.0f, count);
    }

    for (int i = 0; i < count; ++i) {
        const float level = Clamp01(bins[i]);
        m_bins[i] = level;
        m_peaks[i] = std::max(level, m_peaks[i] * kPeakDecay);
    }

    update();
}

void SpectrumView::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // Recessed: the field is cut into the panel rather than floating on it.
    QPainterPath frame;
    frame.addRoundedRect(area, Theme::kRadiusControl, Theme::kRadiusControl);
    painter.fillPath(frame, Theme::kInputFill);

    const int count = static_cast<int>(m_bins.size());
    if (count > 1 && !m_silent) {
        painter.save();
        painter.setClipPath(frame);

        const qreal left = area.left();
        const qreal span = area.width();
        const qreal base = area.bottom();
        const qreal height = area.height();

        // One filled path for the whole field: 256 sub-pixel rectangles would be 256 fills
        // per frame for a shape a single polygon describes exactly.
        QPolygonF profile;
        profile.reserve(count + 2);
        profile << QPointF(left, base);
        for (int i = 0; i < count; ++i) {
            const qreal x = left + span * i / (count - 1);
            profile << QPointF(x, base - height * m_bins[i]);
        }
        profile << QPointF(left + span, base);

        QLinearGradient fill(0.0, area.top(), 0.0, base);
        QColor top = Theme::kRegionAudio;
        top.setAlpha(235);
        QColor bottom = Theme::kRegionAudio;
        bottom.setAlpha(45);
        fill.setColorAt(0.0, top);
        fill.setColorAt(1.0, bottom);

        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPolygon(profile);

        // The peak trace, stroked once. Brighter than the fill so a transient reads against
        // the body of the signal rather than disappearing into it.
        QPolygonF peaks;
        peaks.reserve(count);
        for (int i = 0; i < count; ++i) {
            const qreal x = left + span * i / (count - 1);
            peaks << QPointF(x, base - height * m_peaks[i]);
        }
        QColor trace = Theme::kRegionAudio.lighter(150);
        trace.setAlpha(190);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(trace, 1.0));
        painter.drawPolyline(peaks);

        painter.restore();
    } else {
        // A flat line is indistinguishable from a dead widget, so silence says so.
        painter.setPen(Theme::kTextSecondary);
        painter.drawText(rect(), Qt::AlignCenter,
                         m_bins.isEmpty() ? tr("no signal") : tr("silent (nothing playing)"));
    }

    painter.setPen(QPen(Theme::kPanelBorder, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(area, Theme::kRadiusControl, Theme::kRadiusControl);
}

// =========================================================================================
// AudioPanel
// =========================================================================================

AudioPanel::AudioPanel(Application& app, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
{
    setObjectName(QStringLiteral("AudioPanel"));

    // The application stylesheet paints every QWidget with the canvas, which would lay an
    // opaque black slab over this dock's glass. Named containers opt out; the controls keep
    // their own fills. The readouts are monospace because they are a column of numbers that
    // changes every frame and must not reflow as the digits move.
    setStyleSheet(QStringLiteral(
        "QWidget#AudioPanel, QWidget#AudioRow, QWidget#AudioPage, QWidget#AudioStack,"
        "QWidget#SpectrumView { background: transparent; }"
        "QLabel#AudioValue { font-family: %1; font-size: %2pt; color: %3; }")
        .arg(QString::fromLatin1(Theme::kFontMono))
        .arg(Theme::kFontSizeCaption)
        .arg(Theme::kTextSecondary.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kSpaceUnit);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("AudioStack"));

    auto* live = new QWidget(m_stack);
    live->setObjectName(QStringLiteral("AudioPage"));
    auto* liveLayout = new QVBoxLayout(live);
    liveLayout->setContentsMargins(0, 0, 0, 0);
    liveLayout->setSpacing(Theme::kSpaceUnit);
    BuildLive(liveLayout);
    m_stack->addWidget(live);                       // page 0

    m_stack->addWidget(BuildEmptyPage(m_stack));    // page 1

    layout->addWidget(m_stack, 1);

    m_hadAudio = m_app.GetDecoder().HasAudio();
    m_stack->setCurrentIndex(m_hadAudio ? 0 : 1);
}

void AudioPanel::BuildLive(QVBoxLayout* layout)
{
    BuildLevels(layout);
    BuildSpectrum(layout);
    layout->addStretch(1);
    BuildSettings(layout);
}

namespace {

// One meter row: name, bar, value. Kept together so the five of them line up on one edge.
struct MeterRow {
    QProgressBar* bar = nullptr;
    QLabel* value = nullptr;
};

MeterRow AddMeter(QWidget* host, QVBoxLayout* layout, const QString& name, const QString& tip)
{
    auto* row = new QWidget(host);
    row->setObjectName(QStringLiteral("AudioRow"));
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(Theme::kSpaceUnit);

    auto* label = new QLabel(name, row);
    label->setMinimumWidth(kLabelColumn);
    line->addWidget(label);

    MeterRow meter;
    meter.bar = new QProgressBar(row);
    meter.bar->setRange(0, kMeterSteps);
    meter.bar->setTextVisible(false);
    meter.bar->setToolTip(tip);
    line->addWidget(meter.bar, 1);

    meter.value = new QLabel(row);
    meter.value->setObjectName(QStringLiteral("AudioValue"));
    meter.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    meter.value->setFixedWidth(kValueColumn);
    meter.value->setText(QStringLiteral("0.00"));
    line->addWidget(meter.value);

    layout->addWidget(row);
    return meter;
}

}  // namespace

void AudioPanel::BuildLevels(QVBoxLayout* layout)
{
    auto* heading = new QLabel(tr("Levels"), this);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(heading);

    const MeterRow rms = AddMeter(this, layout, tr("RMS"),
        tr("Overall loudness of the window, as the shader reads audioRms."));
    const MeterRow bass = AddMeter(this, layout, tr("Bass"),
        tr("20-250 Hz, as the shader reads audioBass."));
    const MeterRow mid = AddMeter(this, layout, tr("Mid"),
        tr("250-4000 Hz, as the shader reads audioMid."));
    const MeterRow high = AddMeter(this, layout, tr("High"),
        tr("4-20 kHz, as the shader reads audioHigh."));
    const MeterRow beat = AddMeter(this, layout, tr("Beat"),
        tr("A pulse fired on onset and decayed since, as the shader reads audioBeat.\n"
           "Tune its threshold and fall below."));

    m_rms.bar   = rms.bar;   m_rms.value   = rms.value;
    m_bass.bar  = bass.bar;  m_bass.value  = bass.value;
    m_mid.bar   = mid.bar;   m_mid.value   = mid.value;
    m_high.bar  = high.bar;  m_high.value  = high.value;
    m_beat.bar  = beat.bar;  m_beat.value  = beat.value;
}

void AudioPanel::BuildSpectrum(QVBoxLayout* layout)
{
    auto* heading = new QLabel(tr("Spectrum"), this);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(heading);

    m_spectrum = new SpectrumView(this);
    m_spectrum->setMinimumHeight(kSpectrumHeight);
    layout->addWidget(m_spectrum, 1);

    auto* caption = new QLabel(
        tr("256 bins, low frequencies at the left. Shaders sample the same data from "
           "spectrumTexture at t3."), this);
    caption->setObjectName(QStringLiteral("Caption"));
    caption->setWordWrap(true);
    layout->addWidget(caption);
}

void AudioPanel::AddSetting(QVBoxLayout* layout, const QString& label, const QString& tip,
                            float& store, float min, float max)
{
    auto* row = new QWidget(this);
    row->setObjectName(QStringLiteral("AudioRow"));
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(Theme::kSpaceUnit);

    auto* name = new QLabel(label, row);
    name->setMinimumWidth(kSettingLabelColumn);
    line->addWidget(name);

    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(0, kSettingTicks);
    slider->setValue(TicksFor(store, min, max));
    slider->setToolTip(tip);
    line->addWidget(slider, 1);

    auto* value = new QLabel(row);
    value->setObjectName(QStringLiteral("AudioValue"));
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setFixedWidth(kValueColumn);
    value->setText(QString::number(store, 'f', 2));
    line->addWidget(value);

    // The readout follows the handle, so the drag is legible. The analyser and config.json
    // follow the release: UpdateAudioSettings writes the file, and a disk write per drag
    // tick is a cost the user never asked for. A keyboard or wheel change is not a drag and
    // commits at once.
    connect(slider, &QSlider::valueChanged, this,
            [this, slider, value, &store, min, max](int ticks) {
                store = ValueFor(ticks, min, max);
                value->setText(QString::number(store, 'f', 2));
                if (!slider->isSliderDown()) m_app.UpdateAudioSettings();
            });
    connect(slider, &QSlider::sliderReleased, this, [this] { m_app.UpdateAudioSettings(); });

    layout->addWidget(row);
}

void AudioPanel::BuildSettings(QVBoxLayout* layout)
{
    auto* heading = new QLabel(tr("Analysis"), this);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(heading);

    AudioSettings& settings = m_app.GetConfig().audio;

    AddSetting(layout, tr("Beat Sensitivity"),
               tr("Threshold = sensitivity x rolling-average bass energy.\n"
                  "Lower fires more often; higher only on the strongest hits."),
               settings.beatSensitivity, kBeatSensitivityMin, kBeatSensitivityMax);

    AddSetting(layout, tr("Beat Decay"),
               tr("How slowly a fired beat falls back to zero. Higher holds the pulse "
                  "longer."),
               settings.beatDecay, kBeatDecayMin, kBeatDecayMax);

    AddSetting(layout, tr("Smoothing"),
               tr("0 = no smoothing (raw), 0.95 = very slow response."),
               settings.smoothing, kSmoothingMin, kSmoothingMax);
}

QWidget* AudioPanel::BuildEmptyPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("AudioPage"));

    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(Theme::kSpaceUnit, Theme::kSpaceUnit,
                               Theme::kSpaceUnit, Theme::kSpaceUnit);
    column->setSpacing(0);
    column->addStretch(2);

    auto* heading = new QLabel(tr("No audio source"), page);
    QFont headingFont(QString::fromLatin1(Theme::kFontUi));
    headingFont.setPointSize(Theme::kFontSizePanelTitle + 3);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    heading->setAlignment(Qt::AlignCenter);
    heading->setWordWrap(true);
    column->addWidget(heading);
    column->addSpacing(Theme::kSpaceUnit);

    auto* blurb = new QLabel(
        tr("Open a video or a live capture carrying an audio track and the meters, the "
           "spectrum and the analysis controls arrive here. Until then every audio band a "
           "shader reads (audioBass, audioMid, audioBeat) is zero."), page);
    blurb->setObjectName(QStringLiteral("Caption"));
    blurb->setAlignment(Qt::AlignCenter);
    blurb->setWordWrap(true);
    column->addWidget(blurb);
    column->addStretch(3);

    return page;
}

// ---- per-frame ---------------------------------------------------------------------------

void AudioPanel::Tick()
{
    // A hidden dock draws nothing, so it measures nothing. This is what keeps a 256-bin
    // repaint from costing anything while the monitor is tabbed behind another panel.
    if (!isVisible()) return;

    const bool hasAudio = m_app.GetDecoder().HasAudio();
    if (hasAudio != m_hadAudio) {
        m_hadAudio = hasAudio;
        m_stack->setCurrentIndex(hasAudio ? 0 : 1);
    }
    if (!hasAudio) return;

    UpdateMeters();
}

void AudioPanel::UpdateMeters()
{
    const AudioData& audio = m_app.GetAudioData();

    const auto set = [](Meter& meter, float level) {
        const float value = Clamp01(level);
        meter.bar->setValue(static_cast<int>(value * kMeterSteps));
        meter.value->setText(QString::number(value, 'f', 2));
    };

    set(m_rms,  audio.rms);
    set(m_bass, audio.bass);
    set(m_mid,  audio.mid);
    set(m_high, audio.high);
    set(m_beat, audio.beat);

    // Only the beat bar changes colour, only while it is firing, and only when the state
    // flips: restyling a widget every frame recomputes its stylesheet every frame.
    const bool hot = (audio.beat > kBeatVisible);
    if (hot != m_beat.hot) {
        m_beat.hot = hot;
        m_beat.bar->setStyleSheet(hot
            ? QStringLiteral("QProgressBar::chunk { background: %1; border-radius: 5px;"
                             " margin: 1px; }").arg(Theme::kStateWarning.name())
            : QString());
    }

    // A source that is open but paused, muted or between tracks has a real spectrum of
    // zeroes, which is not the same thing as having no source at all.
    const bool silent = (audio.rms < kSilenceFloor);
    m_spectrum->SetSpectrum(audio.spectrum, AudioData::kSpectrumBins, silent);
}

}  // namespace SP
