#pragma once
#include <webgpu/webgpu.h>

struct SDL_Window;

// SDL3 has no built-in wgpu glue, so this turns an SDL_Window into a WGPUSurface.
WGPUSurface createSurface(WGPUInstance instance, SDL_Window* window);
