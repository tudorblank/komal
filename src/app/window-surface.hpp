#pragma once
// cross platform window surface for WebGPU (Metal, Vulkan, D3D12, OpenGL)

#include <webgpu/webgpu.h>

struct SDL_Window;

class WindowSurface {
public:
    WindowSurface() = default;
    ~WindowSurface() { release(); }
    WindowSurface(const WindowSurface&) = delete;
    WindowSurface& operator=(const WindowSurface&) = delete;

    WGPUSurface create(WGPUInstance instance, SDL_Window* window);   // null on failure
    void release();                                                  // after the device released the surface

private:
    void* m_platformView = nullptr;   // macOS: the SDL Metal view under the surface
};
