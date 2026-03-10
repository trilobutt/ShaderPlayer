#include "D3D11Renderer.h"
#include <stdexcept>
#include <fstream>

namespace SP {

// Fullscreen triangle vertex shader
static const char* g_vertexShaderSource = R"(
struct VS_INPUT {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = float4(input.pos, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}
)";

// Compositor pixel shader — blends video (t0) with generative output (t2).
// Blend mode is in padding2.x (cast to int), blend amount in padding1.
static const char* g_compositorShaderSource = R"(
Texture2D videoTexture      : register(t0);
SamplerState videoSampler   : register(s0);
Texture2D noiseTexture      : register(t1);
SamplerState noiseSampler   : register(s1);
Texture2D generativeTexture : register(t2);

cbuffer Constants : register(b0) {
    float time;
    float blendAmount;      // padding1
    float2 resolution;
    float2 videoResolution;
    float2 blendParams;     // blendParams.x = blendMode (as float int)
    float4 custom[4];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float4 v = videoTexture.Sample(videoSampler, input.uv);
    float4 g = generativeTexture.Sample(videoSampler, input.uv);
    int mode = int(blendParams.x);

    float3 r;
    if (mode == 2) {
        // Add
        r = saturate(v.rgb + g.rgb);
    } else if (mode == 3) {
        // Multiply
        r = v.rgb * g.rgb;
    } else if (mode == 4) {
        // Screen
        r = 1.0 - (1.0 - v.rgb) * (1.0 - g.rgb);
    } else if (mode == 5) {
        // Overlay
        r = lerp(2.0 * v.rgb * g.rgb,
                 1.0 - 2.0 * (1.0 - v.rgb) * (1.0 - g.rgb),
                 step(0.5, v.rgb));
    } else if (mode == 6) {
        // Soft Light
        r = lerp(2.0 * v.rgb * g.rgb + v.rgb * v.rgb * (1.0 - 2.0 * g.rgb),
                 sqrt(v.rgb) * (2.0 * g.rgb - 1.0) + 2.0 * v.rgb * (1.0 - g.rgb),
                 step(0.5, g.rgb));
    } else if (mode == 7) {
        // Difference
        r = abs(v.rgb - g.rgb);
    } else if (mode == 8) {
        // Exclusion
        r = v.rgb + g.rgb - 2.0 * v.rgb * g.rgb;
    } else if (mode == 9) {
        // Darken
        r = min(v.rgb, g.rgb);
    } else if (mode == 10) {
        // Lighten
        r = max(v.rgb, g.rgb);
    } else {
        // Normal (mode == 1) or fallback
        r = g.rgb;
    }

    // Blend: lerp video toward blended result by blendAmount
    float3 out_rgb = lerp(v.rgb, r, blendAmount);
    return float4(out_rgb, 1.0);
}
)";

// Passthrough pixel shader
static const char* g_passthroughShaderSource = R"(
Texture2D videoTexture : register(t0);
SamplerState videoSampler : register(s0);

cbuffer Constants : register(b0) {
    float time;
    float padding1;
    float2 resolution;
    float2 videoResolution;
    float2 padding2;
    float4 custom[4];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    return videoTexture.Sample(videoSampler, input.uv);
}
)";

D3D11Renderer::D3D11Renderer() = default;

D3D11Renderer::~D3D11Renderer() {
    Shutdown();
}

bool D3D11Renderer::Initialize(HWND hwnd, int width, int height) {
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;

    if (!CreateDeviceAndSwapChain(hwnd, width, height)) {
        return false;
    }

    if (!CreateRenderTarget()) {
        return false;
    }

    if (!CreateShaderResources()) {
        return false;
    }

    if (!CreatePassthroughShader()) {
        return false;
    }

    if (!CreateCompositorShader()) {
        return false;
    }

    m_activePS = m_passthroughPS;
    return true;
}

void D3D11Renderer::Shutdown() {
    if (m_context) {
        m_context->ClearState();
        m_context->Flush();
    }

    ReleaseRenderTarget();

    m_videoTexture.Reset();
    m_videoSRV.Reset();
    m_renderTexture.Reset();
    m_renderTextureRTV.Reset();
    m_stagingTexture.Reset();
    m_displayTexture.Reset();
    m_displayRTV.Reset();
    m_displaySRV.Reset();
    m_noiseTexture.Reset();
    m_noiseSRV.Reset();
    m_wrapSampler.Reset();
    m_compositorPS.Reset();
    m_compositorSrcTexture.Reset();
    m_compositorSrcRTV.Reset();
    m_compositorSrcSRV.Reset();
    m_vertexShader.Reset();
    m_passthroughPS.Reset();
    m_activePS.Reset();
    m_inputLayout.Reset();
    m_vertexBuffer.Reset();
    m_constantBuffer.Reset();
    m_sampler.Reset();
    m_blendState.Reset();
    m_rasterizerState.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
}

bool D3D11Renderer::CreateDeviceAndSwapChain(HWND hwnd, int width, int height) {
    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        &m_device,
        &featureLevel,
        &m_context
    );

#ifdef _DEBUG
    // D3D11 debug layer requires "Graphics Tools" Windows optional feature.
    // Retry without it if that's the cause of failure.
    if (FAILED(hr) && (createDeviceFlags & D3D11_CREATE_DEVICE_DEBUG)) {
        createDeviceFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createDeviceFlags,
            featureLevels,
            _countof(featureLevels),
            D3D11_SDK_VERSION,
            &m_device,
            &featureLevel,
            &m_context
        );
    }
#endif

    if (FAILED(hr)) {
        return false;
    }

    // Get DXGI factory
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) return false;

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    hr = dxgiFactory->CreateSwapChainForHwnd(
        m_device.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &m_swapChain
    );

    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTargetView);
    return SUCCEEDED(hr);
}

void D3D11Renderer::ReleaseRenderTarget() {
    if (m_context) {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    m_renderTargetView.Reset();
}

bool D3D11Renderer::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    
    m_width = width;
    m_height = height;

    ReleaseRenderTarget();

    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return false;

    return CreateRenderTarget();
}

bool D3D11Renderer::CreateShaderResources() {
    // Compile vertex shader
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> errorBlob;
    
    HRESULT hr = D3DCompile(
        g_vertexShaderSource,
        strlen(g_vertexShaderSource),
        "VertexShader",
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &vsBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        return false;
    }

    hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &m_vertexShader
    );

    if (FAILED(hr)) return false;

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = m_device->CreateInputLayout(
        inputLayout,
        _countof(inputLayout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &m_inputLayout
    );

    if (FAILED(hr)) return false;

    // Create fullscreen quad vertex buffer
    struct Vertex {
        float x, y, u, v;
    };

    // Fullscreen triangle (more efficient than quad)
    Vertex vertices[] = {
        { -1.0f,  3.0f, 0.0f, -1.0f },
        { -1.0f, -1.0f, 0.0f,  1.0f },
        {  3.0f, -1.0f, 2.0f,  1.0f },
    };

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = vertices;

    hr = m_device->CreateBuffer(&vbDesc, &vbData, &m_vertexBuffer);
    if (FAILED(hr)) return false;

    // Create constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(ShaderConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr)) return false;

    // Create sampler
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = m_device->CreateSamplerState(&samplerDesc, &m_sampler);
    if (FAILED(hr)) return false;

    // Wrap sampler for noise texture (s1)
    D3D11_SAMPLER_DESC wrapDesc = {};
    wrapDesc.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    wrapDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    wrapDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    wrapDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    wrapDesc.MaxLOD   = D3D11_FLOAT32_MAX;

    hr = m_device->CreateSamplerState(&wrapDesc, &m_wrapSampler);
    if (FAILED(hr)) return false;

    // Create blend state (opaque — all compositing done in the compositor shader)
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_device->CreateBlendState(&blendDesc, &m_blendState);
    if (FAILED(hr)) return false;

    // Create rasterizer state
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;

    hr = m_device->CreateRasterizerState(&rasterDesc, &m_rasterizerState);
    if (FAILED(hr)) return false;

    // Audio cbuffer (b1) — always bound, zeroed when no audio.
    D3D11_BUFFER_DESC audioCBDesc = {};
    audioCBDesc.ByteWidth      = sizeof(AudioConstants);
    audioCBDesc.Usage          = D3D11_USAGE_DYNAMIC;
    audioCBDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    audioCBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = m_device->CreateBuffer(&audioCBDesc, nullptr, &m_audioConstantBuffer);
    if (FAILED(hr)) return false;

    // Spectrum texture (t3): 1×256 R32_FLOAT DYNAMIC — updated each frame.
    D3D11_TEXTURE2D_DESC specDesc = {};
    specDesc.Width     = AudioData::kSpectrumBins;
    specDesc.Height    = 1;
    specDesc.MipLevels = 1;
    specDesc.ArraySize = 1;
    specDesc.Format    = DXGI_FORMAT_R32_FLOAT;
    specDesc.SampleDesc.Count  = 1;
    specDesc.Usage             = D3D11_USAGE_DYNAMIC;
    specDesc.BindFlags         = D3D11_BIND_SHADER_RESOURCE;
    specDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
    hr = m_device->CreateTexture2D(&specDesc, nullptr, &m_spectrumTexture);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC specSRVDesc = {};
    specSRVDesc.Format              = DXGI_FORMAT_R32_FLOAT;
    specSRVDesc.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
    specSRVDesc.Texture2D.MipLevels = 1;
    hr = m_device->CreateShaderResourceView(m_spectrumTexture.Get(), &specSRVDesc, &m_spectrumSRV);
    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreatePassthroughShader() {
    std::string error;
    return CompilePixelShader(g_passthroughShaderSource, m_passthroughPS, error);
}

bool D3D11Renderer::CreateCompositorShader() {
    std::string error;
    return CompilePixelShader(g_compositorShaderSource, m_compositorPS, error);
}

bool D3D11Renderer::CreateCompositorSrcTexture(int width, int height) {
    if (m_compositorSrcWidth == width && m_compositorSrcHeight == height && m_compositorSrcTexture)
        return true;

    m_compositorSrcTexture.Reset();
    m_compositorSrcRTV.Reset();
    m_compositorSrcSRV.Reset();

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width     = static_cast<UINT>(width);
    texDesc.Height    = static_cast<UINT>(height);
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage     = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_compositorSrcTexture);
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(m_compositorSrcTexture.Get(), nullptr, &m_compositorSrcRTV);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format        = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = m_device->CreateShaderResourceView(m_compositorSrcTexture.Get(), &srvDesc, &m_compositorSrcSRV);
    if (FAILED(hr)) return false;

    m_compositorSrcWidth  = width;
    m_compositorSrcHeight = height;
    return true;
}

bool D3D11Renderer::CreateVideoTexture(int width, int height) {
    if (m_videoWidth == width && m_videoHeight == height && m_videoTexture) {
        return true;  // Already the right size
    }

    m_videoTexture.Reset();
    m_videoSRV.Reset();

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DYNAMIC;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_videoTexture);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = m_device->CreateShaderResourceView(m_videoTexture.Get(), &srvDesc, &m_videoSRV);
    if (FAILED(hr)) return false;

    m_videoWidth = width;
    m_videoHeight = height;

    // Update constants
    m_constants.videoResolution[0] = static_cast<float>(width);
    m_constants.videoResolution[1] = static_cast<float>(height);

    return true;
}

bool D3D11Renderer::CreateRenderToTexture(int width, int height) {
    m_renderTexture.Reset();
    m_renderTextureRTV.Reset();
    m_stagingTexture.Reset();
    m_renderTextureWidth  = 0;
    m_renderTextureHeight = 0;

    // Render target texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_renderTexture);
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(m_renderTexture.Get(), nullptr, &m_renderTextureRTV);
    if (FAILED(hr)) return false;

    // Staging texture for CPU readback
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.BindFlags = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_stagingTexture);
    if (FAILED(hr)) return false;
    m_renderTextureWidth  = width;
    m_renderTextureHeight = height;
    return true;
}

bool D3D11Renderer::CreateDisplayTexture(int width, int height) {
    if (m_displayWidth == width && m_displayHeight == height && m_displayTexture)
        return true;

    m_displayTexture.Reset();
    m_displayRTV.Reset();
    m_displaySRV.Reset();

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    // Must be both RTV (rendered into) and SRV (sampled by ImGui)
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_displayTexture);
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(m_displayTexture.Get(), nullptr, &m_displayRTV);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = m_device->CreateShaderResourceView(m_displayTexture.Get(), &srvDesc, &m_displaySRV);
    if (FAILED(hr)) return false;

    m_displayWidth = width;
    m_displayHeight = height;
    return true;
}

void D3D11Renderer::RenderToDisplay() {
    const int renderW = (m_videoWidth  > 0) ? m_videoWidth  : m_generativeWidth;
    const int renderH = (m_videoHeight > 0) ? m_videoHeight : m_generativeHeight;
    if (renderW <= 0 || renderH <= 0) return;
    if (!CreateDisplayTexture(renderW, renderH)) return;

    const bool doComposite = (m_videoBlendMode > 0) && (m_videoWidth > 0) && m_compositorPS;

    auto setViewport = [&](int w, int h) {
        D3D11_VIEWPORT vp = {};
        vp.Width    = static_cast<float>(w);
        vp.Height   = static_cast<float>(h);
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);
    };

    if (doComposite) {
        // Pass 1 — run the active (generative) shader into the compositor src texture.
        if (!CreateCompositorSrcTexture(renderW, renderH)) return;

        float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_context->ClearRenderTargetView(m_compositorSrcRTV.Get(), clearColor);
        m_context->OMSetRenderTargets(1, m_compositorSrcRTV.GetAddressOf(), nullptr);
        setViewport(renderW, renderH);
        m_context->PSSetShader(m_activePS.Get(), nullptr, 0);
        m_context->Draw(3, 0);

        // Pass 2 — compositor reads video (t0) + generative result (t2), blends to display.
        m_context->ClearRenderTargetView(m_displayRTV.Get(), clearColor);
        m_context->OMSetRenderTargets(1, m_displayRTV.GetAddressOf(), nullptr);
        setViewport(renderW, renderH);
        m_context->PSSetShader(m_compositorPS.Get(), nullptr, 0);
        m_context->PSSetShaderResources(2, 1, m_compositorSrcSRV.GetAddressOf());
        m_context->Draw(3, 0);

        // Unbind t2 to avoid D3D hazard (it's an RTV on next frame's pass 1)
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->PSSetShaderResources(2, 1, &nullSRV);

        // Restore active shader for subsequent calls
        m_context->PSSetShader(m_activePS.Get(), nullptr, 0);
    } else {
        float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_context->ClearRenderTargetView(m_displayRTV.Get(), clearColor);
        m_context->OMSetRenderTargets(1, m_displayRTV.GetAddressOf(), nullptr);
        setViewport(renderW, renderH);
        m_context->Draw(3, 0);
    }

    // Restore backbuffer as RT so ImGui can render into it
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);
    D3D11_VIEWPORT mainVP = {};
    mainVP.Width    = static_cast<float>(m_width);
    mainVP.Height   = static_cast<float>(m_height);
    mainVP.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &mainVP);
}

void D3D11Renderer::BlitDisplayTo(ID3D11RenderTargetView* rtv, int width, int height) {
    if (!m_displaySRV || !rtv) return;

    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->ClearRenderTargetView(rtv, clearColor);
    m_context->OMSetRenderTargets(1, &rtv, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(width);
    vp.Height   = static_cast<float>(height);
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    // Passthrough — display texture is already shader-processed
    m_context->PSSetShader(m_passthroughPS.Get(), nullptr, 0);
    m_context->PSSetShaderResources(0, 1, m_displaySRV.GetAddressOf());
    m_context->Draw(3, 0);

    // Restore main backbuffer RT, viewport, active PS, and video SRV
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);
    D3D11_VIEWPORT mainVP = {};
    mainVP.Width    = static_cast<float>(m_width);
    mainVP.Height   = static_cast<float>(m_height);
    mainVP.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &mainVP);
    m_context->PSSetShader(m_activePS.Get(), nullptr, 0);
    if (m_videoSRV)
        m_context->PSSetShaderResources(0, 1, m_videoSRV.GetAddressOf());
}

bool D3D11Renderer::UploadVideoFrame(const VideoFrame& frame) {
    if (!CreateVideoTexture(frame.width, frame.height)) {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(m_videoTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;

    // Copy row by row (handle pitch mismatch)
    const uint8_t* src = frame.data[0].data();
    uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
    int srcPitch = frame.linesize[0];
    int dstPitch = static_cast<int>(mapped.RowPitch);
    int rowBytes = frame.width * 4;  // RGBA

    for (int y = 0; y < frame.height; ++y) {
        memcpy(dst + y * dstPitch, src + y * srcPitch, rowBytes);
    }

    m_context->Unmap(m_videoTexture.Get(), 0);
    return true;
}

// FNV-1a 64-bit hash — used to key the shader bytecode cache.
static uint64_t Fnv1a64(const char* data, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint8_t>(data[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

// Returns (and lazily creates) the shader_cache/ dir next to the exe.
static std::filesystem::path GetShaderCacheDir() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    auto dir = std::filesystem::path(exePath).parent_path() / "shader_cache";
    std::filesystem::create_directories(dir);
    return dir;
}

bool D3D11Renderer::CompilePixelShader(const std::string& hlslSource, ComPtr<ID3D11PixelShader>& outShader, std::string& outError) {
    // --- Bytecode cache check ---
    // Key = FNV-1a hash of the full source (includes preamble defines).
    // Cache files are DXBC blobs: portable across GPUs (driver JIT-compiles them).
    const uint64_t hash = Fnv1a64(hlslSource.c_str(), hlslSource.size());
    char hashStr[17];
    snprintf(hashStr, sizeof(hashStr), "%016llx", static_cast<unsigned long long>(hash));
    const auto cachePath = GetShaderCacheDir() / (std::string(hashStr) + ".blob");

    if (std::filesystem::exists(cachePath)) {
        std::ifstream cacheFile(cachePath, std::ios::binary | std::ios::ate);
        if (cacheFile) {
            const auto blobSize = static_cast<size_t>(cacheFile.tellg());
            cacheFile.seekg(0);
            std::vector<char> blobData(blobSize);
            cacheFile.read(blobData.data(), static_cast<std::streamsize>(blobSize));
            if (cacheFile) {
                HRESULT hr = m_device->CreatePixelShader(blobData.data(), blobSize, nullptr, &outShader);
                if (SUCCEEDED(hr)) return true;
                // Blob corrupt or stale — fall through to full recompile.
            }
        }
    }

    // --- Full compile ---
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(
        hlslSource.c_str(),
        hlslSource.size(),
        "PixelShader",
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &psBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            outError = std::string(
                static_cast<const char*>(errorBlob->GetBufferPointer()),
                errorBlob->GetBufferSize()
            );
        } else {
            outError = "Unknown compilation error";
        }
        return false;
    }

    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        &outShader
    );

    if (FAILED(hr)) {
        outError = "Failed to create pixel shader object";
        return false;
    }

    // Write blob to cache so subsequent startups skip D3DCompile.
    {
        std::ofstream cacheOut(cachePath, std::ios::binary);
        cacheOut.write(static_cast<const char*>(psBlob->GetBufferPointer()),
                       static_cast<std::streamsize>(psBlob->GetBufferSize()));
    }

    outError.clear();
    return true;
}

void D3D11Renderer::SetActivePixelShader(ID3D11PixelShader* shader) {
    m_activePS = shader ? shader : m_passthroughPS.Get();
}

void D3D11Renderer::BeginFrame() {
    // Update constant buffer
    m_constants.resolution[0] = static_cast<float>(m_width);
    m_constants.resolution[1] = static_cast<float>(m_height);

    // When no video is loaded, mirror the generative resolution into videoResolution
    // so shaders that reference videoResolution get a sensible value.
    if (m_videoWidth == 0) {
        m_constants.videoResolution[0] = static_cast<float>(m_generativeWidth);
        m_constants.videoResolution[1] = static_cast<float>(m_generativeHeight);
    }

    // Pass blend params through the padding fields so the compositor shader can read them.
    // These don't affect regular pixel shaders (padding fields are unused by convention).
    m_constants.padding1    = m_videoBlendFactor;
    m_constants.padding2[0] = static_cast<float>(m_videoBlendMode);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &m_constants, sizeof(m_constants));
        m_context->Unmap(m_constantBuffer.Get(), 0);
    }

    // Clear render target
    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);

    // Set render target
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);

    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    // Set pipeline state
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    UINT stride = sizeof(float) * 4;
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);

    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_activePS.Get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetShaderResources(0, 1, m_videoSRV.GetAddressOf());
    m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

    // Bind noise texture + wrap sampler globally (shaders that use them declare t1/s1)
    if (m_noiseSRV)
        m_context->PSSetShaderResources(1, 1, m_noiseSRV.GetAddressOf());
    if (m_wrapSampler)
        m_context->PSSetSamplers(1, 1, m_wrapSampler.GetAddressOf());

    // Bind audio cbuffer (b1) and spectrum texture (t3) globally.
    // Always bound — shaders that don't use them simply ignore these slots.
    if (m_audioConstantBuffer)
        m_context->PSSetConstantBuffers(1, 1, m_audioConstantBuffer.GetAddressOf());
    if (m_spectrumSRV)
        m_context->PSSetShaderResources(3, 1, m_spectrumSRV.GetAddressOf());

    m_context->RSSetState(m_rasterizerState.Get());
    m_context->OMSetBlendState(m_blendState.Get(), nullptr, 0xFFFFFFFF);
}

void D3D11Renderer::EndFrame() {
    // Draw fullscreen triangle
    m_context->Draw(3, 0);
}

void D3D11Renderer::Present(bool vsync) {
    m_swapChain->Present(vsync ? 1 : 0, 0);
}

bool D3D11Renderer::RenderToTexture() {
    const int w = (m_videoWidth  > 0) ? m_videoWidth  : m_generativeWidth;
    const int h = (m_videoHeight > 0) ? m_videoHeight : m_generativeHeight;

    if (!m_renderTexture || !m_renderTextureRTV ||
        m_renderTextureWidth != w || m_renderTextureHeight != h) {
        if (!CreateRenderToTexture(w, h)) {
            return false;
        }
    }

    const bool doComposite = (m_videoBlendMode > 0) && (m_videoWidth > 0) && m_compositorPS;

    auto setViewport = [&](int vw, int vh) {
        D3D11_VIEWPORT vp = {};
        vp.Width    = static_cast<float>(vw);
        vp.Height   = static_cast<float>(vh);
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);
    };

    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    if (doComposite) {
        // Pass 1 — active (generative) shader into compositor src texture
        if (!CreateCompositorSrcTexture(w, h)) return false;
        m_context->ClearRenderTargetView(m_compositorSrcRTV.Get(), clearColor);
        m_context->OMSetRenderTargets(1, m_compositorSrcRTV.GetAddressOf(), nullptr);
        setViewport(w, h);
        m_context->PSSetShader(m_activePS.Get(), nullptr, 0);
        m_context->Draw(3, 0);

        // Pass 2 — compositor to render texture
        m_context->ClearRenderTargetView(m_renderTextureRTV.Get(), clearColor);
        m_context->OMSetRenderTargets(1, m_renderTextureRTV.GetAddressOf(), nullptr);
        setViewport(w, h);
        m_context->PSSetShader(m_compositorPS.Get(), nullptr, 0);
        m_context->PSSetShaderResources(2, 1, m_compositorSrcSRV.GetAddressOf());
        m_context->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->PSSetShaderResources(2, 1, &nullSRV);
        m_context->PSSetShader(m_activePS.Get(), nullptr, 0);
    } else {
        m_context->ClearRenderTargetView(m_renderTextureRTV.Get(), clearColor);
        m_context->OMSetRenderTargets(1, m_renderTextureRTV.GetAddressOf(), nullptr);
        setViewport(w, h);
        m_context->Draw(3, 0);
    }

    return true;
}

bool D3D11Renderer::CopyRenderTargetToStaging(std::vector<uint8_t>& outData, int& outWidth, int& outHeight) {
    if (!m_renderTexture || !m_stagingTexture) {
        return false;
    }

    // Copy to staging texture
    m_context->CopyResource(m_stagingTexture.Get(), m_renderTexture.Get());

    // Use the tracked render texture dimensions — these always match the staging
    // texture allocation, unlike recomputing from m_videoWidth/m_generativeWidth
    // which can diverge if the texture was created at a different resolution.
    const int renderW = m_renderTextureWidth;
    const int renderH = m_renderTextureHeight;

    // Map and copy data
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    outWidth  = renderW;
    outHeight = renderH;
    const int rowBytes = renderW * 4;
    // swscale's 4:2:0 chroma path reads source rows in pairs, so it can read one
    // row past the last valid row. For dimensions where width*height*4 is exactly
    // divisible by 4096 (e.g. 1920×1080), the allocation lands page-aligned with
    // zero slack and that extra-row read hits an unmapped page → access violation.
    // Two rows of tail padding guarantees the read lands in committed memory.
    outData.resize(static_cast<size_t>(renderW) * renderH * 4 + rowBytes * 2, 0);

    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    uint8_t* dst = outData.data();

    for (int y = 0; y < renderH; ++y) {
        memcpy(dst + y * rowBytes, src + y * mapped.RowPitch, rowBytes);
    }

    m_context->Unmap(m_stagingTexture.Get(), 0);
    return true;
}

void D3D11Renderer::SetShaderTime(float time) {
    m_constants.time = time;
}

void D3D11Renderer::SetShaderResolution(float width, float height) {
    m_constants.resolution[0] = width;
    m_constants.resolution[1] = height;
}

void D3D11Renderer::SetCustomUniforms(const float* data, size_t floatCount) {
    size_t copyCount = std::min(floatCount, size_t(16));
    memcpy(m_constants.custom, data, copyCount * sizeof(float));
}

// ---------------------------------------------------------------------------
// Noise texture generation (CPU-side Perlin + Voronoi, stored in R+G channels)
// ---------------------------------------------------------------------------

namespace {

// Improved smoothstep (Ken Perlin's quintic)
static float quintic(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Pseudo-random gradient dot product for Perlin noise
static float gradDot(int ix, int iy, float fx, float fy) {
    uint32_t h = static_cast<uint32_t>(ix) * 1619u + static_cast<uint32_t>(iy) * 31337u;
    h ^= h >> 13;
    h *= 0xbf58476du;
    h ^= h >> 31;
    switch (h & 3) {
        case 0: return  fx + fy;
        case 1: return -fx + fy;
        case 2: return  fx - fy;
        case 3: return -fx - fy;
    }
    return 0.0f;
}

static float perlinNoise(float x, float y) {
    int xi = static_cast<int>(floorf(x));
    int yi = static_cast<int>(floorf(y));
    float xf = x - static_cast<float>(xi);
    float yf = y - static_cast<float>(yi);
    float u  = quintic(xf);
    float v  = quintic(yf);

    float n00 = gradDot(xi,     yi,     xf,     yf    );
    float n10 = gradDot(xi + 1, yi,     xf - 1, yf    );
    float n01 = gradDot(xi,     yi + 1, xf,     yf - 1);
    float n11 = gradDot(xi + 1, yi + 1, xf - 1, yf - 1);

    float lx0 = n00 + u * (n10 - n00);
    float lx1 = n01 + u * (n11 - n01);
    return (lx0 + v * (lx1 - lx0)) * 0.5f + 0.5f;  // map [-1,1] → [0,1]
}

// Hash giving a float in [0,1] for a cell feature point coordinate
static float cellHash(uint32_t ix, uint32_t iy, uint32_t seed) {
    uint32_t h = ix * 1619u + iy * 31337u + seed * 6271u;
    h ^= h >> 13;
    h *= 0xbf58476du;
    h ^= h >> 31;
    return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

static float voronoiNoise(float x, float y) {
    int xi = static_cast<int>(floorf(x));
    int yi = static_cast<int>(floorf(y));
    float minDist = 1e10f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int cx = xi + dx;
            int cy = yi + dy;
            float px = static_cast<float>(cx) + cellHash(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy), 0u);
            float py = static_cast<float>(cy) + cellHash(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy), 1u);
            float d  = sqrtf((x - px) * (x - px) + (y - py) * (y - py));
            if (d < minDist) minDist = d;
        }
    }
    return (std::min)(minDist, 1.0f);
}

} // anonymous namespace

void D3D11Renderer::SetGenerativeResolution(int width, int height) {
    m_generativeWidth  = (std::max)(width,  1);
    m_generativeHeight = (std::max)(height, 1);
    // Force display texture recreation on next RenderToDisplay call
    m_displayWidth  = 0;
    m_displayHeight = 0;
}

bool D3D11Renderer::UpdateNoiseTexture(float scale, int texSize) {
    if (!m_device) return false;
    texSize = (std::max)(texSize, 64);

    // Generate Perlin in R, Voronoi in G (inverted so cells are bright centers)
    std::vector<uint8_t> pixels(static_cast<size_t>(texSize) * texSize * 4);

    for (int y = 0; y < texSize; ++y) {
        for (int x = 0; x < texSize; ++x) {
            float nx = (x + 0.5f) / static_cast<float>(texSize) * scale;
            float ny = (y + 0.5f) / static_cast<float>(texSize) * scale;

            float p = perlinNoise(nx, ny);
            float v = 1.0f - voronoiNoise(nx, ny);  // invert: bright centres

            p = (std::max)(0.0f, (std::min)(1.0f, p));
            v = (std::max)(0.0f, (std::min)(1.0f, v));

            uint8_t* px       = &pixels[(static_cast<size_t>(y) * texSize + x) * 4];
            px[0] = static_cast<uint8_t>(p * 255.0f);  // R = Perlin
            px[1] = static_cast<uint8_t>(v * 255.0f);  // G = Voronoi
            px[2] = 0;
            px[3] = 255;
        }
    }

    m_noiseTexture.Reset();
    m_noiseSRV.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = static_cast<UINT>(texSize);
    desc.Height           = static_cast<UINT>(texSize);
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem     = pixels.data();
    initData.SysMemPitch = static_cast<UINT>(texSize * 4);

    HRESULT hr = m_device->CreateTexture2D(&desc, &initData, &m_noiseTexture);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = desc.Format;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels       = 1;

    hr = m_device->CreateShaderResourceView(m_noiseTexture.Get(), &srvDesc, &m_noiseSRV);
    return SUCCEEDED(hr);
}

void D3D11Renderer::SetAudioData(const AudioData* data) {
    if (data) {
        m_audioConstants.rms              = data->rms;
        m_audioConstants.bass             = data->bass;
        m_audioConstants.mid              = data->mid;
        m_audioConstants.high             = data->high;
        m_audioConstants.beat             = data->beat;
        m_audioConstants.spectralCentroid = data->spectralCentroid;
    } else {
        m_audioConstants = {};
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (m_audioConstantBuffer &&
        SUCCEEDED(m_context->Map(m_audioConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &m_audioConstants, sizeof(m_audioConstants));
        m_context->Unmap(m_audioConstantBuffer.Get(), 0);
    }

    if (m_spectrumTexture &&
        SUCCEEDED(m_context->Map(m_spectrumTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        if (data) {
            memcpy(mapped.pData, data->spectrum, AudioData::kSpectrumBins * sizeof(float));
        } else {
            memset(mapped.pData, 0, AudioData::kSpectrumBins * sizeof(float));
        }
        m_context->Unmap(m_spectrumTexture.Get(), 0);
    }
}

} // namespace SP
