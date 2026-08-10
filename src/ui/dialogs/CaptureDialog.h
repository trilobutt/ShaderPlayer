#pragma once

// CaptureDialog.h — open a live source: a DirectShow capture device, or a stream URL.
//
// The device list is enumerated through the DirectShow system device enumerator exactly as
// the outgoing dialog did, on the COM apartment WinMain already initialised. That call walks
// real hardware and can take a noticeable moment, so it is deliberately deferred until after
// the dialog has painted: the surface opens on a "looking for devices" page and swaps to the
// list or to the empty page when the enumerator returns, rather than freezing an empty combo
// before the window is even on screen.

#include "ui/dialogs/Dialog.h"

#include <QString>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
QT_END_NAMESPACE

namespace SP {

class Application;

class CaptureDialog : public Dialog {
    Q_OBJECT
public:
    explicit CaptureDialog(Application& app, QWidget* parent = nullptr);

private:
    // Pages of the device section, in the order they are added to the stack.
    enum Page { kPageSearching = 0, kPageList, kPageEmpty };

    void BuildDeviceSection();
    void BuildUrlSection();

    void Enumerate();          // blocking; only ever called from the event loop, never in ctor
    void OpenSelectedDevice();
    void OpenUrl();
    void ReportFailure(const QString& source);

    Application& m_app;

    QStackedWidget* m_devicePages = nullptr;
    QListWidget* m_devices = nullptr;
    QPushButton* m_openDevice = nullptr;
    QPushButton* m_rescan = nullptr;

    QLineEdit* m_url = nullptr;
    QPushButton* m_openUrl = nullptr;

    QLabel* m_error = nullptr;
};

}  // namespace SP
