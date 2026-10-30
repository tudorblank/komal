#pragma once
// A texture and its default view: what a page of tiles, a render target or a picture lives in.
// Owned like every GPU object (GpuObject): released when it goes, or when created again.

#include "gfx/gpu-object.hpp"

#include <cstdint>

class GpuTexture {
public:
    struct Desc {
        uint32_t          width = 0, height = 0;
        WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
        WGPUTextureUsage  usage = WGPUTextureUsage_TextureBinding;
        const char*       label = "Texture";
    };

    bool create(WGPUDevice device, const Desc& desc);   // replaces what it held; false on failure (logged)
    void reset();

    WGPUTexture     texture() const { return m_texture; }
    WGPUTextureView view()    const { return m_view; }
    uint32_t width()  const { return m_width; }
    uint32_t height() const { return m_height; }
    explicit operator bool() const { return bool(m_view); }

private:
    GpuObject<WGPUTexture>     m_texture;
    GpuObject<WGPUTextureView> m_view;   // after the texture: released first
    uint32_t m_width = 0, m_height = 0;
};
