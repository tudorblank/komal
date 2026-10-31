#include "gfx/render-target.hpp"

#include <algorithm>

void RenderTarget::init(WGPUDevice device, WGPUTextureFormat format)
{
    m_device = device;
    m_format = format;
    WGPULimits limits{};
    m_max = wgpuDeviceGetLimits(device, &limits) == WGPUStatus_Success && limits.maxTextureDimension2D > 0 ? limits.maxTextureDimension2D
                                                                                                         : kDefaultMax;
}

bool RenderTarget::ensure(uint32_t w, uint32_t h)
{
    w = std::clamp<uint32_t>(w, 1, m_max);
    h = std::clamp<uint32_t>(h, 1, m_max);
    if (m_texture && w == m_texture.width() && h == m_texture.height()) return true;
    // Release, not Destroy (inside create): commands already recorded this frame may still reference it.
    return m_texture.create(m_device, { w, h, m_format, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding, "Render Target" });
}
