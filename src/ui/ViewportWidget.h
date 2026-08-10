#pragma once

#include "Common.h"

#include <dxgi1_3.h>

#include <QWidget>

QT_BEGIN_NAMESPACE
class QResizeEvent;
class QPaintEvent;
QT_END_NAMESPACE

namespace SP {

class Application;

// The one surface Qt must never paint. A native child window (via winId()) carrying
// its own IDXGISwapChain1 on the shared D3D11 device, blitted each frame from
// D3D11Renderer's already-processed display texture and presented with vsync.
// Modelled on VideoOutputWindow, which does the same for a standalone top-level
// window; here the swap chain's target HWND is the widget's own native handle.
class ViewportWidget : public QWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(Application& app, QWidget* parent = nullptr);
    ~ViewportWidget();

    // Creates the swap chain from winId(). Call once after the widget (and its
    // top-level window) have been shown, so the native handle is valid and sized.
    bool CreateSwapChain();

    // Clears to Theme::kCanvas, blits the current display texture letterboxed into
    // the widget, presents with vsync. Call once per frame tick. Returns whether it
    // actually presented (false if the swap chain isn't ready yet or there is nothing
    // with valid dimensions to draw) — the caller's frame pacing depends on this being
    // accurate, since a vsync Present is what throttles the loop.
    bool RenderAndPresent();

    QPaintEngine* paintEngine() const override { return nullptr; }

    // The handle DXGI signals when it can accept the next Present, used by main.cpp to
    // pace the event loop off the swap chain instead of blocking inside Present(). Null
    // when the frame-latency-waitable swap chain could not be created (falls back to a
    // polling timer, today's behaviour).
    HANDLE FrameLatencyWaitable() const { return m_frameLatencyWaitable; }

protected:
    void resizeEvent(QResizeEvent* event) override;   // ResizeBuffers, guard on 0x0
    void paintEvent(QPaintEvent*) override {}          // D3D owns these pixels

private:
    void RebuildRTV();

    Application& m_app;
    ComPtr<IDXGISwapChain1>        m_swapChain;
    ComPtr<IDXGISwapChain2>        m_swapChain2;   // the same object; DXGI 1.3 face of it
    HANDLE                         m_frameLatencyWaitable = nullptr;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    int m_width  = 0;
    int m_height = 0;
};

} // namespace SP
