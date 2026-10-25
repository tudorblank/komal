#include "surface-factory.h"
#include <SDL3/SDL.h>
#include <cstring>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

WGPUSurface createSurface(WGPUInstance instance, SDL_Window* window)
{
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);

#if defined(_WIN32)
    WGPUSurfaceSourceWindowsHWND src{};
    src.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    src.hinstance   = GetModuleHandleW(nullptr);
    src.hwnd        = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);

    WGPUSurfaceDescriptor desc{};
    desc.nextInChain = &src.chain;
    return wgpuInstanceCreateSurface(instance, &desc);

#elif defined(__linux__)
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "wayland") == 0) {
        WGPUSurfaceSourceWaylandSurface src{};
        src.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        src.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        src.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        WGPUSurfaceDescriptor desc{};
        desc.nextInChain = &src.chain;
        return wgpuInstanceCreateSurface(instance, &desc);
    }
    WGPUSurfaceSourceXlibWindow src{};
    src.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
    src.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    src.window  = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    WGPUSurfaceDescriptor desc{};
    desc.nextInChain = &src.chain;
    return wgpuInstanceCreateSurface(instance, &desc);

#else
    #error "createSurface: platform not implemented (macOS needs a CAMetalLayer via Obj-C++)"
#endif
}
