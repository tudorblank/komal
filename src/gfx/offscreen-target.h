#pragma once
#include <webgpu/webgpu.h>
#include <cstdint>

// A texture you can render into with wgpu and then show in ImGui via ImGui::Image.
// Sized exactly to what is shown: ImGui's wgpu backend builds image bind groups per frame,
// so swapping the view on resize is free of leaks and stale bindings.
class OffscreenTarget {
public:
    static constexpr uint32_t kMax = 8192;   // default wgpu maxTextureDimension2D

    void init(WGPUDevice device, WGPUTextureFormat format);
    void shutdown();

    // (Re)creates the texture if its size differs from w x h. Returns false on failure.
    bool ensure(uint32_t w, uint32_t h);

    WGPUTextureView view()   const { return m_view; }
    uint32_t        width()  const { return m_w; }
    uint32_t        height() const { return m_h; }

private:
    void release();

    WGPUDevice        m_device = nullptr;
    WGPUTextureFormat m_format = WGPUTextureFormat_RGBA8Unorm;
    WGPUTexture       m_tex  = nullptr;
    WGPUTextureView   m_view = nullptr;
    uint32_t m_w = 0, m_h = 0;
};
