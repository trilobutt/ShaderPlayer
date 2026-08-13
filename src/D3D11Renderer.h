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

    // Initialization. The renderer owns no window and no swap chain: every surface it
    // draws to is either an offscreen texture or an RTV handed in by a caller that owns
    // its own swap chain (ViewportWidget, VideoOutputWindow).
    bool Initialize(int width, int height);
    void Shutdown();
    bool IsInitialized() const { return m_device != nullptr; }

    // Tracks the viewport's client size for the `resolution` cbuffer field (and the
    // post-blit viewport restores in BlitDisplayTo/BlitDisplayToRect). Call on every
    // ViewportWidget::resizeEvent. Deliberately does NOT touch m_displayTexture: that
    // texture is sized from video/generative content resolution (see RenderToDisplay),
    // not from the window, and is letterboxed into whatever viewport exists — resizing
    // it here would only be overwritten by the next RenderToDisplay call and would
    // thrash a GPU resource on every pixel of a drag for no visible effect. No swap
    // chain, backbuffer or HWND is created or touched here.
    void Resize(int width, int height);

    // Frame operations. BeginFrame sets the whole pixel-shader pipeline state
    // (m_activePS, SRVs, samplers, cbuffers) that RenderToDisplay and the recording
    // path both rely on, so it must be called before either of them each frame.
    void BeginFrame();

    // Video frame upload
    bool UploadVideoFrame(const VideoFrame& frame);
    
    // Shader management
    bool CompilePixelShader(const std::string& hlslSource, ComPtr<ID3D11PixelShader>& outShader, std::string& outError);
    void SetActivePixelShader(ID3D11PixelShader* shader);
    ID3D11PixelShader* GetPassthroughShader() const { return m_passthroughPS.Get(); }

    // Render to texture (for recording)
    bool RenderToTexture();
    bool CopyRenderTargetToStaging(std::vector<uint8_t>& outData, int& outWidth, int& outHeight);

    // Render to the display texture — the shader-processed frame every consumer reads
    // (the viewport, the detached output window, Spout).
    void RenderToDisplay();
    ID3D11ShaderResourceView* GetDisplaySRV()    const { return m_displaySRV.Get(); }
    ID3D11Texture2D*          GetDisplayTexture() const { return m_displayTexture.Get(); }
    int GetDisplayWidth()  const { return m_displayWidth; }
    int GetDisplayHeight() const { return m_displayHeight; }

    // Blit the already-processed display texture into an external RTV (a caller's swap
    // chain). The caller's RTV is left bound; the active PS and the video SRV are
    // restored so the next RenderToDisplay finds the pipeline as BeginFrame left it.
    void BlitDisplayTo(ID3D11RenderTargetView* rtv, int width, int height);

    // As BlitDisplayTo, but clears the full RTV to clearColor (RGBA, 0..1) and draws
    // into the sub-rectangle [rectX, rectY, rectWidth, rectHeight] rather than the
    // whole surface. Used for letterboxed viewports (Qt's ViewportWidget) where the
    // draw rectangle does not fill the target surface. Restores the same state
    // BlitDisplayTo does.
    void BlitDisplayToRect(ID3D11RenderTargetView* rtv, int rectX, int rectY,
                            int rectWidth, int rectHeight, const float clearColor[4]);

    // Shader uniforms
    void SetShaderTime(float time);
    void SetShaderResolution(float width, float height);
    void SetCustomUniforms(const float* data, size_t floatCount);

    // Noise texture — generates Perlin (R) + Voronoi (G) into a tiling texture
    // bound globally as t1 / s1 for all pixel shaders.
    bool UpdateNoiseTexture(float scale, int texSize);

    // The two halves of UpdateNoiseTexture, split so the CPU half can run off the GUI
    // thread. GenerateNoisePixels touches no device state and is safe to call from any
    // thread before the device exists at all; UploadNoisePixels is the D3D half and must
    // run on the thread that owns the renderer. `pixels` must hold texSize*texSize*4
    // bytes, which is what GenerateNoisePixels returns for the same texSize.
    static std::vector<uint8_t> GenerateNoisePixels(float scale, int texSize);
    bool UploadNoisePixels(const std::vector<uint8_t>& pixels, int texSize);

    // texSize as GenerateNoisePixels will clamp it. Callers that generate ahead of time
    // need the clamped value to pass back into UploadNoisePixels.
    static int ClampNoiseSize(int texSize);
    ID3D11ShaderResourceView* GetNoiseSRV() const { return m_noiseSRV.Get(); }

    // Audio data — cbuffer b1 + spectrum texture t3 (1×256 R32_FLOAT).
    // Pass nullptr to zero both (used when no audio is available).
    void SetAudioData(const AudioData* data);

    // Release video texture and reset video dimensions to zero.
    // Must be called when a video is closed so RenderToTexture/RenderToDisplay
    // fall back to generative resolution rather than stale video dimensions.
    void ReleaseVideoTexture();

    // Generative resolution — used as the render target size when no video is loaded.
    void SetGenerativeResolution(int width, int height);
    int GetGenerativeWidth()  const { return m_generativeWidth; }
    int GetGenerativeHeight() const { return m_generativeHeight; }

    // Video blend — only active when blendMode > 0 and video is also loaded.
    void SetVideoBlend(int mode, float amount) { m_videoBlendMode = mode; m_videoBlendFactor = amount; }

    // Accessors
    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    bool CreateDevice();
    bool CreateVideoTexture(int width, int height);
    bool CreateRenderToTexture(int width, int height);
    bool CreateDisplayTexture(int width, int height);
    bool CreateCompositorSrcTexture(int width, int height);
    bool CreateShaderResources();
    bool CreatePassthroughShader();
    bool CreateCompositorShader();

    // Device
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;

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

    // Display texture (the shader-processed frame every consumer blits from)
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
        float custom[32];  // Custom uniforms (float4 custom[8] in HLSL)
    };
    ShaderConstants m_constants = {};

    int m_width = 0;
    int m_height = 0;
    int   m_generativeWidth  = 1920;
    int   m_generativeHeight = 1080;
    int   m_videoBlendMode   = 0;
    float m_videoBlendFactor = 0.0f;
};

} // namespace SP
