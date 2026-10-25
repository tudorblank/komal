#pragma once
#include "gfx/gfx-context.h"

#include <cstdint>

// Uniform of the preview signal pass (preview-signal.wgsl). Channels in NodeChannel order
// (A, R, G, B); a mask is the index of its texture (0..11), or -1.
struct PreviewSignalParams {
    float   mainK[4], wetK[4], dryK[4];
    int32_t mainM[4], wetM[4], dryM[4];
    int32_t flags[4];   // [0] Wet present, [1] Dry present
};
static_assert(sizeof(PreviewSignalParams) == 112);
constexpr int kPreviewMaskTextures = 12;

// GPU state shared by every item of a canvas scene: the bind group layouts and pipelines each
// item kind draws with. Items own only their data (buffers, textures, bind groups).
//
// Bind group 0 of every pipeline is the camera (a view-projection matrix, one per view).
struct CanvasPipelines {
    GfxContext ctx;

    WGPUBindGroupLayout cameraLayout = nullptr;   // group 0: mat4 viewProj

    // Solid rectangles (color.wgsl). Group 1: model matrix + colour per item.
    WGPUBindGroupLayout colorLayout   = nullptr;
    WGPURenderPipeline  colorPipeline = nullptr;

    // Atlas-backed raster tiles (texture.wgsl), premultiplied. Group 1: page texture + sampler.
    // Vertex buffer 0: one ChunkInstance per tile (see tile-atlas.h).
    WGPUBindGroupLayout atlasLayout   = nullptr;
    WGPURenderPipeline  atlasPipeline = nullptr;
    WGPUSampler         atlasSampler  = nullptr;   // nearest: pixels stay crisp squares at any zoom

    // Edit previews (preview-signal.wgsl, preview-blit.wgsl): full-screen passes over textures
    // the size of the view, read with textureLoad (pixel for pixel, no sampler).
    //   signal: one node's signals on the GPU. Group 0: PreviewSignalParams. Group 1: Wet, Dry
    //           and 12 mask textures (signal * 4 + channel). Writes (no blending).
    //   blit:   one texture drawn over the target, premultiplied. Group 0: the texture.
    WGPUBindGroupLayout previewParamsLayout   = nullptr;
    WGPUBindGroupLayout previewTexturesLayout = nullptr;
    WGPURenderPipeline  previewSignalPipeline = nullptr;
    WGPUBindGroupLayout blitLayout            = nullptr;
    WGPURenderPipeline  blitPipeline          = nullptr;

    bool create(const GfxContext& context);
    void release();
};
