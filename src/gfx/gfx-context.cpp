#include "gfx-context.h"

WGPUShaderModule createShaderModule(const GfxContext& ctx, const char* wgsl, const char* label)
{
    WGPUShaderSourceWGSL src{};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code        = sv(wgsl);

    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &src.chain;
    desc.label       = sv(label);
    return wgpuDeviceCreateShaderModule(ctx.device, &desc);
}
