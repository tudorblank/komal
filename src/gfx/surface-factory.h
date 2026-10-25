#pragma once
#include <webgpu/webgpu.h>

struct SDL_Window;

// SDL3 has no built-in wgpu glue, so this turns an SDL_Window into a WGPUSurface: an HWND on
// Windows, a CAMetalLayer on macOS (the window needs SDL_WINDOW_METAL), Wayland or X11
// elsewhere. Some platforms need an extra native object kept alive with the surface
// (`platformView`); release it with destroySurfaceView() after the surface.
WGPUSurface createSurface(WGPUInstance instance, SDL_Window* window, void** platformView);
void        destroySurfaceView(void* platformView);
