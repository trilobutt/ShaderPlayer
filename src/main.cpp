#include "Application.h"

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

    SP::Application app;

    if (!app.Initialize(hInstance, nCmdShow)) {
        return 1;
    }

    return app.Run();
}
