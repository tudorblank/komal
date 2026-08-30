#pragma once

#ifdef _WIN32
  #include <windows.h>
#endif

#include <webgpu.h>
#include "gfxcontext.hpp"
#include "camera.hpp"
#include "colsys.hpp"
#include "texsys.hpp"
#include "linsys.hpp"
#include "blursys.hpp"

class GFXDevice{
public:
    GFXDevice() = default;
    GFXDevice(const GFXDevice&) = delete;
    GFXDevice& operator=(const GFXDevice&) = delete;

    WGPUInstance m_instance = nullptr;
    WGPUSurface  m_surface  = nullptr;
    WGPUAdapter  m_adapter  = nullptr;
    WGPUDevice   m_device   = nullptr;
    WGPUQueue    m_queue    = nullptr;
    WGPUTextureFormat m_surfaceFormat = WGPUTextureFormat_Undefined;

    GFXContext context() const { return { m_instance, m_device, m_queue, m_surfaceFormat }; }
    void passContext(Camera& cam);
    bool m_initialized = false;

    // device branches
    ColSys m_COLSYS;
    TexSys m_TEXSYS;
    LinSys m_LINSYS;
    bool m_showBBOXs = false;
    BlurSys m_BLURSYS;

    // init
#ifdef _WIN32
    void init(HWND hwnd);
#else
    void init(const char* platform, void* display, void* handle);
#endif
    void initCommon();
    void configSurface(uint32_t width, uint32_t height);

    void initIndexBuffer();
    WGPUBuffer m_indexBuffer = nullptr;

    // render
    void renderPass(uint32_t width, uint32_t height, const Camera& cam);

    // destructor
    ~GFXDevice()
    {
        m_LINSYS.releaseAll();
        m_TEXSYS.releaseAll();
        m_COLSYS.releaseAll();
        m_BLURSYS.releaseAll();

        if(m_indexBuffer){
            wgpuBufferRelease(m_indexBuffer);
            m_indexBuffer = nullptr;
        }

        if(m_queue)    wgpuQueueRelease(m_queue);
        if(m_device)   wgpuDeviceRelease(m_device);
        if(m_adapter)  wgpuAdapterRelease(m_adapter);
        if(m_surface)  wgpuSurfaceRelease(m_surface);
        if(m_instance) wgpuInstanceRelease(m_instance);
    }
};