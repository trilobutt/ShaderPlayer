#include "ui/ViewportWidget.h"

#include "Application.h"
#include "D3D11Renderer.h"
#include "VideoDecoder.h"
#include "ui/Theme.h"

#include <algorithm>

#include <QResizeEvent>

namespace SP {

ViewportWidget::ViewportWidget(Application& app, QWidget* parent)
    : QWidget(parent), m_app(app) {
    // D3D owns this surface's pixels outright; Qt must never composite over it.
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

bool ViewportWidget::CreateSwapChain() {
    ID3D11Device* device = m_app.GetRenderer().GetDevice();
    if (!device) return false;

    // Obtain the DXGI factory from the existing device so the widget's swap chain
    // shares the same adapter as the main renderer — no cross-adapter copy.
    ComPtr<IDXGIDevice>   dxgiDevice;
    ComPtr<IDXGIAdapter>  adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        return false;
    }

    const int w = std::max(1, width());
    const int h = std::max(1, height());

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width            = static_cast<UINT>(w);
    desc.Height           = static_cast<UINT>(h);
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount      = 2;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (FAILED(factory->CreateSwapChainForHwnd(
            device, hwnd, &desc, nullptr, nullptr, &m_swapChain))) {
        return false;
    }

    // Prevent DXGI from hijacking Alt+Enter on this window.
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    m_width  = w;
    m_height = h;
    RebuildRTV();
    return true;
}

void ViewportWidget::RebuildRTV() {
    m_rtv.Reset();
    if (!m_swapChain) return;
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return;
    m_app.GetRenderer().GetDevice()->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);
}

void ViewportWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // The renderer's `resolution` cbuffer field tracks this widget's client size
    // regardless of the swap chain below, so shaders see a live value even before the
    // swap chain exists (first layout, before CreateSwapChain has run).
    m_app.GetRenderer().Resize(std::max(1, width()), std::max(1, height()));

    if (!m_swapChain) return;

    const int w = std::max(1, width());
    const int h = std::max(1, height());
    if (w == m_width && h == m_height) return;

    m_rtv.Reset();
    m_swapChain->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h),
                                DXGI_FORMAT_UNKNOWN, 0);
    m_width  = w;
    m_height = h;
    RebuildRTV();
}

bool ViewportWidget::RenderAndPresent() {
    if (!m_swapChain || !m_rtv) return false;

    D3D11Renderer& renderer = m_app.GetRenderer();

    // Source dimensions: the decoder's when a video is open, else the generative
    // render resolution — same split as UIManager::DrawVideoViewport's letterbox.
    float srcW, srcH;
    if (m_app.GetDecoder().IsOpen()) {
        srcW = static_cast<float>(m_app.GetDecoder().GetWidth());
        srcH = static_cast<float>(m_app.GetDecoder().GetHeight());
    } else {
        srcW = static_cast<float>(renderer.GetGenerativeWidth());
        srcH = static_cast<float>(renderer.GetGenerativeHeight());
    }
    if (srcW <= 0.0f || srcH <= 0.0f || m_width <= 0 || m_height <= 0) return false;

    const float availW = static_cast<float>(m_width);
    const float availH = static_cast<float>(m_height);
    const float scale  = std::min(availW / srcW, availH / srcH);
    const int   drawW  = static_cast<int>(srcW * scale);
    const int   drawH  = static_cast<int>(srcH * scale);
    const int   drawX  = static_cast<int>((availW - static_cast<float>(drawW)) * 0.5f);
    const int   drawY  = static_cast<int>((availH - static_cast<float>(drawH)) * 0.5f);

    const float clearColor[4] = {
        static_cast<float>(Theme::kCanvas.redF()),
        static_cast<float>(Theme::kCanvas.greenF()),
        static_cast<float>(Theme::kCanvas.blueF()),
        1.0f
    };
    renderer.BlitDisplayToRect(m_rtv.Get(), drawX, drawY, drawW, drawH, clearColor);
    m_swapChain->Present(1, 0);
    return true;
}

} // namespace SP
