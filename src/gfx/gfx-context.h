#pragma once
#include <webgpu/webgpu.h>

// What a rendering subsystem needs from the device: non-owning, cheap to copy.
// targetFormat is the format of the texture the subsystem's pipelines draw into.
struct GfxContext {
    WGPUDevice        device       = nullptr;
    WGPUQueue         queue        = nullptr;
    WGPUTextureFormat targetFormat = WGPUTextureFormat_Undefined;
};

inline WGPUStringView sv(const char* s) { return WGPUStringView{ s, WGPU_STRLEN }; }

// Compiles WGSL source (a C string, e.g. one embedded by embed_file()). Errors are reported
// through the device's uncaptured-error callback.
WGPUShaderModule createShaderModule(const GfxContext& ctx, const char* wgsl, const char* label);
