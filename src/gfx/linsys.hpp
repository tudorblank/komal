#pragma once

#include <QMatrix4x4>

#include <webgpu.h>
#include "gfxcontext.hpp"
#include "raster/raster-utils.hpp"

#include <vector>
#include <unordered_map>
#include <cstdint>

class LinSys{
public:
    GFXContext m_ctx;

    void createRenderPipeline(WGPUBindGroupLayout screenCamLayout);
    WGPUBindGroup m_bindGroup = nullptr;
    WGPURenderPipeline m_pipeline = nullptr;

    // bbox lines
    WGPUBuffer m_vertBuff = nullptr;
    uint32_t m_vertBuffCapacity = 0;
    uint32_t m_vertCount = 0;
    void updateBoundsLines(const std::vector<BoundsI>& boxes, Vec2 pan, float zoom);

    void releaseAll();
    ~LinSys() { releaseAll(); }
};