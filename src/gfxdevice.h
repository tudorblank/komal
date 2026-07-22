#pragma once

#include <QMatrix4x4>

#include <webgpu.h>
#include "raster.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>

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

    ~Camera()
    {
        if(m_bindGroup)   wgpuBindGroupRelease(m_bindGroup);
        if(m_bindLayout)  wgpuBindGroupLayoutRelease(m_bindLayout);
        if(m_buffer)      wgpuBufferRelease(m_buffer);
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
};
struct TexObject{
    float x, y, w, h;
    WGPUBuffer modelBuffer = nullptr;
    WGPUTexture texture = nullptr;
    WGPUTextureView textureView = nullptr;
    WGPUSampler sampler = nullptr;
    WGPUBindGroup bindGroup = nullptr;
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

    std::unordered_map<uint64_t, TexObject> m_chunkTexObjects;
    TexObject buildTexObject(float x, float y, float w, float h,
                            const RGBA* pixels, uint32_t texW, uint32_t texH);
    void updateTexObject(TexObject& obj, const RGBA* pixels, uint32_t texW, uint32_t texH);
    void updateTexObjectModel(TexObject& obj);

    // render
    void renderPass(uint32_t width, uint32_t height, WGPUBindGroup cameraBindGroup);

    ~GFXDevice()
    {
        for(auto& [key, texObj] : m_chunkTexObjects)
        {
            if(texObj.bindGroup)    wgpuBindGroupRelease(texObj.bindGroup);
            if(texObj.sampler)      wgpuSamplerRelease(texObj.sampler);
            if(texObj.textureView)  wgpuTextureViewRelease(texObj.textureView);
            if(texObj.texture)      wgpuTextureRelease(texObj.texture);
            if(texObj.modelBuffer)  wgpuBufferRelease(texObj.modelBuffer);
        }

        if(m_texPipelineLayout) wgpuPipelineLayoutRelease(m_texPipelineLayout);
        if(m_texPipeline)       wgpuRenderPipelineRelease(m_texPipeline);
        if(m_texBindLayout)     wgpuBindGroupLayoutRelease(m_texBindLayout);

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