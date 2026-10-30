#include "gfx/render-target.hpp"

#include <algorithm>

bool RenderTarget::ensure(uint32_t w, uint32_t h)
{
    w = std::clamp<uint32_t>(w, 1, kMax);
    h = std::clamp<uint32_t>(h, 1, kMax);
    if (m_texture && w == m_texture.width() && h == m_texture.height()) return true;
    // Release, not Destroy (inside create): commands already recorded this frame may still reference it.
    return m_texture.create(m_device, { w, h, m_format, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding, "Render Target" });
}
