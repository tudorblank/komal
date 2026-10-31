#pragma once
// Where the GPU draws the main window: a wgpu surface over the SDL window, made the way each
// platform wants it (a Win32 window, a Metal layer, Wayland or X11). Owns what it made on the
// way (macOS: the Metal view), released once the GPU device has let go of the surface.

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
