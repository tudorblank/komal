#pragma once

#include <webgpu.h>
#include "raster/raster-utils.hpp"
#include "gfxcontext.hpp"

#include <cstdint>
#include <cstring>

struct BlurParams{
    int32_t radius;
    int32_t _pad[3] = {};
};

class BlurSys{
public:
    GFXContext m_ctx;

    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUComputePipeline m_pipeline = nullptr;

    void createComputePipeline();

    void blurTile(const RGBA* paddedPixels, int paddedSize, int radius, RGBA* outPixels);

    void releaseAll();
    ~BlurSys() { releaseAll(); }
};