#include "handles.hpp"

WGPUShaderModule createShaderModule(const GpuHandles& ctx, const char* wgsl, const char* label)
{
    WGPUShaderSourceWGSL src{};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code        = sv(wgsl);

    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &src.chain;
    desc.label       = sv(label);
    return wgpuDeviceCreateShaderModule(ctx.device, &desc);
}

bool GpuUniform::create(WGPUDevice device, WGPUBindGroupLayout layout, uint64_t size, const char* label)
{
    release();
    WGPUBufferDescriptor bd{};
    bd.label = sv(label);
    bd.size  = size;
    bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    buffer = wgpuDeviceCreateBuffer(device, &bd);
    if (!buffer) return false;

    WGPUBindGroupEntry e{};
    e.binding = 0;
    e.buffer  = buffer;
    e.size    = size;
    WGPUBindGroupDescriptor gd{};
    gd.label      = sv(label);
    gd.layout     = layout;
    gd.entryCount = 1;
    gd.entries    = &e;
    group = wgpuDeviceCreateBindGroup(device, &gd);
    return group != nullptr;
}

void GpuUniform::release()
{
    if (group)  { wgpuBindGroupRelease(group); group  = nullptr; }
    if (buffer) { wgpuBufferRelease(buffer);   buffer = nullptr; }
}
