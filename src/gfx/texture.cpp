#include "gfx/texture.hpp"
#include "gfx/error-scope.hpp"
#include "gfx/handles.hpp"

#include <string>

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
    GpuErrorScope scope(device);
    m_texture.reset(wgpuDeviceCreateTexture(device, &td));
    if (m_texture) m_view.reset(wgpuTextureCreateView(m_texture, nullptr));
    const std::string what = std::string(desc.label) + ": a " + std::to_string(desc.width) + "x" + std::to_string(desc.height) + " texture";
    if (scope.failed(what.c_str()) || !m_view) {   // out of memory, or too big: what it was for shows nothing
        reset();
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
