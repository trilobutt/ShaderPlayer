#include "VideoOutputWindow.h"
#include "D3D11Renderer.h"

namespace SP {

bool VideoOutputWindow::Open(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (m_hwnd) return true;
    m_device  = device;
    m_context = context;
    return CreateWindowAndSwapChain(1280, 720);
}

void VideoOutputWindow::Close() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        // WM_DESTROY clears m_hwnd, m_swapChain, m_rtv
    }
    m_device  = nullptr;
    m_context = nullptr;
}

void VideoOutputWindow::BlitAndPresent(D3D11Renderer& renderer) {
    if (!m_hwnd || !m_rtv) return;
    renderer.BlitDisplayTo(m_rtv.Get(), m_width, m_height);
    m_swapChain->Present(0, 0);
}

bool VideoOutputWindow::CreateWindowAndSwapChain(int width, int height) {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // Register the window class once; subsequent calls with the same name return
    // ERROR_CLASS_ALREADY_EXISTS, which is harmless.
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = StaticWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = L"ShaderPlayerVideoOutput";
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        0,
        L"ShaderPlayerVideoOutput",
        L"ShaderPlayer \u2014 Video Output",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        nullptr, nullptr,
        hInstance,
        this  // forwarded to WM_NCCREATE as CREATESTRUCTW::lpCreateParams
    );
    if (!m_hwnd) return false;

    // Obtain the DXGI factory from the existing device so both windows share the
    // same adapter and there is no cross-adapter copy.
    ComPtr<IDXGIDevice>  dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width       = static_cast<UINT>(width);
    desc.Height      = static_cast<UINT>(height);
    desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    if (FAILED(factory->CreateSwapChainForHwnd(
            m_device, m_hwnd, &desc, nullptr, nullptr, &m_swapChain))) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    // Prevent DXGI from hijacking Alt+Enter on this window
    factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    m_width  = width;
    m_height = height;
    RebuildRTV();
    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

void VideoOutputWindow::RebuildRTV() {
    m_rtv.Reset();
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return;
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);
}

void VideoOutputWindow::ResizeSwapChain(int width, int height) {
    m_rtv.Reset();
    m_swapChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                               DXGI_FORMAT_UNKNOWN, 0);
    m_width  = width;
    m_height = height;
    RebuildRTV();
}

LRESULT CALLBACK VideoOutputWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    VideoOutputWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<VideoOutputWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<VideoOutputWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT VideoOutputWindow::HandleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: {
        const int w = LOWORD(lParam);
        const int h = HIWORD(lParam);
        if (m_swapChain && w > 0 && h > 0)
            ResizeSwapChain(w, h);
        return 0;
    }
    case WM_DESTROY:
        m_rtv.Reset();
        m_swapChain.Reset();
        m_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace SP
