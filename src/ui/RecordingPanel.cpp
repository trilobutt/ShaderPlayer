#include "ui/RecordingPanel.h"

#include "Application.h"
#include "VideoEncoder.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>

namespace SP {

namespace {

// The toggle is the largest target on the panel while armed: stopping a recording that was
// left running by accident is the one action this surface must never make anyone hunt for.
constexpr int kToggleIdleHeight  = 44;
constexpr int kToggleArmedHeight = 58;

// The elapsed clock is the number the eye goes to while recording, so it is the largest
// type in the dock.
constexpr int kElapsedPoints = Theme::kFontSizePanelTitle + 6;

constexpr int kBrowseWidth = 92;
constexpr int kFieldLabelWidth = 74;

// Bitrate, in Mbps. The range the outgoing panel offered.
constexpr int kBitrateMin  = 5;
constexpr int kBitrateMax  = 100;
constexpr int kBitrateStep = 5;

// The pulsing border. Never falls to nothing at the bottom of the cycle: an indicator that
// blinks fully off is one a glance can arrive in the dark half of.
constexpr qreal kPulseFloor  = 0.45;
constexpr int   kBorderAlpha = 235;   // at full pulse
constexpr int   kBorderWidth = 2;

const char* CodecId(int index)
{
    return (index == 0) ? "libx264" : "prores_ks";
}

// h:mm:ss, dropping the hours field until there is one. A recording clock that stops making
// sense after an hour would fail at exactly the length that matters.
QString ElapsedText(qint64 ms)
{
    const qint64 total = (ms > 0) ? ms / 1000 : 0;
    const int hours = static_cast<int>(total / 3600);
    const int mins  = static_cast<int>((total / 60) % 60);
    const int secs  = static_cast<int>(total % 60);

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(mins, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(mins, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

QString ElideMiddle(const QWidget* widget, const QString& text, int inset)
{
    const QFontMetrics metrics(widget->font());
    return metrics.elidedText(text, Qt::ElideMiddle, std::max(widget->width() - inset, 24));
}

}  // namespace

// =========================================================================================
// PathEdit
// =========================================================================================

PathEdit::PathEdit(QWidget* parent)
    : QLineEdit(parent)
{
    setObjectName(QStringLiteral("RecordingPath"));

    // textEdited, not textChanged: the elided display is pushed in with setText, and only
    // typing is allowed to move the truth.
    connect(this, &QLineEdit::textEdited, this, [this](const QString& text) {
        m_path = text;
        setToolTip(m_path);
        emit PathChanged(m_path);
    });
}

void PathEdit::SetPath(const QString& path)
{
    m_path = path;
    setToolTip(m_path);
    if (hasFocus()) {
        const QSignalBlocker block(this);
        setText(m_path);
    } else {
        ShowElided();
    }
}

void PathEdit::focusInEvent(QFocusEvent* event)
{
    QLineEdit::focusInEvent(event);
    const QSignalBlocker block(this);
    setText(m_path);
}

void PathEdit::focusOutEvent(QFocusEvent* event)
{
    QLineEdit::focusOutEvent(event);
    ShowElided();
}

void PathEdit::resizeEvent(QResizeEvent* event)
{
    QLineEdit::resizeEvent(event);
    if (!hasFocus()) ShowElided();
}

void PathEdit::ShowElided()
{
    // 22px covers the field's horizontal padding and both borders.
    const QSignalBlocker block(this);
    setText(ElideMiddle(this, m_path, 22));
}

// =========================================================================================
// RecordingPanel
// =========================================================================================

RecordingPanel::RecordingPanel(Application& app, RecordingSettings& settings, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
    , m_settings(settings)
{
    setObjectName(QStringLiteral("RecordingPanel"));

    // The application stylesheet paints every QWidget with the canvas, which would lay an
    // opaque black slab over this dock's glass. Named containers opt out; the controls keep
    // their own fills. The two monospace faces are the fixed-width data this panel carries:
    // a clock whose digits tick in place, and counters that must not reflow as they climb.
    setStyleSheet(QStringLiteral(
        "QWidget#RecordingPanel, QWidget#RecordingRow, QWidget#RecordingStatus,"
        "QWidget#RecordingStatusHead { background: transparent; }"
        "QLabel#RecordingElapsed { font-family: %1; font-size: %2pt; font-weight: bold;"
        " color: %3; }"
        "QLabel#RecordingCounters { font-family: %1; font-size: %4pt; color: %5; }"
        "QLabel#RecordingBadge { background: %6; color: %7; font-weight: bold;"
        " border-radius: %8px; padding: 3px 10px; }")
        .arg(QString::fromLatin1(Theme::kFontMono))
        .arg(kElapsedPoints)
        .arg(Theme::kTextPrimary.name())
        .arg(Theme::kFontSizeCaption)
        .arg(Theme::kTextSecondary.name())
        .arg(Theme::kStateError.name())
        .arg(Theme::kTextOnAccent.name())
        .arg(Theme::kRadiusControl));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kSpaceUnit);

    BuildOutput(layout);
    BuildEncoding(layout);
    layout->addStretch(1);
    BuildStatus(layout);

    m_pulse = new QVariantAnimation(this);
    m_pulse->setKeyValueAt(0.0, kPulseFloor);
    m_pulse->setKeyValueAt(0.5, 1.0);
    m_pulse->setKeyValueAt(1.0, kPulseFloor);
    m_pulse->setEasingCurve(QEasingCurve::InOutSine);
    m_pulse->setLoopCount(-1);
    connect(m_pulse, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_pulseT = value.toReal();
        update();
    });

    SyncCodec();
    SyncArmed(m_app.GetEncoder().IsRecording());
}

void RecordingPanel::BuildOutput(QVBoxLayout* layout)
{
    auto* heading = new QLabel(tr("Output"), this);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(heading);

    auto* row = new QWidget(this);
    row->setObjectName(QStringLiteral("RecordingRow"));
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(Theme::kSpaceUnit);

    m_path = new PathEdit(row);
    m_path->setPlaceholderText(tr("output.mp4"));
    m_path->SetPath(QString::fromStdString(m_settings.outputPath));
    connect(m_path, &PathEdit::PathChanged, this, [this](const QString& path) {
        m_settings.outputPath = path.toStdString();
        SyncStatus();
        Persist();
    });
    line->addWidget(m_path, 1);

    m_browse = new QPushButton(tr("Browse..."), row);
    m_browse->setFixedWidth(kBrowseWidth);
    m_browse->setCursor(Qt::PointingHandCursor);
    m_browse->setToolTip(tr("Choose where the recording is written."));
    connect(m_browse, &QPushButton::clicked, this, &RecordingPanel::BrowseForOutput);
    line->addWidget(m_browse);

    layout->addWidget(row);
}

void RecordingPanel::BuildEncoding(QVBoxLayout* layout)
{
    auto* heading = new QLabel(tr("Encoding"), this);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(heading);

    // ---- codec ---------------------------------------------------------------------------
    auto* codecRow = new QWidget(this);
    codecRow->setObjectName(QStringLiteral("RecordingRow"));
    auto* codecLine = new QHBoxLayout(codecRow);
    codecLine->setContentsMargins(0, 0, 0, 0);
    codecLine->setSpacing(Theme::kSpaceUnit);

    auto* codecLabel = new QLabel(tr("Codec"), codecRow);
    codecLabel->setMinimumWidth(kFieldLabelWidth);
    codecLine->addWidget(codecLabel);

    m_codec = new QComboBox(codecRow);
    m_codec->addItem(tr("H.264 (MP4)"));
    m_codec->addItem(tr("ProRes (MOV)"));
    m_codec->setCurrentIndex(m_settings.codec == "prores_ks" ? 1 : 0);
    // Each of these three seeds a widget whose range is narrower than the field it comes
    // from, and then takes the widget's answer back. A config holding 0.5 Mbps otherwise
    // shows the user 5 Mbps and hands the encoder 0.5, and neither number is wrong enough
    // to look like a bug.
    m_settings.codec = CodecId(m_codec->currentIndex());
    m_codec->setToolTip(tr("H.264 is small and plays anywhere. ProRes is far larger and "
                           "survives further editing."));
    connect(m_codec, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_settings.codec = CodecId(index);
        SyncCodec();
        Persist();
    });
    codecLine->addWidget(m_codec, 1);
    layout->addWidget(codecRow);

    // ---- frame rate ------------------------------------------------------------------
    m_fpsRow = new QWidget(this);
    m_fpsRow->setObjectName(QStringLiteral("RecordingRow"));
    auto* fpsLine = new QHBoxLayout(m_fpsRow);
    fpsLine->setContentsMargins(0, 0, 0, 0);
    fpsLine->setSpacing(Theme::kSpaceUnit);

    auto* fpsLabel = new QLabel(tr("Frame rate"), m_fpsRow);
    fpsLabel->setMinimumWidth(kFieldLabelWidth);
    fpsLine->addWidget(fpsLabel);

    m_fps = new QSpinBox(m_fpsRow);
    m_fps->setRange(kMinRenderFps, kMaxRenderFps);
    m_fps->setSuffix(tr(" fps"));
    m_fps->setKeyboardTracking(false);
    // 0 is what a config written before this control existed holds, and it means the
    // default rather than a rate. Seeding the widget with the resolved value and writing
    // it straight back is the same contract the codec and bitrate rows keep: the store
    // must never hold a number the panel is not showing.
    m_fps->setValue(m_settings.fps > 0
                        ? std::clamp(m_settings.fps, kMinRenderFps, kMaxRenderFps)
                        : kDefaultRenderFps);
    m_settings.fps = m_fps->value();
    connect(m_fps, &QSpinBox::valueChanged, this, [this](int fps) {
        m_settings.fps = fps;
        Persist();
    });
    fpsLine->addWidget(m_fps, 1);
    layout->addWidget(m_fpsRow);

    // ---- bitrate (H.264 only) --------------------------------------------------------------
    m_bitrateRow = new QWidget(this);
    m_bitrateRow->setObjectName(QStringLiteral("RecordingRow"));
    auto* bitrateLine = new QHBoxLayout(m_bitrateRow);
    bitrateLine->setContentsMargins(0, 0, 0, 0);
    bitrateLine->setSpacing(Theme::kSpaceUnit);

    auto* bitrateLabel = new QLabel(tr("Bitrate"), m_bitrateRow);
    bitrateLabel->setMinimumWidth(kFieldLabelWidth);
    bitrateLine->addWidget(bitrateLabel);

    m_bitrate = new QSpinBox(m_bitrateRow);
    m_bitrate->setRange(kBitrateMin, kBitrateMax);
    m_bitrate->setSingleStep(kBitrateStep);
    m_bitrate->setSuffix(tr(" Mbps"));
    m_bitrate->setKeyboardTracking(false);
    m_bitrate->setValue(std::clamp(m_settings.bitrate / 1000000, kBitrateMin, kBitrateMax));
    m_settings.bitrate = m_bitrate->value() * 1000000;   // see the codec combo
    m_bitrate->setToolTip(tr("Higher keeps more detail and costs proportionally more disk."));
    connect(m_bitrate, &QSpinBox::valueChanged, this, [this](int mbps) {
        m_settings.bitrate = mbps * 1000000;
        Persist();
    });
    bitrateLine->addWidget(m_bitrate, 1);
    layout->addWidget(m_bitrateRow);

    // ---- ProRes profile (ProRes only) ------------------------------------------------------
    m_profileRow = new QWidget(this);
    m_profileRow->setObjectName(QStringLiteral("RecordingRow"));
    auto* profileLine = new QHBoxLayout(m_profileRow);
    profileLine->setContentsMargins(0, 0, 0, 0);
    profileLine->setSpacing(Theme::kSpaceUnit);

    auto* profileLabel = new QLabel(tr("Profile"), m_profileRow);
    profileLabel->setMinimumWidth(kFieldLabelWidth);
    profileLine->addWidget(profileLabel);

    m_profile = new QComboBox(m_profileRow);
    m_profile->addItem(tr("Proxy"));
    m_profile->addItem(tr("LT"));
    m_profile->addItem(tr("422"));
    m_profile->addItem(tr("HQ"));
    m_profile->setCurrentIndex(std::clamp(m_settings.proresProfile, 0, 3));
    m_settings.proresProfile = m_profile->currentIndex();   // see the codec combo
    m_profile->setToolTip(tr("Proxy is the smallest, HQ the largest. The profile sets the "
                             "rate, so ProRes ignores the bitrate above."));
    connect(m_profile, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_settings.proresProfile = index;
        Persist();
    });
    profileLine->addWidget(m_profile, 1);
    layout->addWidget(m_profileRow);
}

void RecordingPanel::BuildStatus(QVBoxLayout* layout)
{
    // Idle: one line saying what the button does and where else it lives.
    m_idleHint = new QLabel(tr("Not recording. F9 starts and stops from anywhere."), this);
    m_idleHint->setObjectName(QStringLiteral("Caption"));
    m_idleHint->setWordWrap(true);
    layout->addWidget(m_idleHint);

    // Armed: the two things a glance has to answer, then the counters.
    m_statusBlock = new QWidget(this);
    m_statusBlock->setObjectName(QStringLiteral("RecordingStatus"));
    auto* column = new QVBoxLayout(m_statusBlock);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);

    auto* head = new QWidget(m_statusBlock);
    head->setObjectName(QStringLiteral("RecordingStatusHead"));
    auto* headLine = new QHBoxLayout(head);
    headLine->setContentsMargins(0, 0, 0, 0);
    headLine->setSpacing(Theme::kSpaceUnit);

    m_badge = new QLabel(tr("REC"), head);
    m_badge->setObjectName(QStringLiteral("RecordingBadge"));
    headLine->addWidget(m_badge, 0, Qt::AlignVCenter);

    m_elapsed = new QLabel(head);
    m_elapsed->setObjectName(QStringLiteral("RecordingElapsed"));
    m_elapsed->setToolTip(tr("How long this recording has been running."));
    headLine->addWidget(m_elapsed, 0, Qt::AlignVCenter);
    headLine->addStretch(1);
    column->addWidget(head);

    m_destination = new QLabel(m_statusBlock);
    m_destination->setObjectName(QStringLiteral("Caption"));
    column->addWidget(m_destination);

    m_counters = new QLabel(m_statusBlock);
    m_counters->setObjectName(QStringLiteral("RecordingCounters"));
    m_counters->setToolTip(tr("Frames written, frames the encoder could not keep up with, "
                              "and the rate it is managing."));
    column->addWidget(m_counters);

    m_statusBlock->setVisible(false);
    layout->addWidget(m_statusBlock);

    m_toggle = new QPushButton(this);
    m_toggle->setCursor(Qt::PointingHandCursor);
    connect(m_toggle, &QPushButton::clicked, this, &RecordingPanel::ToggleRecording);
    layout->addWidget(m_toggle);
}

// ---- actions ----------------------------------------------------------------------------

void RecordingPanel::BrowseForOutput()
{
    const std::string chosen = m_app.OpenRecordingOutputDialog(m_settings.outputPath);
    if (chosen.empty()) return;   // cancelled

    m_settings.outputPath = chosen;
    m_path->SetPath(QString::fromStdString(chosen));
    SyncStatus();
    Persist();
}

void RecordingPanel::Persist()
{
    m_app.GetConfig().recordingDefaults = m_settings;
    m_app.SaveConfig();
}

void RecordingPanel::ToggleRecording()
{
    VideoEncoder& encoder = m_app.GetEncoder();

    if (encoder.IsRecording()) {
        m_app.StopRecording();
        SyncArmed(encoder.IsRecording());
        return;
    }

    if (!m_app.StartRecording(m_settings)) {
        SyncArmed(encoder.IsRecording());
        return;
    }

    SyncArmed(encoder.IsRecording());
    emit RecordingArmed();
}

// ---- per-frame sync ---------------------------------------------------------------------

void RecordingPanel::Tick()
{
    const bool recording = m_app.GetEncoder().IsRecording();
    if (recording != m_armed) SyncArmed(recording);
    if (recording) SyncStatus();
    else           SyncIdleAffordance();
}

void RecordingPanel::SyncIdleAffordance()
{
    const VideoDecoder& decoder = m_app.GetDecoder();
    const bool fileSource = decoder.IsOpen() && !decoder.IsLiveCapture();
    if (static_cast<int>(fileSource) == m_fileSourceApplied) return;
    m_fileSourceApplied = static_cast<int>(fileSource);

    m_toggle->setText(fileSource ? tr("Render Video to File") : tr("Start Recording"));
    m_toggle->setToolTip(fileSource
        ? tr("Play the open video from its first frame with the shader applied, writing "
             "every frame to the file above. It stops itself at the end (F9).")
        : tr("Begin writing every rendered frame to the file above (F9)."));
    m_idleHint->setText(fileSource
        ? tr("Renders the whole video from the start and stops at the end. "
             "F9 starts and stops from anywhere.")
        : tr("Not recording. F9 starts and stops from anywhere."));

    // The rate is the shader's to choose only when the shader is the source. An open video
    // supplies its own, so the control goes inert and says why rather than sitting there
    // live and doing nothing, which is how a setting gets blamed for an unrelated result.
    m_fps->setEnabled(!fileSource);
    m_fps->setToolTip(fileSource
        ? tr("The open video's own frame rate is used, so this does not apply. It sets the "
             "rate for a generative recording, with no video open.")
        : tr("The rate the file is written at. The render steps the shader by exactly one "
             "frame at a time, so this is the speed the result plays at no matter what "
             "the display refreshes at or how long each frame takes to draw."));
}

void RecordingPanel::SyncCodec()
{
    // Each codec shows only the control it actually reads: ProRes takes its rate from the
    // profile, so a bitrate box beside it would be a setting that changes nothing.
    const bool h264 = (m_codec->currentIndex() == 0);
    m_bitrateRow->setVisible(h264);
    m_profileRow->setVisible(!h264);
}

void RecordingPanel::SyncArmed(bool recording)
{
    m_armed = recording;
    // Both renders, not only the file one: a generative recording now steps the shader a
    // frame at a time too, so "Stop Rendering" is the honest verb for it as well.
    m_rendering = recording && (m_app.IsOfflineRender() || m_app.IsGenerativeRender());

    if (recording && !m_since.isValid()) m_since.start();
    if (!recording) m_since.invalidate();

    // FFmpeg took its copy of these at StartRecording. Leaving them live would let the panel
    // describe a file that is not the one being written.
    m_path->setEnabled(!recording);
    m_browse->setEnabled(!recording);
    m_codec->setEnabled(!recording);
    m_bitrate->setEnabled(!recording);
    m_profile->setEnabled(!recording);
    // Re-enabled by SyncIdleAffordance, which owns this one while idle: with a video open
    // it stays disabled because the video's rate is used, and only the tri-state's reset
    // below makes that run again.
    m_fps->setEnabled(!recording);

    m_statusBlock->setVisible(recording);
    m_idleHint->setVisible(!recording);

    m_toggle->setMinimumHeight(recording ? kToggleArmedHeight : kToggleIdleHeight);
    if (recording) {
        m_toggle->setText(m_rendering ? tr("Stop Rendering") : tr("Stop Recording"));
        m_toggle->setToolTip(m_rendering
            ? tr("Cancel the render and close the file where it has got to (F9).")
            : tr("End the recording and close the file (F9)."));
    } else {
        // The idle button is one of two different actions; SyncIdleAffordance picks it.
        m_fileSourceApplied = -1;
        SyncIdleAffordance();
    }

    // Red is spent on the state, not on the way out of it: armed, the accent-filled toggle
    // is the obvious thing to press, and the red belongs to the badge and the border.
    m_toggle->setObjectName(recording ? QStringLiteral("Primary") : QStringLiteral("Danger"));
    m_toggle->style()->unpolish(m_toggle);
    m_toggle->style()->polish(m_toggle);

    if (recording) {
        m_pulse->setDuration(Theme::kMotionBase * 2);
        m_pulse->start();
        SyncStatus();
    } else {
        m_pulse->stop();
        m_pulseT = 0.0;
    }
    update();
}

void RecordingPanel::SyncStatus()
{
    const VideoEncoder& encoder = m_app.GetEncoder();

    const QString elapsed = ElapsedText(m_since.isValid() ? m_since.elapsed() : 0);
    if (elapsed != m_elapsedText) {
        m_elapsedText = elapsed;
        m_elapsed->setText(elapsed);
    }

    // The path in full where it fits and middle-elided where it does not: the tail is the
    // file name, which is the half that identifies the recording.
    const QString path = QString::fromStdString(m_settings.outputPath);
    m_destination->setToolTip(path);
    const QString shown = ElideMiddle(m_destination, path, 4);
    if (shown != m_destination->text()) m_destination->setText(shown);

    const qint64 dropped = encoder.GetFramesDropped();
    QString counters = tr("%1 frames").arg(encoder.GetFramesEncoded());
    if (dropped > 0) {
        counters += QStringLiteral(", <span style='color:%1'>%2</span>")
                        .arg(Theme::kStateWarning.name())
                        .arg(tr("%1 dropped").arg(dropped));
    } else {
        counters += QStringLiteral(", ") + tr("0 dropped");
    }
    counters += QStringLiteral(", ") + tr("%1 fps").arg(encoder.GetEncodingFPS(), 0, 'f', 1);

    // A render has a known end, so say how far off it is. A live or generative recording
    // has none, and a percentage of nothing would be a number the panel made up.
    // IsOfflineRender rather than the cached m_rendering: the encoder thread takes a few
    // frames to wind down after the render itself has finished and rewound, and reading the
    // cache there would show a progress of 0% over the top of a completed file.
    if (m_app.IsOfflineRender()) {
        const double duration = m_app.GetDecoder().GetDuration();
        if (duration > 0.0) {
            const double done = std::clamp(m_app.GetPlaybackTime() / duration, 0.0, 1.0);
            counters.prepend(tr("%1%, ").arg(done * 100.0, 0, 'f', 0));
        }
    }

    if (counters != m_countersText) {
        m_countersText = counters;
        m_counters->setText(counters);
    }
}

// ---- paint ------------------------------------------------------------------------------

void RecordingPanel::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    if (m_pulseT <= 0.001) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor border = Theme::kStateError;
    border.setAlpha(static_cast<int>(kBorderAlpha * m_pulseT));

    painter.setPen(QPen(border, kBorderWidth));
    painter.setBrush(Qt::NoBrush);

    const qreal inset = kBorderWidth * 0.5;
    painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset),
                            Theme::kRadiusPanel, Theme::kRadiusPanel);
}

}  // namespace SP
