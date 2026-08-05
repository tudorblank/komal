#pragma once

#include <QMatrix4x4>
#include <webgpu.h>
#include "raster.hpp"
#include "gfxcontext.hpp"

// CAMERA
struct CameraUniform{
    float viewProj[16];
};

class Camera{
public:
    Camera() = default;
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    Vec2 pan = { 0.0f, 0.0f };
    Vec2 pendingPan = { 0.0f, 0.0f };
    bool panDirty = false;
    float zoom = 1.0f;

    GFXContext m_ctx;

    void create();
    void update(float screenW, float screenH);
    WGPUBuffer m_buffer = nullptr;
    WGPUBindGroupLayout m_bindLayout = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;

    void createScreen(uint32_t width, uint32_t height);
    void updateScreen(uint32_t width, uint32_t height);
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