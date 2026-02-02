#pragma once

#include "Common.h"

namespace SP {

class D3D11Renderer {
public:
    D3D11Renderer();
    ~D3D11Renderer();

    // Non-copyable
    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    // Initialization
    bool Initialize(HWND hwnd, int width, int height);
    void Shutdown();
    bool IsInitialized() const { return m_device != nullptr; }

    // Resize handling
    bool Resize(int width, int height);

    // Frame operations
    void BeginFrame();
    void EndFrame();
    void Present(bool vsync = true);

    // Video frame upload
    bool UploadVideoFrame(const VideoFrame& frame);
    
    // Shader management
    bool CompilePixelShader(const std::string& hlslSource, ComPtr<ID3D11PixelShader>& outShader, std::string& outError);
    void SetActivePixelShader(ID3D11PixelShader* shader);
    ID3D11PixelShader* GetPassthroughShader() const { return m_passthroughPS.Get(); }

    // Render to texture (for recording)
    bool RenderToTexture();
    bool CopyRenderTargetToStaging(std::vector<uint8_t>& outData, int& outWidth, int& outHeight);

    // Shader uniforms
    void SetShaderTime(float time);
    void SetShaderResolution(float width, float height);
    void SetCustomUniforms(const float* data, size_t floatCount);

    // Accessors
    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    ID3D11RenderTargetView* GetRenderTargetView() const { return m_renderTargetView.Get(); }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    bool CreateDeviceAndSwapChain(HWND hwnd, int width, int height);
    bool CreateRenderTarget();
    bool CreateVideoTexture(int width, int height);
    bool CreateRenderToTexture(int width, int height);
    bool CreateShaderResources();
    bool CreatePassthroughShader();
    void ReleaseRenderTarget();

    // Device and swap chain
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    // Video texture
    ComPtr<ID3D11Texture2D> m_videoTexture;
    ComPtr<ID3D11ShaderResourceView> m_videoSRV;
    int m_videoWidth = 0;
    int m_videoHeight = 0;

    // Render-to-texture for recording
    ComPtr<ID3D11Texture2D> m_renderTexture;
    ComPtr<ID3D11RenderTargetView> m_renderTextureRTV;
    ComPtr<ID3D11Texture2D> m_stagingTexture;

    // Shaders and pipeline state
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_passthroughPS;
    ComPtr<ID3D11PixelShader> m_activePS;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11BlendState> m_blendState;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;

    // Constant buffer data
    struct alignas(16) ShaderConstants {
        float time;
        float padding1;
        float resolution[2];
        float videoResolution[2];
        float padding2[2];
        float custom[16];  // Custom uniforms
    };
    ShaderConstants m_constants = {};

    int m_width = 0;
    int m_height = 0;
    HWND m_hwnd = nullptr;
};

} // namespace SP
