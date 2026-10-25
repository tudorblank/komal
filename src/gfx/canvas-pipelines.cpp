#include "canvas-pipelines.h"
#include "gfx/tile-atlas.h"   // ChunkInstance layout

#include <cstddef>
#include <vector>

extern const unsigned char kShaderColor[];     // generated from shaders/color.wgsl
extern const unsigned char kShaderTexture[];   // generated from shaders/texture.wgsl
extern const unsigned char kShaderPreviewSignal[];   // shaders/preview-signal.wgsl
extern const unsigned char kShaderPreviewBlit[];     // shaders/preview-blit.wgsl

namespace {

struct CameraUniform { float viewProj[16]; };
struct ColorUniform  { float model[16]; float color[4]; };

WGPUBindGroupLayout uniformLayout(const GfxContext& ctx, const char* label, WGPUShaderStage stages, uint64_t size)
{
    WGPUBindGroupLayoutEntry e{};
    e.binding               = 0;
    e.visibility            = stages;
    e.buffer.type           = WGPUBufferBindingType_Uniform;
    e.buffer.minBindingSize = size;

    WGPUBindGroupLayoutDescriptor d{};
    d.label      = sv(label);
    d.entryCount = 1;
    d.entries    = &e;
    return wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
}

// One render pipeline for the canvas target: group 0 = camera, group 1 = `itemLayout`.
WGPURenderPipeline makePipeline(const GfxContext& ctx, const char* label, WGPUShaderModule module,
                                WGPUBindGroupLayout cameraLayout, WGPUBindGroupLayout itemLayout,
                                const WGPUBlendState& blend, const WGPUVertexBufferLayout* vertexBuffer)
{
    WGPUColorTargetState target{};
    target.format    = ctx.targetFormat;
    target.blend     = &blend;
    target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs{};
    fs.module      = module;
    fs.entryPoint  = sv("fs_main");
    fs.targetCount = 1;
    fs.targets     = &target;

    WGPUBindGroupLayout groups[2] = { cameraLayout, itemLayout };
    WGPUPipelineLayoutDescriptor pld{};
    pld.label                = sv(label);
    pld.bindGroupLayoutCount = 2;
    pld.bindGroupLayouts     = groups;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(ctx.device, &pld);

    WGPURenderPipelineDescriptor rpd{};
    rpd.label               = sv(label);
    rpd.layout              = layout;
    rpd.vertex.module       = module;
    rpd.vertex.entryPoint   = sv("vs_main");
    rpd.vertex.bufferCount  = vertexBuffer ? 1 : 0;
    rpd.vertex.buffers      = vertexBuffer;
    rpd.primitive.topology  = WGPUPrimitiveTopology_TriangleStrip;   // quads from vertex_index
    rpd.primitive.frontFace = WGPUFrontFace_CCW;
    rpd.primitive.cullMode  = WGPUCullMode_None;
    rpd.multisample.count   = 1;
    rpd.multisample.mask    = 0xFFFFFFFFu;
    rpd.fragment            = &fs;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &rpd);

    wgpuPipelineLayoutRelease(layout);
    return pipeline;
}

WGPUBindGroupLayout textureLayout(const GfxContext& ctx, const char* label, int count)
{
    std::vector<WGPUBindGroupLayoutEntry> e(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        e[size_t(i)].binding               = uint32_t(i);
        e[size_t(i)].visibility            = WGPUShaderStage_Fragment;
        e[size_t(i)].texture.sampleType    = WGPUTextureSampleType_Float;
        e[size_t(i)].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    WGPUBindGroupLayoutDescriptor d{};
    d.label      = sv(label);
    d.entryCount = e.size();
    d.entries    = e.data();
    return wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
}

// A full-screen pass (a triangle from vertex_index), no camera.
WGPURenderPipeline makeFullscreenPipeline(const GfxContext& ctx, const char* label, WGPUShaderModule module,
                                          const std::vector<WGPUBindGroupLayout>& groups, const WGPUBlendState* blend)
{
    WGPUColorTargetState target{};
    target.format    = ctx.targetFormat;
    target.blend     = blend;
    target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs{};
    fs.module      = module;
    fs.entryPoint  = sv("fs_main");
    fs.targetCount = 1;
    fs.targets     = &target;

    WGPUPipelineLayoutDescriptor pld{};
    pld.label                = sv(label);
    pld.bindGroupLayoutCount = groups.size();
    pld.bindGroupLayouts     = groups.data();
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(ctx.device, &pld);

    WGPURenderPipelineDescriptor rpd{};
    rpd.label               = sv(label);
    rpd.layout              = layout;
    rpd.vertex.module       = module;
    rpd.vertex.entryPoint   = sv("vs_main");
    rpd.primitive.topology  = WGPUPrimitiveTopology_TriangleList;
    rpd.primitive.frontFace = WGPUFrontFace_CCW;
    rpd.primitive.cullMode  = WGPUCullMode_None;
    rpd.multisample.count   = 1;
    rpd.multisample.mask    = 0xFFFFFFFFu;
    rpd.fragment            = &fs;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &rpd);
    wgpuPipelineLayoutRelease(layout);
    return pipeline;
}

} // namespace

bool CanvasPipelines::create(const GfxContext& context)
{
    ctx = context;
    cameraLayout = uniformLayout(ctx, "Canvas Camera Layout", WGPUShaderStage_Vertex, sizeof(CameraUniform));

    // ---- solid rectangles: straight alpha
    colorLayout = uniformLayout(ctx, "Rect Item Layout", WGPUShaderStage_Vertex | WGPUShaderStage_Fragment, sizeof(ColorUniform));
    if (WGPUShaderModule m = createShaderModule(ctx, (const char*)kShaderColor, "color.wgsl")) {
        WGPUBlendState blend{};
        blend.color = { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha };
        blend.alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One,      WGPUBlendFactor_OneMinusSrcAlpha };
        colorPipeline = makePipeline(ctx, "Rect Pipeline", m, cameraLayout, colorLayout, blend, nullptr);
        wgpuShaderModuleRelease(m);
    }

    // ---- atlas tiles: premultiplied alpha
    WGPUSamplerDescriptor sd{};
    sd.label         = sv("Atlas Sampler");
    sd.magFilter     = WGPUFilterMode_Nearest;
    sd.minFilter     = WGPUFilterMode_Nearest;
    sd.mipmapFilter  = WGPUMipmapFilterMode_Nearest;
    sd.addressModeU  = WGPUAddressMode_ClampToEdge;
    sd.addressModeV  = WGPUAddressMode_ClampToEdge;
    sd.addressModeW  = WGPUAddressMode_ClampToEdge;
    sd.maxAnisotropy = 1;
    atlasSampler = wgpuDeviceCreateSampler(ctx.device, &sd);

    WGPUBindGroupLayoutEntry entries[2]{};
    entries[0].binding               = 0;
    entries[0].visibility            = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType    = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].binding               = 1;
    entries[1].visibility            = WGPUShaderStage_Fragment;
    entries[1].sampler.type          = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor ld{};
    ld.label      = sv("Atlas Page Layout");
    ld.entryCount = 2;
    ld.entries    = entries;
    atlasLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &ld);

    if (WGPUShaderModule m = createShaderModule(ctx, (const char*)kShaderTexture, "texture.wgsl")) {
        WGPUVertexAttribute attribs[5]{};
        attribs[0] = { WGPUVertexFormat_Float32x2, offsetof(ChunkInstance, center),   0 };
        attribs[1] = { WGPUVertexFormat_Float32x2, offsetof(ChunkInstance, scale),    1 };
        attribs[2] = { WGPUVertexFormat_Float32x2, offsetof(ChunkInstance, uvOffset), 2 };
        attribs[3] = { WGPUVertexFormat_Float32x2, offsetof(ChunkInstance, uvScale),  3 };
        attribs[4] = { WGPUVertexFormat_Float32,   offsetof(ChunkInstance, opacity),  4 };
        WGPUVertexBufferLayout instances{};
        instances.arrayStride    = sizeof(ChunkInstance);
        instances.stepMode       = WGPUVertexStepMode_Instance;
        instances.attributeCount = 5;
        instances.attributes     = attribs;

        WGPUBlendState blend{};
        blend.color = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha };
        blend.alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha };
        atlasPipeline = makePipeline(ctx, "Atlas Pipeline", m, cameraLayout, atlasLayout, blend, &instances);
        wgpuShaderModuleRelease(m);
    }

    // ---- edit previews
    previewParamsLayout   = uniformLayout(ctx, "Preview Params Layout", WGPUShaderStage_Fragment, sizeof(PreviewSignalParams));
    previewTexturesLayout = textureLayout(ctx, "Preview Textures Layout", 2 + kPreviewMaskTextures);
    if (WGPUShaderModule m = createShaderModule(ctx, (const char*)kShaderPreviewSignal, "preview-signal.wgsl")) {
        previewSignalPipeline = makeFullscreenPipeline(ctx, "Preview Signal Pipeline", m, { previewParamsLayout, previewTexturesLayout }, nullptr);
        wgpuShaderModuleRelease(m);
    }
    blitLayout = textureLayout(ctx, "Blit Layout", 1);
    if (WGPUShaderModule m = createShaderModule(ctx, (const char*)kShaderPreviewBlit, "preview-blit.wgsl")) {
        WGPUBlendState blend{};
        blend.color = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha };
        blend.alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha };
        blitPipeline = makeFullscreenPipeline(ctx, "Blit Pipeline", m, { blitLayout }, &blend);
        wgpuShaderModuleRelease(m);
    }

    return cameraLayout && colorLayout && colorPipeline && atlasLayout && atlasPipeline && atlasSampler &&
           previewParamsLayout && previewTexturesLayout && previewSignalPipeline && blitLayout && blitPipeline;
}

void CanvasPipelines::release()
{
    if (blitPipeline)          { wgpuRenderPipelineRelease(blitPipeline);           blitPipeline          = nullptr; }
    if (blitLayout)            { wgpuBindGroupLayoutRelease(blitLayout);            blitLayout            = nullptr; }
    if (previewSignalPipeline) { wgpuRenderPipelineRelease(previewSignalPipeline);  previewSignalPipeline = nullptr; }
    if (previewTexturesLayout) { wgpuBindGroupLayoutRelease(previewTexturesLayout); previewTexturesLayout = nullptr; }
    if (previewParamsLayout)   { wgpuBindGroupLayoutRelease(previewParamsLayout);   previewParamsLayout   = nullptr; }
    if (atlasPipeline) { wgpuRenderPipelineRelease(atlasPipeline);  atlasPipeline = nullptr; }
    if (atlasSampler)  { wgpuSamplerRelease(atlasSampler);          atlasSampler  = nullptr; }
    if (atlasLayout)   { wgpuBindGroupLayoutRelease(atlasLayout);   atlasLayout   = nullptr; }
    if (colorPipeline) { wgpuRenderPipelineRelease(colorPipeline);  colorPipeline = nullptr; }
    if (colorLayout)   { wgpuBindGroupLayoutRelease(colorLayout);   colorLayout   = nullptr; }
    if (cameraLayout)  { wgpuBindGroupLayoutRelease(cameraLayout);  cameraLayout  = nullptr; }
}
