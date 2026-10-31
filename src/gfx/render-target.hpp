#pragma once
// A texture to render into and show with ImGui::Image, sized to what's shown. Re-creating it on
// resize is safe: ImGui's wgpu backend rebuilds image bind groups every frame.

#include "gfx/texture.hpp"

class RenderTarget {
public:
    void init(WGPUDevice device, WGPUTextureFormat format);   // learns the device's largest texture
    void shutdown() { m_texture.reset(); m_device = nullptr; }

    // (Re)creates the texture if its size differs from w x h (each side clamped to what the
    // device takes). Returns false on failure.
    bool ensure(uint32_t w, uint32_t h);

    WGPUTextureView view()   const { return m_texture.view(); }
    uint32_t        width()  const { return m_texture.width(); }
    uint32_t        height() const { return m_texture.height(); }

private:
    WGPUDevice        m_device = nullptr;
    WGPUTextureFormat m_format = WGPUTextureFormat_RGBA8Unorm;
    uint32_t          m_max = kDefaultMax;
    GpuTexture        m_texture;

    static constexpr uint32_t kDefaultMax = 8192;   // WebGPU's default maxTextureDimension2D: if the device won't say
};
