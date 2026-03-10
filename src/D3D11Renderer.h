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

    // Render to display texture (for ImGui::Image preview)
    void RenderToDisplay();
    ID3D11ShaderResourceView* GetDisplaySRV()    const { return m_displaySRV.Get(); }
    ID3D11Texture2D*          GetDisplayTexture() const { return m_displayTexture.Get(); }
    int GetDisplayWidth()  const { return m_displayWidth; }
    int GetDisplayHeight() const { return m_displayHeight; }

    // Blit the already-processed display texture into an external RTV (e.g. a second
    // swap chain window).  Restores the main backbuffer RT and active PS afterwards.
    void BlitDisplayTo(ID3D11RenderTargetView* rtv, int width, int height);

    // Shader uniforms
    void SetShaderTime(float time);
    void SetShaderResolution(float width, float height);
    void SetCustomUniforms(const float* data, size_t floatCount);

    // Noise texture — generates Perlin (R) + Voronoi (G) into a tiling texture
    // bound globally as t1 / s1 for all pixel shaders.
    bool UpdateNoiseTexture(float scale, int texSize);
    ID3D11ShaderResourceView* GetNoiseSRV() const { return m_noiseSRV.Get(); }

    // Audio data — cbuffer b1 + spectrum texture t3 (1×256 R32_FLOAT).
    // Pass nullptr to zero both (used when no audio is available).
    void SetAudioData(const AudioData* data);

    // Generative resolution — used as the render target size when no video is loaded.
    void SetGenerativeResolution(int width, int height);
    int GetGenerativeWidth()  const { return m_generativeWidth; }
    int GetGenerativeHeight() const { return m_generativeHeight; }

    // Video blend — only active when blendMode > 0 and video is also loaded.
    void SetVideoBlend(int mode, float amount) { m_videoBlendMode = mode; m_videoBlendFactor = amount; }

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
    bool CreateDisplayTexture(int width, int height);
    bool CreateCompositorSrcTexture(int width, int height);
    bool CreateShaderResources();
    bool CreatePassthroughShader();
    bool CreateCompositorShader();
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
    int m_renderTextureWidth  = 0;
    int m_renderTextureHeight = 0;

    // Display texture (shader-processed frame for ImGui::Image preview)
    ComPtr<ID3D11Texture2D> m_displayTexture;
    ComPtr<ID3D11RenderTargetView> m_displayRTV;
    ComPtr<ID3D11ShaderResourceView> m_displaySRV;
    int m_displayWidth = 0;
    int m_displayHeight = 0;

    // Blend compositor shader and its intermediate source texture
    ComPtr<ID3D11PixelShader>          m_compositorPS;
    ComPtr<ID3D11Texture2D>            m_compositorSrcTexture;
    ComPtr<ID3D11RenderTargetView>     m_compositorSrcRTV;
    ComPtr<ID3D11ShaderResourceView>   m_compositorSrcSRV;
    int m_compositorSrcWidth  = 0;
    int m_compositorSrcHeight = 0;

    // Shaders and pipeline state
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_passthroughPS;
    ComPtr<ID3D11PixelShader> m_activePS;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11BlendState> m_blendState;  // opaque
    ComPtr<ID3D11RasterizerState> m_rasterizerState;

    // Noise texture (t1) + wrap sampler (s1)
    ComPtr<ID3D11Texture2D>          m_noiseTexture;
    ComPtr<ID3D11ShaderResourceView> m_noiseSRV;
    ComPtr<ID3D11SamplerState>       m_wrapSampler;

    // Audio cbuffer (b1) + spectrum texture (t3, 1×256 R32_FLOAT DYNAMIC)
    ComPtr<ID3D11Buffer>             m_audioConstantBuffer;
    ComPtr<ID3D11Texture2D>          m_spectrumTexture;
    ComPtr<ID3D11ShaderResourceView> m_spectrumSRV;

    struct alignas(16) AudioConstants {
        float rms;
        float bass;
        float mid;
        float high;
        float beat;
        float spectralCentroid;
        float padding[2];
    };
    AudioConstants m_audioConstants = {};

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
    int   m_generativeWidth  = 1920;
    int   m_generativeHeight = 1080;
    int   m_videoBlendMode   = 0;
    float m_videoBlendFactor = 0.0f;
    HWND m_hwnd = nullptr;
};

} // namespace SP
