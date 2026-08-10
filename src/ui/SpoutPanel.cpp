#include "ui/SpoutPanel.h"

#include "Application.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

namespace SP {

namespace {

// The state word is the largest thing in the dock: it is the one question the panel answers.
constexpr int kStatePoints = Theme::kFontSizePanelTitle + 5;

constexpr int kDotSize = 14;

// The lit border carried while the sender is live. Steady, not pulsing: sending is a settled
// state rather than something running out, so movement would be decoration.
constexpr int kBorderAlpha = 200;
constexpr int kBorderWidth = 2;

constexpr int kNameMaxChars = 127;   // spoutDX's own sender-name limit

const char* kSpoutCamUrl = "https://github.com/leadedge/SpoutCam/releases/latest";

}  // namespace

SpoutPanel::SpoutPanel(Application& app, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
{
    setObjectName(QStringLiteral("SpoutPanel"));

    // The application stylesheet paints every QWidget with the canvas, which would lay an
    // opaque black slab over this dock's glass. Named containers opt out; the controls keep
    // their own fills. The sender name is monospace because it is a literal string a person
    // has to match by eye in another application's source list.
    setStyleSheet(QStringLiteral(
        "QWidget#SpoutPanel, QWidget#SpoutRow, QWidget#SpoutStatus { background: transparent; }"
        "QLabel#SpoutState { font-size: %1pt; font-weight: bold; }"
        "QLineEdit#SpoutName { font-family: %2; }")
        .arg(kStatePoints)
        .arg(QString::fromLatin1(Theme::kFontMono)));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kSpaceUnit);

    BuildStatus(layout);
    BuildSender(layout);
    layout->addStretch(1);
    BuildReceivers(layout);

    SyncStatus();
}

void SpoutPanel::BuildStatus(QVBoxLayout* layout)
{
    auto* status = new QWidget(this);
    status->setObjectName(QStringLiteral("SpoutStatus"));
    auto* column = new QVBoxLayout(status);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);

    auto* head = new QWidget(status);
    head->setObjectName(QStringLiteral("SpoutRow"));
    auto* headLine = new QHBoxLayout(head);
    headLine->setContentsMargins(0, 0, 0, 0);
    headLine->setSpacing(Theme::kSpaceUnit + 2);

    // The dot repeats the state in shape and colour beside the word, so the state survives
    // a glance that lands before the word is read.
    m_dot = new QWidget(head);
    m_dot->setObjectName(QStringLiteral("SpoutDot"));
    m_dot->setFixedSize(kDotSize, kDotSize);
    headLine->addWidget(m_dot, 0, Qt::AlignVCenter);

    m_stateWord = new QLabel(head);
    m_stateWord->setObjectName(QStringLiteral("SpoutState"));
    headLine->addWidget(m_stateWord, 0, Qt::AlignVCenter);
    headLine->addStretch(1);
    column->addWidget(head);

    m_stateDetail = new QLabel(status);
    m_stateDetail->setObjectName(QStringLiteral("Caption"));
    m_stateDetail->setWordWrap(true);
    column->addWidget(m_stateDetail);

    // Only ever visible when spoutDX has renamed the sender out from under the setting.
    m_collision = new QLabel(status);
    m_collision->setObjectName(QStringLiteral("StatusWarning"));
    m_collision->setWordWrap(true);
    m_collision->setVisible(false);
    column->addWidget(m_collision);

    layout->addWidget(status);

    m_enable = new QCheckBox(tr("Send via Spout"), this);
    m_enable->setCursor(Qt::PointingHandCursor);
    m_enable->setToolTip(tr("Share every rendered frame as a Spout sender, for any "
                            "Spout-aware application on this machine (F8)."));
    connect(m_enable, &QCheckBox::toggled, this, [this](bool on) {
        m_app.SetSpoutEnabled(on);
        // Enabling can fail: Spout initialises at launch and may not have. SyncStatus puts
        // the box back where the sender actually is and says why it went there.
        SyncStatus();
    });
    layout->addWidget(m_enable);
}

void SpoutPanel::BuildSender(QVBoxLayout* layout)
{
    auto* heading = new QLabel(tr("Sender name"), this);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(heading);

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("SpoutName"));
    m_name->setMaxLength(kNameMaxChars);
    m_name->setPlaceholderText(QStringLiteral("ShaderPlayer"));
    m_name->setText(QString::fromStdString(m_app.GetConfig().spoutSenderName));
    m_name->setToolTip(tr("The name receivers pick this stream out of their source list by."));
    // editingFinished covers Return and leaving the field, so a typed name cannot be lost by
    // clicking away from it.
    connect(m_name, &QLineEdit::editingFinished, this, &SpoutPanel::ApplySenderName);
    layout->addWidget(m_name);

    auto* hint = new QLabel(tr("Applied when you press Enter or leave the field."), this);
    hint->setObjectName(QStringLiteral("Caption"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
}

void SpoutPanel::BuildReceivers(QVBoxLayout* layout)
{
    auto* heading = new QLabel(tr("Receivers"), this);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(heading);

    auto* blurb = new QLabel(
        tr("Resolume, MadMapper, OBS (Spout2 plugin) and SpoutCam (virtual webcam) all read "
           "a Spout sender directly, with no capture card and no second encode."), this);
    blurb->setObjectName(QStringLiteral("Caption"));
    blurb->setWordWrap(true);
    layout->addWidget(blurb);

    auto* spoutCam = new QPushButton(tr("Get SpoutCam (virtual webcam)..."), this);
    spoutCam->setCursor(Qt::PointingHandCursor);
    spoutCam->setToolTip(tr("Opens the SpoutCam releases page in your browser. SpoutCam "
                            "turns this sender into a webcam any application can select."));
    connect(spoutCam, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QString::fromLatin1(kSpoutCamUrl)));
    });
    layout->addWidget(spoutCam);
}

// ---- state -------------------------------------------------------------------------------

SpoutPanel::State SpoutPanel::CurrentState() const
{
    if (m_app.IsSpoutActive())  return State::Sending;
    if (m_app.IsSpoutEnabled()) return State::Waiting;

    // Enabled in the config with nothing behind it: Spout failed to initialise at launch.
    // Without this branch that failure is completely silent.
    if (m_app.GetConfig().spoutEnabled) return State::Unavailable;
    return State::Off;
}

void SpoutPanel::ApplySenderName()
{
    const QString typed = m_name->text().trimmed();
    if (typed.isEmpty()) {
        // An empty name would leave spoutDX falling back to the executable name, which is
        // not what the field says. Put the current one back rather than accepting it.
        const QSignalBlocker block(m_name);
        m_name->setText(QString::fromStdString(m_app.GetConfig().spoutSenderName));
        return;
    }
    if (typed.toStdString() == m_app.GetConfig().spoutSenderName) return;

    m_app.SetSpoutSenderName(typed.toStdString());
    SyncStatus();
}

void SpoutPanel::Tick()
{
    // A hidden dock has nothing to show and no reason to be polled.
    if (!isVisible()) return;
    SyncStatus();
}

void SpoutPanel::SyncStatus()
{
    const State state = CurrentState();
    const QString activeName = QString::fromStdString(m_app.GetSpoutActiveSenderName());
    if (m_synced && state == m_state && activeName == m_activeName) return;

    m_state = state;
    m_activeName = activeName;
    m_synced = true;

    const QString requested = QString::fromStdString(m_app.GetConfig().spoutSenderName);

    QColor colour = Theme::kTextSecondary;
    QString word;
    QString detail;

    switch (state) {
    case State::Sending:
        colour = Theme::kAccentTertiary;
        word   = tr("SENDING");
        detail = tr("Live. Receivers on this machine can pick it up as \"%1\".")
                     .arg(activeName.isEmpty() ? requested : activeName);
        break;
    case State::Waiting:
        colour = Theme::kStateWarning;
        word   = tr("WAITING");
        detail = tr("On, but no frame has been shared yet. The sender registers with the "
                    "first frame ShaderPlayer renders.");
        break;
    case State::Unavailable:
        colour = Theme::kStateError;
        word   = tr("UNAVAILABLE");
        detail = tr("Spout could not start on this machine, so nothing is being shared. "
                    "Install the Spout runtime and restart ShaderPlayer.");
        break;
    case State::Off:
        word   = tr("OFF");
        detail = tr("Nothing is leaving this machine. Turn on Send via Spout and the frame "
                    "you see becomes a source other applications can read.");
        break;
    }

    m_stateWord->setText(word);
    m_stateWord->setStyleSheet(QStringLiteral("color: %1;").arg(colour.name()));
    m_dot->setStyleSheet(QStringLiteral("QWidget#SpoutDot { background: %1;"
                                        " border-radius: %2px; }")
                             .arg(colour.name())
                             .arg(kDotSize / 2));
    m_stateDetail->setText(detail);

    // spoutDX appends a suffix when the requested name is already registered. Nothing else
    // reports that, and a receiver looking for the name in the field above would never find
    // this stream.
    const bool renamed = (state == State::Sending)
                      && !activeName.isEmpty()
                      && activeName != requested;
    m_collision->setVisible(renamed);
    if (renamed) {
        m_collision->setText(tr("\"%1\" is already in use by another application. This "
                                "stream is registered as \"%2\", so look for that instead.")
                                 .arg(requested, activeName));
    }

    {
        const QSignalBlocker block(m_enable);
        m_enable->setChecked(m_app.IsSpoutEnabled());
    }

    update();
}

// ---- paint ------------------------------------------------------------------------------

void SpoutPanel::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    if (m_state != State::Sending) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor border = Theme::kAccentTertiary;
    border.setAlpha(kBorderAlpha);

    painter.setPen(QPen(border, kBorderWidth));
    painter.setBrush(Qt::NoBrush);

    const qreal inset = kBorderWidth * 0.5;
    painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset),
                            Theme::kRadiusPanel, Theme::kRadiusPanel);
}

}  // namespace SP
