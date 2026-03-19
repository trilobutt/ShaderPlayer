#include "Application.h"
#include <objbase.h>

// Global pointer used by the crash handler. Set just before Run() and cleared
// immediately after — narrow window where a crash actually needs this.
static SP::Application* g_appForCrashCleanup = nullptr;

// Unhandled exception filter: fires on access violations, assertion failures,
// and other fatal crashes that bypass normal C++ destructors.
// Releases the Spout sender from shared memory so SpoutCam and other receivers
// don't stay connected to a dead sender after the process dies.
// Returns EXCEPTION_CONTINUE_SEARCH so the normal crash handler (WER/debugger)
// still runs after we clean up.
static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS*) {
    if (g_appForCrashCleanup) {
        // Minimal-risk cleanup: just Spout and the decoder.
        // Avoid anything that requires a valid D3D device or window.
        __try {
            g_appForCrashCleanup->Shutdown();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // If Shutdown itself faults (heap corruption etc.), swallow it —
            // we're already crashing and the OS will clean up the rest.
        }
        g_appForCrashCleanup = nullptr;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow
) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Enable high DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // COM is required for IFileOpenDialog (folder/file pickers)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    SP::Application app;

    int result = 1;
    if (app.Initialize(hInstance, nCmdShow)) {
        g_appForCrashCleanup = &app;
        SetUnhandledExceptionFilter(OnUnhandledException);

        result = app.Run();

        SetUnhandledExceptionFilter(nullptr);
        g_appForCrashCleanup = nullptr;
    }

    CoUninitialize();
    return result;
}
