#include "handles.hpp"
#include "gfx/error-scope.hpp"

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
    GpuErrorScope scope(device);
    WGPUBufferDescriptor bd{};
    bd.label = sv(label);
    bd.size  = size;
    bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    buffer.reset(wgpuDeviceCreateBuffer(device, &bd));

    WGPUBindGroupEntry e{};
    e.binding = 0;
    e.buffer  = buffer;
    e.size    = size;
    WGPUBindGroupDescriptor gd{};
    gd.label      = sv(label);
    gd.layout     = layout;
    gd.entryCount = 1;
    gd.entries    = &e;
    group.reset(wgpuDeviceCreateBindGroup(device, &gd));
    if (scope.failed(label) || !group) {
        release();
        return false;
    }
    return true;
}
