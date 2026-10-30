#include "gfx/texture.hpp"
#include "base/log.hpp"
#include "gfx/handles.hpp"

bool GpuTexture::create(WGPUDevice device, const Desc& desc)
{
    reset();
    WGPUTextureDescriptor td{};
    td.label         = sv(desc.label);
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = { desc.width, desc.height, 1 };
    td.format        = desc.format;
    td.usage         = desc.usage;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    m_texture.reset(wgpuDeviceCreateTexture(device, &td));
    if (m_texture) m_view.reset(wgpuTextureCreateView(m_texture, nullptr));
    if (!m_view) {   // out of memory, or too big: what it was for shows nothing
        reset();
        Log::error("gpu", "%s: no %ux%u texture", desc.label, desc.width, desc.height);
        return false;
    }
    m_width  = desc.width;
    m_height = desc.height;
    return true;
}

void GpuTexture::reset()
{
    m_view.reset();
    m_texture.reset();
    m_width = m_height = 0;
}
