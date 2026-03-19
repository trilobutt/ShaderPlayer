#include "SpoutOutput.h"

#include "SpoutDX.h"     // from Spout2 SDK (spout_lib)
#include "SpoutUtils.h"  // spoututils::DisableSpoutLog

namespace SP {

struct SpoutOutput::Impl {
    spoutDX sender;
};

SpoutOutput::~SpoutOutput() {
    Shutdown();
}

bool SpoutOutput::Initialize(ID3D11Device* device) {
    m_impl = new Impl();

    // Suppress Spout's log file — we're a WIN32 app with no console, and
    // creating a log file in %temp% on every launch is noise.
    spoututils::DisableSpoutLog();

    if (!m_impl->sender.OpenDirectX11(device)) {
        delete m_impl;
        m_impl = nullptr;
        return false;
    }

    m_impl->sender.SetSenderName(m_senderName.c_str());
    m_initialized = true;
    return true;
}

void SpoutOutput::Shutdown() {
    if (m_impl) {
        m_impl->sender.ReleaseSender();
        m_impl->sender.CloseDirectX11();
        delete m_impl;
        m_impl = nullptr;
    }
    m_initialized = false;
    m_enabled = false;
}

void SpoutOutput::SetEnabled(bool enabled) {
    m_enabled = enabled;
    // Release the Spout sender slot so it disappears from receiver lists
    // immediately when disabled, rather than appearing stale.
    if (!enabled && m_impl) {
        m_impl->sender.ReleaseSender();
        // ReleaseSender unconditionally zeros m_SenderName[0] inside spoutDX.
        // Re-apply our stored name so the next SendTexture call registers with
        // the correct name instead of falling back to the exe name.
        m_impl->sender.SetSenderName(m_senderName.c_str());
    }
}

bool SpoutOutput::IsActive() const {
    return m_initialized && m_enabled && m_impl && m_impl->sender.IsInitialized();
}

std::string SpoutOutput::GetActiveSenderName() const {
    if (!m_impl || !m_impl->sender.IsInitialized())
        return {};
    const char* name = m_impl->sender.GetSenderName();
    return name ? name : std::string{};
}

void SpoutOutput::SetSenderName(const std::string& name) {
    m_senderName = name;
    if (m_impl && m_initialized)
        m_impl->sender.SetSenderName(m_senderName.c_str());
}

bool SpoutOutput::SendFrame(ID3D11Texture2D* texture) {
    if (!m_initialized || !m_enabled || !m_impl || !texture)
        return false;
    return m_impl->sender.SendTexture(texture);
}

} // namespace SP
