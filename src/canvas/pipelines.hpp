#pragma once
#include "gfx/handles.hpp"

#include <cstdint>

// Uniform of the preview signal pass (preview-signal.wgsl). Channels in NodeChannel order
// (A, R, G, B); a mask is its texture index (0..11), or -1.
struct PreviewSignalParams {
    float   mainK[4], wetK[4], dryK[4];
    int32_t mainM[4], wetM[4], dryM[4];
    int32_t flags[4];   // [0] Wet present, [1] Dry present
};
static_assert(sizeof(PreviewSignalParams) == 112);
constexpr int kPreviewMaskTextures = 12;

// Uniform of the preview stroke pass (preview-stroke.wgsl): the brush over a raster's pixels.
struct PreviewStrokeParams {
    float   color[4];    // premultiplied
    float   opacity;     // applied once to the whole stroke
    int32_t erase;       // erases instead of painting
    int32_t keepAlpha;   // the raster's transparency stays (alpha locked)
    int32_t pad;
};
static_assert(sizeof(PreviewStrokeParams) == 32);

// Uniform of a solid rectangle (color.wgsl).
struct RectParams {
    float model[16];   // unit quad centred on the origin -> world rectangle
    float color[4];    // straight RGBA
};
static_assert(sizeof(RectParams) == 80);

// Uniform of the checkerboard background (checker.wgsl).
struct CheckerParams {
    float colorA[4], colorB[4];   // straight RGBA
    float origin[2];              // the canvas origin, target pixels
    float cell;                   // square size, target pixels
    float pad;
};
static_assert(sizeof(CheckerParams) == 48);
CheckerParams pictureChecker(float originX, float originY);   // a small picture's (thumbnails, the navigator), from that corner

// The layouts and pipelines every item kind of a canvas scene draws with, shared (and owned: each
// a GpuObject). Items own only their data (buffers, textures, bind groups).
struct CanvasPipelines {
    GpuHandles ctx;

    GpuObject<WGPUBindGroupLayout> cameraLayout;   // group 0 of world-space pipelines: mat4 viewProj

    // Checkerboard background: full-screen, no camera. Group 0: CheckerParams. No blending.
    GpuObject<WGPUBindGroupLayout> checkerLayout;
    GpuObject<WGPURenderPipeline>  checkerPipeline;

    // Solid rectangles. Group 1: RectParams.
    GpuObject<WGPUBindGroupLayout> colorLayout;
    GpuObject<WGPURenderPipeline>  colorPipeline;

    // Raster tiles, premultiplied. Group 1: page texture + sampler; vertex buffer 0: ChunkInstance.
    GpuObject<WGPUBindGroupLayout> atlasLayout;
    GpuObject<WGPURenderPipeline>  atlasPipeline;
    GpuObject<WGPUSampler>         atlasSampler;   // nearest: pixels stay crisp squares

    // Edit previews: full-screen passes over view-sized textures, read 1:1 (textureLoad).
    //   signal: one node's signals. Group 0: PreviewSignalParams; group 1: Wet, Dry, 12 masks
    //           (signal * 4 + channel). No blending.
    //   stroke: a stroke's coverage over a raster's pixels. Group 0: PreviewStrokeParams; group 1:
    //           the pixels, the coverage. No blending.
    //   blit:   one texture over the target, premultiplied. Group 0: the texture.
    GpuObject<WGPUBindGroupLayout> previewParamsLayout;
    GpuObject<WGPUBindGroupLayout> previewTexturesLayout;
    GpuObject<WGPURenderPipeline>  previewSignalPipeline;
    GpuObject<WGPUBindGroupLayout> previewStrokeLayout;
    GpuObject<WGPUBindGroupLayout> previewStrokeTexturesLayout;
    GpuObject<WGPURenderPipeline>  previewStrokePipeline;
    GpuObject<WGPUBindGroupLayout> blitLayout;
    GpuObject<WGPURenderPipeline>  blitPipeline;

    bool create(const GpuHandles& context);
    void release();
};
