#pragma once
#include <webgpu/webgpu.h>
#include <cstdint>

// What GPU code needs from the GpuDevice, without owning anything.
struct GpuHandles {
    WGPUDevice        device       = nullptr;
    WGPUQueue         queue        = nullptr;
    WGPUTextureFormat targetFormat = WGPUTextureFormat_Undefined;
};

inline WGPUStringView sv(const char* s) { return WGPUStringView{ s, WGPU_STRLEN }; }

WGPUShaderModule createShaderModule(const GpuHandles& ctx, const char* wgsl, const char* label);

// A uniform buffer bound alone at binding 0 of a bind group.
struct GpuUniform {
    WGPUBuffer    buffer = nullptr;
    WGPUBindGroup group  = nullptr;

    bool create(WGPUDevice device, WGPUBindGroupLayout layout, uint64_t size, const char* label);
    void write(WGPUQueue queue, const void* data, uint64_t size) const { wgpuQueueWriteBuffer(queue, buffer, 0, data, size); }
    void release();
    explicit operator bool() const { return group != nullptr; }
};
