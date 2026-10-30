#include "pipelines.hpp"
#include "canvas/tiles/atlas.hpp"   // ChunkInstance layout
#include "base/math.hpp"
#include "base/palette.hpp"

#include <cstddef>
#include <initializer_list>
#include <vector>

extern const unsigned char kShaderColor[];           // generated from shaders/*.wgsl, see CMakeLists.txt
extern const unsigned char kShaderTexture[];
extern const unsigned char kShaderPreviewSignal[];
extern const unsigned char kShaderPreviewBlit[];
extern const unsigned char kShaderPreviewStroke[];
extern const unsigned char kShaderChecker[];

namespace {

WGPUBindGroupLayout uniformLayout(const GpuHandles& ctx, const char* label, WGPUShaderStage stages, uint64_t size)
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

WGPUBindGroupLayout textureLayout(const GpuHandles& ctx, const char* label, int count)
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

// Premultiplied "over".
constexpr WGPUBlendState kPremultiplied = {
    { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha },
    { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha },
};
// Straight-alpha "over".
constexpr WGPUBlendState kStraight = {
    { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha },
    { WGPUBlendOperation_Add, WGPUBlendFactor_One,      WGPUBlendFactor_OneMinusSrcAlpha },
};

struct PipelineDesc {
    const char*                       label;
    const unsigned char*              wgsl;
    std::vector<WGPUBindGroupLayout>  groups;
    const WGPUBlendState*             blend        = nullptr;   // null: overwrite
    const WGPUVertexBufferLayout*     vertexBuffer = nullptr;
    // Quads from vertex_index (strip), or one full-screen triangle (list).
    WGPUPrimitiveTopology             topology     = WGPUPrimitiveTopology_TriangleStrip;
};

WGPURenderPipeline makePipeline(const GpuHandles& ctx, const PipelineDesc& d)
{
    const GpuObject<WGPUShaderModule> module{ createShaderModule(ctx, reinterpret_cast<const char*>(d.wgsl), d.label) };
    if (!module) return nullptr;

    WGPUColorTargetState target{};
    target.format    = ctx.targetFormat;
    target.blend     = d.blend;
    target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs{};
    fs.module      = module;
    fs.entryPoint  = sv("fs_main");
    fs.targetCount = 1;
    fs.targets     = &target;

    WGPUPipelineLayoutDescriptor pld{};
    pld.label                = sv(d.label);
    pld.bindGroupLayoutCount = d.groups.size();
    pld.bindGroupLayouts     = d.groups.data();
    const GpuObject<WGPUPipelineLayout> layout{ wgpuDeviceCreatePipelineLayout(ctx.device, &pld) };

    WGPURenderPipelineDescriptor rpd{};
    rpd.label               = sv(d.label);
    rpd.layout              = layout;
    rpd.vertex.module       = module;
    rpd.vertex.entryPoint   = sv("vs_main");
    rpd.vertex.bufferCount  = d.vertexBuffer ? 1 : 0;
    rpd.vertex.buffers      = d.vertexBuffer;
    rpd.primitive.topology  = d.topology;
    rpd.primitive.frontFace = WGPUFrontFace_CCW;
    rpd.primitive.cullMode  = WGPUCullMode_None;
    rpd.multisample.count   = 1;
    rpd.multisample.mask    = 0xFFFFFFFFu;
    rpd.fragment            = &fs;
    return wgpuDeviceCreateRenderPipeline(ctx.device, &rpd);   // the layout and module go with this scope
}

}

CheckerParams pictureChecker(float originX, float originY)
{
    CheckerParams c{};
    const auto dark = Palette::kPictureCheckerA.floats(), light = Palette::kPictureCheckerB.floats();
    for (int i = 0; i < 3; ++i) { c.colorA[i] = dark[i]; c.colorB[i] = light[i]; }
    c.colorA[3] = c.colorB[3] = 1.0f;
    c.origin[0] = originX;
    c.origin[1] = originY;
    c.cell      = Palette::kPictureChecker;
    return c;
}

bool CanvasPipelines::create(const GpuHandles& context)
{
    ctx = context;
    constexpr auto kFullscreen = WGPUPrimitiveTopology_TriangleList;
    cameraLayout.reset(uniformLayout(ctx, "Canvas Camera Layout", WGPUShaderStage_Vertex, sizeof(Mat4::m)));

    // ---- solid rectangles
    colorLayout.reset(uniformLayout(ctx, "Rect Layout", WGPUShaderStage_Vertex | WGPUShaderStage_Fragment, sizeof(RectParams)));
    colorPipeline.reset(makePipeline(ctx, { "color.wgsl", kShaderColor, { cameraLayout, colorLayout }, &kStraight }));

    // ---- atlas tiles
    WGPUSamplerDescriptor sd{};
    sd.label         = sv("Atlas Sampler");
    sd.magFilter     = WGPUFilterMode_Nearest;
    sd.minFilter     = WGPUFilterMode_Nearest;
    sd.mipmapFilter  = WGPUMipmapFilterMode_Nearest;
    sd.addressModeU  = WGPUAddressMode_ClampToEdge;
    sd.addressModeV  = WGPUAddressMode_ClampToEdge;
    sd.addressModeW  = WGPUAddressMode_ClampToEdge;
    sd.maxAnisotropy = 1;
    atlasSampler.reset(wgpuDeviceCreateSampler(ctx.device, &sd));

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
    atlasLayout.reset(wgpuDeviceCreateBindGroupLayout(ctx.device, &ld));

    const WGPUVertexAttribute attribs[5] = {
        { WGPUVertexFormat_Float32x2, offsetof(ChunkInstance, center),   0 },
        { WGPUVertexFormat_Float32x2, offsetof(ChunkInstance, scale),    1 },
        { WGPUVertexFormat_Float32x2, offsetof(ChunkInstance, uvOffset), 2 },
        { WGPUVertexFormat_Float32x2, offsetof(ChunkInstance, uvScale),  3 },
        { WGPUVertexFormat_Float32,   offsetof(ChunkInstance, opacity),  4 },
    };
    WGPUVertexBufferLayout instances{};
    instances.arrayStride    = sizeof(ChunkInstance);
    instances.stepMode       = WGPUVertexStepMode_Instance;
    instances.attributeCount = 5;
    instances.attributes     = attribs;
    atlasPipeline.reset(makePipeline(ctx, { "texture.wgsl", kShaderTexture, { cameraLayout, atlasLayout }, &kPremultiplied, &instances }));

    // ---- full-screen passes
    checkerLayout.reset(uniformLayout(ctx, "Checker Layout", WGPUShaderStage_Fragment, sizeof(CheckerParams)));
    checkerPipeline.reset(makePipeline(ctx, { "checker.wgsl", kShaderChecker, { checkerLayout }, nullptr, nullptr, kFullscreen }));

    previewParamsLayout.reset(uniformLayout(ctx, "Preview Params Layout", WGPUShaderStage_Fragment, sizeof(PreviewSignalParams)));
    previewTexturesLayout.reset(textureLayout(ctx, "Preview Textures Layout", 2 + kPreviewMaskTextures));
    previewSignalPipeline.reset(makePipeline(ctx, { "preview-signal.wgsl", kShaderPreviewSignal,
                                                { previewParamsLayout, previewTexturesLayout }, nullptr, nullptr, kFullscreen }));

    previewStrokeLayout.reset(uniformLayout(ctx, "Preview Stroke Layout", WGPUShaderStage_Fragment, sizeof(PreviewStrokeParams)));
    previewStrokeTexturesLayout.reset(textureLayout(ctx, "Preview Stroke Textures Layout", 2));
    previewStrokePipeline.reset(makePipeline(ctx, { "preview-stroke.wgsl", kShaderPreviewStroke,
                                                { previewStrokeLayout, previewStrokeTexturesLayout }, nullptr, nullptr, kFullscreen }));

    blitLayout.reset(textureLayout(ctx, "Blit Layout", 1));
    blitPipeline.reset(makePipeline(ctx, { "preview-blit.wgsl", kShaderPreviewBlit, { blitLayout }, &kPremultiplied, nullptr, kFullscreen }));

    return cameraLayout && colorLayout && colorPipeline && atlasLayout && atlasPipeline && atlasSampler &&
           previewParamsLayout && previewTexturesLayout && previewSignalPipeline && previewStrokeLayout && previewStrokeTexturesLayout &&
           previewStrokePipeline && blitLayout && blitPipeline &&
           checkerLayout && checkerPipeline;
}

void CanvasPipelines::release()
{
    *this = CanvasPipelines{};   // every handle released
}
