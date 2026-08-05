#pragma once

#include <QMatrix4x4>

#include <webgpu.h>
#include "gfxcontext.hpp"

#include <vector>
#include <unordered_map>
#include <cstdint>

struct ColVert{
    float pos[2];
};
struct ColUniform{
    float model[16];
    float color[4];
};
struct ColObject{
    float x, y, w, h;
    float color[4];
    WGPUBuffer buffer = nullptr;
    WGPUBindGroup bindGroup = nullptr;
};

class ColSys{
public:
    GFXContext m_ctx;
    WGPUBuffer m_vertBuff = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPURenderPipeline m_pipeline = nullptr;

    void createRenderPipeline(WGPUBindGroupLayout camLayout);

    // objects
    std::vector<ColObject> m_colObjects;
    ColObject& addColObject(float x, float y, float w, float h, float r, float g, float b, float a);
    void updateColObjectMVP(ColObject& obj);

    void releaseAll();
    ~ColSys() { releaseAll(); }
};