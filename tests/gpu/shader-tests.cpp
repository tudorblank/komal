// Every canvas shader compiles and every pipeline is made (a WGSL mistake shows here, not at the
// app's start), and the preview's signal pass gives what the CPU does: a mask's factor at each
// pixel is node.hpp's maskFactor, to within the GPU's rounding.

#include "gpu/device.hpp"
#include "gpu/render-pass.hpp"
#include "gpu/texture.hpp"
#include "node/node.hpp"
#include "ui/panels/canvas/pipelines.hpp"

#include <doctest/doctest.h>
#include <webgpu/wgpu.h>

#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr WGPUTextureFormat kTarget = WGPUTextureFormat_RGBA8Unorm;   // CanvasEngine::kTargetFormat

// One device for every test; null when there's no GPU here (the tests are then skipped).
GpuDevice* device()
{
    static GpuDevice gpu;
    static const bool ready = gpu.initHeadless();
    return ready ? &gpu : nullptr;
}

GpuTexture texture(const GpuDevice& gpu, uint32_t w, uint32_t h, const std::vector<RGBA>& pixels)
{
    GpuTexture t;
    REQUIRE(t.create(gpu.device(), { w, h, kTarget, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, "Test input" }));
    WGPUTexelCopyTextureInfo dst{};
    dst.texture = t.texture();
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow  = w * 4;
    layout.rowsPerImage = h;
    const WGPUExtent3D size = { w, h, 1 };
    wgpuQueueWriteTexture(gpu.queue(), &dst, pixels.data(), pixels.size() * sizeof(RGBA), &layout, &size);
    return t;
}

// `target`'s pixels, read back (its width * 4 must be a multiple of 256).
std::vector<RGBA> readBack(const GpuDevice& gpu, WGPUCommandEncoder encoder, const GpuTexture& target)
{
    const uint32_t w = target.width(), h = target.height();
    const uint64_t bytes = uint64_t(w) * h * 4;
    WGPUBufferDescriptor bd{};
    bd.label = sv("Test readback");
    bd.size  = bytes;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    const GpuObject<WGPUBuffer> buffer{ wgpuDeviceCreateBuffer(gpu.device(), &bd) };
    WGPUTexelCopyTextureInfo src{};
    src.texture = target.texture();
    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = buffer;
    dst.layout.bytesPerRow  = w * 4;
    dst.layout.rowsPerImage = h;
    const WGPUExtent3D size = { w, h, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &size);

    WGPUCommandBufferDescriptor cd{};
    const GpuObject<WGPUCommandBuffer> commands{ wgpuCommandEncoderFinish(encoder, &cd) };
    const WGPUCommandBuffer list = commands;
    wgpuQueueSubmit(gpu.queue(), 1, &list);

    bool mapped = false;
    WGPUBufferMapCallbackInfo cb{};
    cb.mode      = WGPUCallbackMode_AllowSpontaneous;
    cb.callback  = [](WGPUMapAsyncStatus status, WGPUStringView, void* done, void*) { *static_cast<bool*>(done) = status == WGPUMapAsyncStatus_Success; };
    cb.userdata1 = &mapped;
    wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, size_t(bytes), cb);
    wgpuDevicePoll(gpu.device(), true, nullptr);
    REQUIRE(mapped);
    std::vector<RGBA> out(size_t(w) * h);
    std::memcpy(out.data(), wgpuBufferGetConstMappedRange(buffer, 0, size_t(bytes)), size_t(bytes));
    wgpuBufferUnmap(buffer);
    return out;
}

}

TEST_CASE("every canvas shader compiles and every pipeline is made")
{
    GpuDevice* gpu = device();
    if (!gpu) {
        MESSAGE("no GPU here: skipped");
        return;
    }
    CanvasPipelines pipelines;
    CHECK(pipelines.create(GpuHandles{ gpu->device(), gpu->queue(), kTarget }));
    pipelines.release();
}

TEST_CASE("the preview's mask factor is the CPU's (maskFactor)")
{
    GpuDevice* gpu = device();
    if (!gpu) {
        MESSAGE("no GPU here: skipped");
        return;
    }
    CanvasPipelines p;
    REQUIRE(p.create(GpuHandles{ gpu->device(), gpu->queue(), kTarget }));

    // 64 x 64 masks, premultiplied (rgb <= a), from transparent to opaque; over white, opaque wet.
    constexpr uint32_t kSize = 64;
    std::vector<RGBA> masks(kSize * kSize);
    uint32_t seed = 7;
    const auto next = [&](int below) { seed = seed * 1664525u + 1013904223u; return int((seed >> 8) % uint32_t(below)); };
    for (RGBA& m : masks) {
        const int a = next(256);
        m = { uint8_t(next(a + 1)), uint8_t(next(a + 1)), uint8_t(next(a + 1)), uint8_t(a) };
    }
    masks[0] = { 0, 0, 0, 0 };
    masks[1] = { 255, 255, 255, 255 };
    masks[2] = { 0, 0, 0, 255 };
    const GpuTexture wet  = texture(*gpu, kSize, kSize, std::vector<RGBA>(kSize * kSize, RGBA{ 255, 255, 255, 255 }));
    const GpuTexture mask = texture(*gpu, kSize, kSize, masks);
    GpuTexture target;
    REQUIRE(target.create(gpu->device(), { kSize, kSize, kTarget, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc, "Test target" }));

    // Main's A channel masked (texture 0); every scale 1; no dry.
    PreviewSignalParams params{};
    for (int c = 0; c < 4; ++c) {
        params.mainK[c] = params.wetK[c] = params.dryK[c] = 1.0f;
        params.mainM[c] = params.wetM[c] = params.dryM[c] = -1;
    }
    params.mainM[0] = 0;
    params.flags[0] = 1;
    GpuUniform uniform;
    REQUIRE(uniform.create(gpu->device(), p.previewParamsLayout, sizeof(params), "Test params"));
    uniform.write(gpu->queue(), &params, sizeof(params));
    WGPUTextureView views[2 + kPreviewMaskTextures];
    views[0] = wet.view();
    views[1] = wet.view();   // not read (no dry)
    for (int m = 0; m < kPreviewMaskTextures; ++m) views[2 + m] = mask.view();
    const GpuObject<WGPUBindGroup> textures = textureBindGroup(gpu->device(), p.previewTexturesLayout, views, "Test textures");

    WGPUCommandEncoderDescriptor ed{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device(), &ed);
    {
        GpuRenderPass pass(encoder, target.view(), GpuRenderPass::Load::Clear, "Test signal");
        wgpuRenderPassEncoderSetPipeline(pass, p.previewSignalPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uniform.group, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, textures, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    }
    const std::vector<RGBA> out = readBack(*gpu, encoder, target);
    wgpuCommandEncoderRelease(encoder);

    int worst = 0;
    for (size_t i = 0; i < masks.size(); ++i) {
        const int cpu = maskFactor(masks[i]);
        worst = std::max(worst, std::abs(int(out[i].a) - cpu));
        if (std::abs(int(out[i].a) - cpu) > 1) {
            CAPTURE(i);
            CAPTURE(int(masks[i].r)); CAPTURE(int(masks[i].g)); CAPTURE(int(masks[i].b)); CAPTURE(int(masks[i].a));
            CHECK(int(out[i].a) == cpu);
        }
    }
    CHECK(worst <= 1);   // the GPU's float maths against the CPU's bytes: a step of rounding at most
    p.release();
}
