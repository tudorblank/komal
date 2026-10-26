#pragma once
#include <webgpu/webgpu.h>

// GpuHandles: what a renderer needs from the GpuDevice (device.h), without owning anything:
// cheap to copy, handed to whatever builds pipelines or textures. targetFormat is the format of
// the texture that renderer's pipelines draw into. Plus small wgpu helpers used everywhere.
struct GpuHandles {
    WGPUDevice        device       = nullptr;
    WGPUQueue         queue        = nullptr;
    WGPUTextureFormat targetFormat = WGPUTextureFormat_Undefined;
};

inline WGPUStringView sv(const char* s) { return WGPUStringView{ s, WGPU_STRLEN }; }

// Compiles WGSL source (a C string, e.g. one embedded by embed_file()). Errors are reported
// through the device's uncaptured-error callback.
WGPUShaderModule createShaderModule(const GpuHandles& ctx, const char* wgsl, const char* label);
