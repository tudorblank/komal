#include "render-target.hpp"
#include <algorithm>

void RenderTarget::init(WGPUDevice device, WGPUTextureFormat format)
{
    m_device = device;
    m_format = format;
}

void RenderTarget::release()
{
    // Release, not Destroy: commands already recorded this frame may still reference it.
    if (m_view) { wgpuTextureViewRelease(m_view); m_view = nullptr; }
    if (m_tex)  { wgpuTextureRelease(m_tex);      m_tex  = nullptr; }
    m_w = m_h = 0;
}

void RenderTarget::shutdown() { release(); m_device = nullptr; }

bool RenderTarget::ensure(uint32_t w, uint32_t h)
{
    w = std::clamp<uint32_t>(w, 1, kMax);
    h = std::clamp<uint32_t>(h, 1, kMax);
    if (m_tex && w == m_w && h == m_h)
        return true;

    release();

    WGPUTextureDescriptor td{};
    td.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = { w, h, 1 };
    td.format        = m_format;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    m_tex = wgpuDeviceCreateTexture(m_device, &td);
    if (!m_tex) return false;

    m_view = wgpuTextureCreateView(m_tex, nullptr);
    m_w = w;
    m_h = h;
    return m_view != nullptr;
}
