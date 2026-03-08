#pragma once

#include <d3d11.h>
#include <string>

namespace SP {

// Spout2 sender — shares the processed display texture with any Spout-aware
// receiver on the same machine: Resolume, MadMapper, OBS (SpoutPlugin),
// SpoutCam (virtual webcam), etc.
// Uses pImpl so SpoutDX headers stay out of Application.h.
class SpoutOutput {
public:
    SpoutOutput() = default;
    ~SpoutOutput();

    SpoutOutput(const SpoutOutput&) = delete;
    SpoutOutput& operator=(const SpoutOutput&) = delete;

    bool Initialize(ID3D11Device* device);
    void Shutdown();

    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled && m_initialized; }

    // Rename the Spout sender. Safe to call before or after Initialize.
    void SetSenderName(const std::string& name);
    const std::string& GetSenderName() const { return m_senderName; }

    // Call after D3D11Renderer::RenderToDisplay(). Returns false on failure.
    bool SendFrame(ID3D11Texture2D* texture);

private:
    struct Impl;
    Impl*       m_impl        = nullptr;
    bool        m_enabled     = false;
    bool        m_initialized = false;
    std::string m_senderName  = "ShaderPlayer";
};

} // namespace SP
