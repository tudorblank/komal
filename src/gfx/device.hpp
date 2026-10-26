#pragma once

#include <webgpu/webgpu.h>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

struct GpuFrame {
    WGPUSurfaceTexture      surfaceTexture{};
    WGPUTextureView         view    = nullptr;
    WGPUCommandEncoder      encoder = nullptr;
    WGPURenderPassEncoder   pass    = nullptr;
    uint32_t                width = 0, height = 0; // swapchain image size in pixels
};

class GpuDevice {
public:
    bool init(SDL_Window* window);
    void shutdown();

    // gpuframe lifecycle
    void resize(int pixelW, int pixelH); // reconfigures the surface if changed (or suboptimal)
    bool acquire(GpuFrame& f);           // (swapchain image + command encoder) blocks until a swapchain image is free (vsync)
    void beginSurfacePass(GpuFrame& f, const float clearRGBA[4]); // 'pass' draws into the swapchain image
    void present(GpuFrame& f);           // one submit for the whole frame, then present

    // device getters
    WGPUDevice        device()        const { return m_device; }
    WGPUQueue         queue()         const { return m_queue; }
    WGPUTextureFormat surfaceFormat() const { return m_format; }
    const std::string& adapterInfo()  const { return m_adapterInfo; }
    const char* presentModeName() const;
    uint32_t    frameLatency()    const { return m_frameLatency; }

private:
    // internals
    void configureSurface();
    WGPUInstance m_instance = nullptr;
    WGPUSurface  m_surface  = nullptr;
    void*        m_surfaceView = nullptr;   // macOS: the SDL Metal view under the surface
    WGPUAdapter  m_adapter  = nullptr;
    WGPUDevice   m_device   = nullptr;
    WGPUQueue    m_queue    = nullptr;
    WGPUTextureFormat m_format = WGPUTextureFormat_Undefined;
    // trackers
    int  m_width = 0, m_height = 0;
    bool m_surfaceStale = false;    // got suboptimal image: reconfigure next frame
    std::string m_adapterInfo;
    WGPUPresentMode m_presentMode = WGPUPresentMode_Fifo;
    uint32_t m_frameLatency = 1;    // swapchain images queued ahead of the display
    std::vector<WGPUPresentMode> m_supportedModes;
};
