#include "gfx/render-pass.hpp"
#include "gfx/handles.hpp"

#include <vector>

GpuRenderPass::GpuRenderPass(WGPUCommandEncoder encoder, WGPUTextureView target, Load load, const char* label, const float* clear)
{
    WGPURenderPassColorAttachment ca{};
    ca.view       = target;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp     = load == Load::Clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    ca.storeOp    = WGPUStoreOp_Store;
    if (clear) ca.clearValue = { clear[0], clear[1], clear[2], clear[3] };
    WGPURenderPassDescriptor rp{};
    rp.label                = sv(label);
    rp.colorAttachmentCount = 1;
    rp.colorAttachments     = &ca;
    m_pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp);
}

GpuRenderPass::~GpuRenderPass()
{
    wgpuRenderPassEncoderEnd(m_pass);
    wgpuRenderPassEncoderRelease(m_pass);
}

void GpuRenderPass::confine(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const
{
    wgpuRenderPassEncoderSetViewport(m_pass, float(x), float(y), float(width), float(height), 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(m_pass, x, y, width, height);
}

GpuObject<WGPUBindGroup> textureBindGroup(WGPUDevice device, WGPUBindGroupLayout layout, std::span<const WGPUTextureView> views,
                                          const char* label)
{
    std::vector<WGPUBindGroupEntry> e(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
        e[i].binding     = uint32_t(i);
        e[i].textureView = views[i];
    }
    WGPUBindGroupDescriptor gd{};
    gd.label      = sv(label);
    gd.layout     = layout;
    gd.entryCount = e.size();
    gd.entries    = e.data();
    return GpuObject<WGPUBindGroup>{ wgpuDeviceCreateBindGroup(device, &gd) };
}
