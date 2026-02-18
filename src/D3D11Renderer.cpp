#include "D3D11Renderer.h"
#include <stdexcept>

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

    // Create blend state (no blending, opaque)
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
    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreatePassthroughShader() {
    std::string error;
    return CompilePixelShader(g_passthroughShaderSource, m_passthroughPS, error);
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
    return SUCCEEDED(hr);
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
    if (m_videoWidth <= 0 || m_videoHeight <= 0) return;
    if (!CreateDisplayTexture(m_videoWidth, m_videoHeight)) return;

    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->ClearRenderTargetView(m_displayRTV.Get(), clearColor);
    m_context->OMSetRenderTargets(1, m_displayRTV.GetAddressOf(), nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(m_videoWidth);
    vp.Height = static_cast<float>(m_videoHeight);
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    m_context->Draw(3, 0);

    // Unbind display RTV so ImGui can use it as SRV; restore backbuffer as RT
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);

    D3D11_VIEWPORT mainVP = {};
    mainVP.Width = static_cast<float>(m_width);
    mainVP.Height = static_cast<float>(m_height);
    mainVP.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &mainVP);
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

bool D3D11Renderer::CompilePixelShader(const std::string& hlslSource, ComPtr<ID3D11PixelShader>& outShader, std::string& outError) {
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
    if (!m_renderTexture || !m_renderTextureRTV) {
        if (!CreateRenderToTexture(m_videoWidth, m_videoHeight)) {
            return false;
        }
    }

    // Render to our texture instead of swapchain
    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->ClearRenderTargetView(m_renderTextureRTV.Get(), clearColor);
    m_context->OMSetRenderTargets(1, m_renderTextureRTV.GetAddressOf(), nullptr);

    // Set viewport to video resolution
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_videoWidth);
    viewport.Height = static_cast<float>(m_videoHeight);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    // Draw
    m_context->Draw(3, 0);

    return true;
}

bool D3D11Renderer::CopyRenderTargetToStaging(std::vector<uint8_t>& outData, int& outWidth, int& outHeight) {
    if (!m_renderTexture || !m_stagingTexture) {
        return false;
    }

    // Copy to staging texture
    m_context->CopyResource(m_stagingTexture.Get(), m_renderTexture.Get());

    // Map and copy data
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    outWidth = m_videoWidth;
    outHeight = m_videoHeight;
    outData.resize(m_videoWidth * m_videoHeight * 4);

    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    uint8_t* dst = outData.data();
    int rowBytes = m_videoWidth * 4;

    for (int y = 0; y < m_videoHeight; ++y) {
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

} // namespace SP
