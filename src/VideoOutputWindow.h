#pragma once

#include "Common.h"

namespace SP {

class D3D11Renderer;

// A standalone Win32 window backed by its own IDXGISwapChain on the same D3D11
// device as the main renderer.  Each frame, BlitAndPresent() copies the already-
// processed display texture into this window's back buffer and presents it.
// The window appears as a separate entry in the taskbar / window switcher, so
// screen-sharing software (Discord, Zoom, Teams) can capture it directly via
// "Share a Window" without any virtual camera driver.
class VideoOutputWindow {
public:
    VideoOutputWindow() = default;
    ~VideoOutputWindow() { Close(); }

    VideoOutputWindow(const VideoOutputWindow&) = delete;
    VideoOutputWindow& operator=(const VideoOutputWindow&) = delete;

    bool Open(ID3D11Device* device, ID3D11DeviceContext* context);
    void Close();
    bool IsOpen() const { return m_hwnd != nullptr; }

    // Call after D3D11Renderer::RenderToDisplay() each frame.
    void BlitAndPresent(D3D11Renderer& renderer);

private:
    bool CreateWindowAndSwapChain(int width, int height);
    void RebuildRTV();
    void ResizeSwapChain(int width, int height);

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND                          m_hwnd      = nullptr;
    ComPtr<IDXGISwapChain1>       m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ID3D11Device*                 m_device    = nullptr;
    ID3D11DeviceContext*          m_context   = nullptr;
    int                           m_width     = 0;
    int                           m_height    = 0;
};

} // namespace SP
