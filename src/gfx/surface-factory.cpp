#include "surface-factory.h"
#include <SDL3/SDL.h>
#include <cstring>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

WGPUSurface createSurface(WGPUInstance instance, SDL_Window* window, void** platformView)
{
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    *platformView = nullptr;

#if defined(_WIN32)
    WGPUSurfaceSourceWindowsHWND src{};
    src.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    src.hinstance   = GetModuleHandleW(nullptr);
    src.hwnd        = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);

    WGPUSurfaceDescriptor desc{};
    desc.nextInChain = &src.chain;
    return wgpuInstanceCreateSurface(instance, &desc);

#elif defined(__APPLE__)
    // SDL makes the Metal view and its CAMetalLayer: no Objective-C needed here.
    (void)props;
    SDL_MetalView view = SDL_Metal_CreateView(window);
    if (!view) return nullptr;
    *platformView = view;
    WGPUSurfaceSourceMetalLayer src{};
    src.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    src.layer       = SDL_Metal_GetLayer(view);
    WGPUSurfaceDescriptor desc{};
    desc.nextInChain = &src.chain;
    return wgpuInstanceCreateSurface(instance, &desc);

#else   // Linux, BSDs: Wayland or X11
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
#endif
}

void destroySurfaceView(void* platformView)
{
#if defined(__APPLE__)
    if (platformView) SDL_Metal_DestroyView(static_cast<SDL_MetalView>(platformView));
#else
    (void)platformView;
#endif
}
