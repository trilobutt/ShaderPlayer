#include "ui/dialogs/CaptureDialog.h"

#include "Application.h"
#include "ui/Theme.h"

#include <dshow.h>

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace SP {

namespace {

constexpr int kDialogWidth = 560;
constexpr int kListHeight  = 150;

// Enumerate DirectShow video capture devices (webcams, capture cards, virtual cameras).
// Unchanged from the outgoing dialog: the COM apartment is initialised in WinMain and this
// runs on that thread.
std::vector<std::string> EnumerateCaptureDevices()
{
    std::vector<std::string> devices;

    ICreateDevEnum* devEnum = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ICreateDevEnum, reinterpret_cast<void**>(&devEnum))))
        return devices;

    IEnumMoniker* enumMon = nullptr;
    HRESULT hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMon, 0);
    devEnum->Release();
    if (hr != S_OK || !enumMon)
        return devices;

    IMoniker* moniker = nullptr;
    while (enumMon->Next(1, &moniker, nullptr) == S_OK) {
        IPropertyBag* propBag = nullptr;
        if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
                                             reinterpret_cast<void**>(&propBag)))) {
            VARIANT var;
            VariantInit(&var);
            if (SUCCEEDED(propBag->Read(L"FriendlyName", &var, nullptr)) && var.vt == VT_BSTR) {
                int len = WideCharToMultiByte(CP_UTF8, 0, var.bstrVal, -1, nullptr, 0,
                                              nullptr, nullptr);
                if (len > 0) {
                    std::string name(len - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, var.bstrVal, -1, name.data(), len,
                                        nullptr, nullptr);
                    devices.push_back(std::move(name));
                }
            }
            VariantClear(&var);
            propBag->Release();
        }
        moniker->Release();
    }
    enumMon->Release();
    return devices;
}

}  // namespace

CaptureDialog::CaptureDialog(Application& app, QWidget* parent)
    : Dialog(tr("Open Stream / Webcam"), Theme::kRegionTransport, parent)
    , m_app(app)
{
    setMinimumWidth(kDialogWidth);

    BuildDeviceSection();
    BuildUrlSection();

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("StatusError"));
    m_error->setWordWrap(true);
    m_error->hide();
    Body()->addWidget(m_error);

    QHBoxLayout* buttons = AddButtonRow();
    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);

    // After the first paint, not during construction: the enumerator walks real hardware and
    // blocks, and a dialog that appears already finished hides that it did any work at all.
    QTimer::singleShot(0, this, &CaptureDialog::Enumerate);
}

void CaptureDialog::BuildDeviceSection()
{
    auto* heading = new QLabel(tr("Webcam or capture device"), this);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    Body()->addWidget(heading);

    m_devicePages = new QStackedWidget(this);

    // ---- searching -----------------------------------------------------------------------
    auto* searching = new QLabel(tr("Looking for capture devices..."), m_devicePages);
    searching->setObjectName(QStringLiteral("Caption"));
    searching->setAlignment(Qt::AlignCenter);
    searching->setMinimumHeight(kListHeight);
    m_devicePages->addWidget(searching);                 // kPageSearching

    // ---- the list ------------------------------------------------------------------------
    m_devices = new QListWidget(m_devicePages);
    m_devices->setSelectionMode(QAbstractItemView::SingleSelection);
    m_devices->setFrameShape(QFrame::NoFrame);
    m_devices->setMinimumHeight(kListHeight);
    connect(m_devices, &QListWidget::itemSelectionChanged, this, [this] {
        m_openDevice->setEnabled(m_devices->currentItem() != nullptr);
    });
    connect(m_devices, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { OpenSelectedDevice(); });
    m_devicePages->addWidget(m_devices);                 // kPageList

    // ---- nothing found ---------------------------------------------------------------------
    auto* empty = new QWidget(m_devicePages);
    empty->setObjectName(QStringLiteral("CaptureEmpty"));
    empty->setStyleSheet(QStringLiteral("QWidget#CaptureEmpty { background: transparent; }"));
    auto* emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(Theme::kSpaceUnit);
    emptyLayout->addStretch(1);

    auto* emptyTitle = new QLabel(tr("No capture devices found"), empty);
    QFont emptyFont(QString::fromLatin1(Theme::kFontUi));
    emptyFont.setPointSize(Theme::kFontSizePanelTitle);
    emptyFont.setBold(true);
    emptyTitle->setFont(emptyFont);
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);

    auto* emptyBlurb = new QLabel(
        tr("Windows is reporting no DirectShow video device. Plug a camera in, close "
           "whatever else is holding it, then search again. A stream URL below works "
           "either way."), empty);
    emptyBlurb->setObjectName(QStringLiteral("Caption"));
    emptyBlurb->setAlignment(Qt::AlignCenter);
    emptyBlurb->setWordWrap(true);
    emptyLayout->addWidget(emptyBlurb);

    m_rescan = new QPushButton(tr("Search Again"), empty);
    m_rescan->setCursor(Qt::PointingHandCursor);
    connect(m_rescan, &QPushButton::clicked, this, [this] {
        m_devicePages->setCurrentIndex(kPageSearching);
        QTimer::singleShot(0, this, &CaptureDialog::Enumerate);
    });
    emptyLayout->addWidget(m_rescan, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(1);
    empty->setMinimumHeight(kListHeight);
    m_devicePages->addWidget(empty);                     // kPageEmpty

    m_devicePages->setCurrentIndex(kPageSearching);
    Body()->addWidget(m_devicePages);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addStretch(1);
    m_openDevice = new QPushButton(tr("Open Device"), this);
    m_openDevice->setObjectName(QStringLiteral("Primary"));
    m_openDevice->setCursor(Qt::PointingHandCursor);
    m_openDevice->setEnabled(false);
    m_openDevice->setToolTip(tr("Start playing from the selected device."));
    connect(m_openDevice, &QPushButton::clicked, this, &CaptureDialog::OpenSelectedDevice);
    row->addWidget(m_openDevice);
    Body()->addLayout(row);
}

void CaptureDialog::BuildUrlSection()
{
    Body()->addSpacing(Theme::kSpaceUnit);

    auto* heading = new QLabel(tr("Stream URL"), this);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    Body()->addWidget(heading);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Theme::kSpaceUnit);

    m_url = new QLineEdit(this);
    m_url->setPlaceholderText(QStringLiteral("rtsp://192.168.1.100:554/stream"));
    m_url->setToolTip(tr("RTSP, RTMP or HTTP. For example:\n"
                         "  rtsp://192.168.1.100:554/stream\n"
                         "  rtmp://live.example.com/app/key\n"
                         "  http://cam.local/video.mjpg"));
    m_url->setClearButtonEnabled(true);
    connect(m_url, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_openUrl->setEnabled(!text.trimmed().isEmpty());
    });
    connect(m_url, &QLineEdit::returnPressed, this, [this] {
        if (m_openUrl->isEnabled()) OpenUrl();
    });
    row->addWidget(m_url, 1);

    m_openUrl = new QPushButton(tr("Open URL"), this);
    m_openUrl->setCursor(Qt::PointingHandCursor);
    m_openUrl->setEnabled(false);
    m_openUrl->setToolTip(tr("Type an address first."));
    connect(m_openUrl, &QPushButton::clicked, this, &CaptureDialog::OpenUrl);
    row->addWidget(m_openUrl);

    Body()->addLayout(row);
}

void CaptureDialog::Enumerate()
{
    const std::vector<std::string> devices = EnumerateCaptureDevices();

    m_devices->clear();
    for (const std::string& name : devices) {
        m_devices->addItem(QString::fromStdString(name));
    }

    if (devices.empty()) {
        m_openDevice->setEnabled(false);
        m_devicePages->setCurrentIndex(kPageEmpty);
        return;
    }

    m_devices->setCurrentRow(0);
    m_openDevice->setEnabled(true);
    m_devicePages->setCurrentIndex(kPageList);
    m_devices->setFocus();
}

void CaptureDialog::OpenSelectedDevice()
{
    const QListWidgetItem* item = m_devices->currentItem();
    if (!item) return;

    const QString name = item->text();
    if (!m_app.OpenCapture(name.toStdString(), /*isDshow=*/true)) {
        ReportFailure(name);
        return;
    }
    accept();
}

void CaptureDialog::OpenUrl()
{
    const QString url = m_url->text().trimmed();
    if (url.isEmpty()) return;

    if (!m_app.OpenCapture(url.toStdString(), /*isDshow=*/false)) {
        ReportFailure(url);
        return;
    }
    accept();
}

void CaptureDialog::ReportFailure(const QString& source)
{
    // A source that will not open leaves the dialog open holding what was tried, rather than
    // closing onto an unchanged picture with nothing said.
    m_error->setText(tr("Could not open %1. Check that it is connected, not already in use, "
                        "and reachable.").arg(source));
    m_error->show();
}

}  // namespace SP
