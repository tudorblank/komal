#pragma once

#include <QMatrix4x4>

#include <windows.h>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>

#include <webgpu.h>

#include "raster.hpp"

inline std::string readFile(const char* filename)
{
    std::ifstream f(filename);
    if(!f) throw std::runtime_error(std::string("Cannot open: ") + filename);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static WGPUStringView sv(const char* s) {
    return WGPUStringView{ s, strlen(s) };
}

// CAMERA
struct CameraUniform{
    float viewProj[16];
};

class Camera
{
public:
    Camera() = default;
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    Vec2 pan = { 0.0f, 0.0f };
    Vec2 pendingPan = { 0.0f, 0.0f };
    bool panDirty = false;
    float zoom = 1.0f;

    void create(WGPUDevice device, WGPUQueue queue);
    void update(WGPUQueue queue, float screenW, float screenH);
    WGPUBuffer m_buffer = nullptr;
    WGPUBindGroupLayout m_bindLayout = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;

    void createScreen(WGPUDevice device, WGPUQueue queue, uint32_t width, uint32_t height);
    void updateScreen(WGPUQueue queue, uint32_t width, uint32_t height);
    WGPUBuffer m_screenBuffer = nullptr;
    WGPUBindGroupLayout m_screenBindLayout = nullptr;
    WGPUBindGroup m_screenBindGroup = nullptr;

    ~Camera()
    {
        if(m_screenBuffer)    wgpuBufferRelease(m_screenBuffer);
        if(m_screenBindLayout)wgpuBindGroupLayoutRelease(m_screenBindLayout);
        if(m_screenBindGroup) wgpuBindGroupRelease(m_screenBindGroup);
        
        if(m_bindGroup)       wgpuBindGroupRelease(m_bindGroup);
        if(m_bindLayout)      wgpuBindGroupLayoutRelease(m_bindLayout);
        if(m_buffer)          wgpuBufferRelease(m_buffer);
    }
};

// COLOR
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

// TEXTURE
struct TexVert{
    float pos[2];
    float uv[2];
};
struct TexUniform{
    float model[16];
    float uvOffset[2];
    float uvScale[2];
    float opacity = 1.0f;
    float _pad0[7] = {};
};
struct ChunkSlot { int page = -1; int slotX = 0; int slotY = 0; };

struct TexObject{ // chunk object
    float x, y, w, h;
    ChunkSlot slot; // where this chunk lives in the atlas (page + slotX/Y)
    WGPUBuffer modelBuffer = nullptr;
    WGPUBindGroup bindGroup = nullptr; 
};

struct AtlasPage{
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    std::vector<bool> slotUsed; 
    int usedSlots = 0;
    static constexpr int PAGE_SIZE = 4096;
    static constexpr int SLOTS_PER_ROW = PAGE_SIZE / Chunk::SIZE;
    static constexpr int SLOTS_TOTAL = SLOTS_PER_ROW * SLOTS_PER_ROW;
    bool isFull() const { return usedSlots >= SLOTS_TOTAL; }
};

struct AtlasSet{
    std::vector<AtlasPage> pages;
    std::unordered_map<uint64_t, ChunkSlot> chunkSlots; // keyed by chunkKey
};

class GFXDevice
{
public:
    GFXDevice() = default;
    GFXDevice(const GFXDevice&) = delete;
    GFXDevice& operator=(const GFXDevice&) = delete;

    // init
    void init(HWND hwnd);
    bool m_initialized;
    WGPUInstance m_instance = nullptr;
    WGPUSurface  m_surface  = nullptr;
    WGPUAdapter  m_adapter  = nullptr;
    WGPUDevice   m_device   = nullptr;
    WGPUQueue    m_queue    = nullptr;
    void configSurface(uint32_t width, uint32_t height);
    WGPUTextureFormat m_surfaceFormat = WGPUTextureFormat_Undefined;

    // geometry
    void createGeometry();
    WGPUBuffer m_colVertBuffer = nullptr;
    WGPUBuffer m_texVertBuffer = nullptr;
    WGPUBuffer m_indexBuffer = nullptr;
    
    // shader modules
    void createShaders();
    WGPUShaderModule m_colShaderModule = nullptr;
    WGPUShaderModule m_texShaderModule = nullptr;

    // color pipeline
    void createColorBindLayout();
    WGPUBindGroupLayout m_colBindLayout = nullptr;
    void createColorPipeline(WGPUBindGroupLayout camLayout);
    WGPURenderPipeline m_colPipeline = nullptr;
    WGPUPipelineLayout m_colPipelineLayout = nullptr;

    std::vector<ColObject> m_colObjects;
    ColObject& addColObject(float x, float y, float w, float h, float r, float g, float b, float a);
    void updateColObjectMVP(ColObject& obj);

    // texture pipeline
    void createTexBindLayout();
    WGPUBindGroupLayout m_texBindLayout = nullptr;
    void createTexPipeline(WGPUBindGroupLayout camLayout);
    WGPURenderPipeline m_texPipeline = nullptr;
    WGPUPipelineLayout m_texPipelineLayout = nullptr;

    // atlas
    void initAtlas(); // just creates the shared nearest-sampler now; pages are created lazily per layer
    AtlasSet& atlasForLayer(size_t layerIndex); // returns (creating if needed) that layer's own atlas set
    std::vector<AtlasSet> m_atlases;
    WGPUSampler m_atlasSampler = nullptr;

    AtlasPage createAtlasPage();
    ChunkSlot allocateSlot(AtlasSet& atlas, uint64_t chunkKey);
    void freeSlot(AtlasSet& atlas, uint64_t chunkKey);

    TexObject buildAtlasTexObject(AtlasSet& atlas, uint64_t chunkKey, float x, float y, float w, float h, const RGBA* pixels);
    void writeChunkToAtlas(AtlasPage& page, ChunkSlot slot, const RGBA* pixels);
    std::vector<std::unordered_map<uint64_t, TexObject>> m_chunkTexObjects;

    // update tex
    void updateTexObject(TexObject& obj, AtlasSet& atlas, const RGBA* pixels);
    void updateAtlasTexObjectModel(TexObject& obj);

    // line pipeline
    void createLinePipeline(WGPUBindGroupLayout screenCamLayout);
    WGPUShaderModule m_lineShaderModule = nullptr;
    WGPUBindGroupLayout m_lineBindLayout = nullptr;
    WGPUBindGroup m_lineColorBindGroup = nullptr;
    WGPUPipelineLayout m_linePipelineLayout = nullptr;
    WGPURenderPipeline m_linePipeline = nullptr;

    // bbox lines
    void updateBoundsLines(const std::vector<BoundsI>& boxes, Vec2 pan, float zoom);
    WGPUBuffer m_lineVertexBuffer = nullptr;
    uint32_t m_lineVertexCapacity = 0;
    uint32_t m_lineVertexCount = 0;
    bool m_showBoundsLine = false;

    // render
    void renderPass(uint32_t width, uint32_t height, const Camera& cam);

    // destructor
    ~GFXDevice()
    {
        for(auto& layerMap : m_chunkTexObjects)
        {
            for(auto& [key, texObj] : layerMap)
            {
                if(texObj.bindGroup)    wgpuBindGroupRelease(texObj.bindGroup);
                if(texObj.modelBuffer)  wgpuBufferRelease(texObj.modelBuffer);
            }
        }

        for(auto& atlas : m_atlases)
            for(auto& page : atlas.pages)
            {
                if(page.view)    wgpuTextureViewRelease(page.view);
                if(page.texture) wgpuTextureRelease(page.texture);
            }
        if(m_atlasSampler) wgpuSamplerRelease(m_atlasSampler);

        if(m_texPipelineLayout)     wgpuPipelineLayoutRelease(m_texPipelineLayout);
        if(m_texPipeline)           wgpuRenderPipelineRelease(m_texPipeline);
        if(m_texBindLayout)         wgpuBindGroupLayoutRelease(m_texBindLayout);

        if(m_lineVertexBuffer)      wgpuBufferRelease(m_lineVertexBuffer);
        if(m_linePipeline)          wgpuRenderPipelineRelease(m_linePipeline);
        if(m_linePipelineLayout)    wgpuPipelineLayoutRelease(m_linePipelineLayout);
        if(m_lineColorBindGroup)    wgpuBindGroupRelease(m_lineColorBindGroup);
        if(m_lineBindLayout)        wgpuBindGroupLayoutRelease(m_lineBindLayout);
        if(m_lineShaderModule)      wgpuShaderModuleRelease(m_lineShaderModule);

        for(auto& colObj : m_colObjects)
        {
            if(colObj.bindGroup)    wgpuBindGroupRelease(colObj.bindGroup);
            if(colObj.buffer)       wgpuBufferRelease(colObj.buffer);
        }

        if(m_colPipelineLayout) wgpuPipelineLayoutRelease(m_colPipelineLayout);
        if(m_colPipeline)       wgpuRenderPipelineRelease(m_colPipeline);
        if(m_colBindLayout)     wgpuBindGroupLayoutRelease(m_colBindLayout);

        if(m_texShaderModule)   wgpuShaderModuleRelease(m_texShaderModule);
        if(m_colShaderModule)   wgpuShaderModuleRelease(m_colShaderModule);

        if(m_indexBuffer)       wgpuBufferRelease(m_indexBuffer);
        if(m_texVertBuffer)     wgpuBufferRelease(m_texVertBuffer);
        if(m_colVertBuffer)     wgpuBufferRelease(m_colVertBuffer);

        if(m_queue)    wgpuQueueRelease(m_queue);
        if(m_device)   wgpuDeviceRelease(m_device);
        if(m_adapter)  wgpuAdapterRelease(m_adapter);
        if(m_surface)  wgpuSurfaceRelease(m_surface);
        if(m_instance) wgpuInstanceRelease(m_instance);
    }
};