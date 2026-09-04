#include "Application.h"
#include "FrameProfiler.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/ViewportWidget.h"

#include <objbase.h>
#include <dbghelp.h>

#include <cstdio>
#include <memory>

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QWinEventNotifier>

// Global pointer used by the crash handler. Set just before the event loop and
// cleared immediately after — narrow window where a crash actually needs this.
static SP::Application* g_appForCrashCleanup = nullptr;

// Writes a symbolised stack trace to crash.log next to the executable.
//
// This exists because the machine has no debugger installed and a GUI process
// gives no console to crash into: without it an access violation is a bare
// 0xC0000005 exit code and nothing else. DbgHelp ships with Windows, so this
// costs one import and no deployment.
//
// Everything here runs inside a crashed process, so it stays on the stack, takes
// no locks and allocates nothing.
static void WriteCrashLog(EXCEPTION_POINTERS* ep) {
    char path[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) return;
    if (char* slash = strrchr(path, '\\')) {
        strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "crash.log");
    }

    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;

    const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    fprintf(f, "Exception 0x%08lX at %p\n\n", code,
            ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr);

    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);

    if (ep && ep->ContextRecord) {
        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 frame = {};
        frame.AddrPC.Offset    = ctx.Rip;  frame.AddrPC.Mode    = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp;  frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp;  frame.AddrStack.Mode = AddrModeFlat;

        // SYMBOL_INFO is variable-length: the name follows the struct.
        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512] = {};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 511;

        for (int i = 0; i < 64; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(),
                             &frame, &ctx, nullptr, SymFunctionTableAccess64,
                             SymGetModuleBase64, nullptr)) {
                break;
            }
            if (frame.AddrPC.Offset == 0) break;

            DWORD64 disp = 0;
            if (SymFromAddr(process, frame.AddrPC.Offset, &disp, sym)) {
                fprintf(f, "%2d  %s + 0x%llX", i, sym->Name, disp);
            } else {
                fprintf(f, "%2d  0x%llX", i, frame.AddrPC.Offset);
            }

            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisp = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisp, &line)) {
                fprintf(f, "   [%s:%lu]", line.FileName, line.LineNumber);
            }
            fputc('\n', f);
        }
    }

    fclose(f);
}

// Unhandled exception filter: fires on access violations, assertion failures,
// and other fatal crashes that bypass normal C++ destructors.
// Releases the Spout sender from shared memory so SpoutCam and other receivers
// don't stay connected to a dead sender after the process dies.
// Returns EXCEPTION_CONTINUE_SEARCH so the normal crash handler (WER/debugger)
// still runs after we clean up.
static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* ep) {
    WriteCrashLog(ep);

    if (g_appForCrashCleanup) {
        // Minimal-risk cleanup, and now actually minimal: releasing the Spout sender is
        // the one piece of state that outlives this process. Shutdown() was doing far more
        // than the comment claimed — writing config.json and tearing down the whole Qt
        // widget tree — from a process whose heap may already be corrupt, which is how a
        // crash took the user's configuration with it.
        __try {
            g_appForCrashCleanup->CrashCleanup();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // If the cleanup itself faults, swallow it — we're already crashing and the
            // OS will clean up the rest.
        }
        g_appForCrashCleanup = nullptr;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Input delivery latency: the gap between the OS stamping an input event and Qt handing
// it to a widget. No Q_OBJECT — eventFilter is a plain virtual, and the macro would need
// MOC for nothing here.
class InputLatencyFilter : public QObject {
public:
    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::KeyPress:
        case QEvent::Wheel: {
            // event->type() already narrows this to a QInputEvent subtype.
            auto* ie = static_cast<QInputEvent*>(event);
            const double lag = static_cast<double>(
                GetTickCount64() - static_cast<ULONGLONG>(ie->timestamp()));
            // Discard samples outside [0, 2000) ms as clock noise.
            if (lag >= 0.0 && lag < 2000.0) SP::Profile::NoteInput(lag);
            break;
        }
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }
};

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow
) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // Enable high DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // COM is required for the shell file pickers and for the DirectShow device
    // enumeration behind the capture dialog.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Installed before anything else runs. Startup is exactly where a crash is
    // least survivable and hardest to see: a GUI process has no console, and
    // installing this after initialisation would leave the whole of InitializeQt
    // uncovered.
    SetUnhandledExceptionFilter(OnUnhandledException);

    int result = 1;
    {
        // QApplication first, and before SP::Application: a QWidget cannot be
        // constructed without one, and InitializeQt() builds MainWindow. __argc/__argv
        // are the CRT's parsed command line — WinMain's lpCmdLine is unsplit, and Qt
        // requires an argv that outlives the QApplication.
        QApplication qtApp(__argc, __argv);
        SP::Theme::Apply(qtApp);

        // Declared after qtApp, so destroyed before it: Shutdown() destroys MainWindow,
        // and a QWidget must not outlive its QApplication.
        SP::Application app;
        if (app.InitializeQt()) {
            // The filter itself is installed at the top of WinMain; this only arms
            // the Spout cleanup, which needs a live Application to act on.
            g_appForCrashCleanup = &app;

            // The viewport's swap chain signals a waitable handle when DXGI can accept the
            // next frame, and the Qt event loop waits on it alongside the window's own
            // messages. Nothing blocks the GUI thread, so a click is dispatched when it
            // arrives instead of behind the frame.
            //
            // The handle is the pacer only while something is actually being presented.
            // With no video and no active shader nothing presents, no frame retires, and
            // the handle would either stall the loop or, still signalled, spin it — so the
            // timer takes over for exactly that case and hands back on the first frame
            // that presents.
            constexpr int kIdlePollMs = 16;   // ~60 Hz poll for the empty-state window
            QTimer idleTimer;
            idleTimer.setInterval(kIdlePollMs);

            HANDLE waitable = nullptr;
            if (SP::MainWindow* window = app.GetMainWindow()) {
                if (SP::ViewportWidget* viewport = window->Viewport())
                    waitable = viewport->FrameLatencyWaitable();
            }
            std::unique_ptr<QWinEventNotifier> vsyncNotifier;

            double lastTickEnd = 0.0;
            auto tick = [&app, &idleTimer, &vsyncNotifier, &lastTickEnd] {
                const double t0 = SP::Profile::Now();
                if (lastTickEnd > 0.0)
                    SP::Profile::Add(SP::Profile::kEventLoopGap, t0 - lastTickEnd);
                const bool presented = app.TickOnce();
                lastTickEnd = SP::Profile::Now();
                SP::Profile::EndFrame();

                if (vsyncNotifier) {
                    vsyncNotifier->setEnabled(presented);
                    if (presented) idleTimer.stop(); else idleTimer.start();
                }
            };

            if (waitable) {
                vsyncNotifier = std::make_unique<QWinEventNotifier>(waitable);
                QObject::connect(vsyncNotifier.get(), &QWinEventNotifier::activated,
                                 &qtApp, tick);
            }
            QObject::connect(&idleTimer, &QTimer::timeout, &qtApp, tick);
            idleTimer.start();

            // Input delivery latency: the gap between the OS stamping an input event and
            // Qt handing it to a widget. It is the number the user actually feels, and the
            // only one that says whether the frame loop is standing on the event loop.
            InputLatencyFilter latencyFilter;
            qtApp.installEventFilter(&latencyFilter);

            result = qApp->exec();

            // The filter stays installed for the rest of the process: teardown is
            // still a place worth getting a stack trace from. Only the Spout
            // cleanup is disarmed, since `app` is about to go out of scope.
            g_appForCrashCleanup = nullptr;
        }
    }

    CoUninitialize();
    return result;
}
