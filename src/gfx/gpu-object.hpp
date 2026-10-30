#pragma once
// One reference to a wgpu object, owned: released when the GpuObject goes (or is reset), moved
// but never copied. The one way the app holds GPU objects; it reads as the raw handle wherever a
// wgpu call wants one.
//
//   GpuObject<WGPUTexture> texture{ wgpuDeviceCreateTexture(device, &desc) };
//   wgpuTextureCreateView(texture, nullptr);   // the raw handle
//
// (The device, queue, surface and instance belong to GpuDevice, which releases them in order.)

#include <webgpu/webgpu.h>

#include <utility>

// How each kind of object is released.
template<class T> struct GpuRelease;
template<> struct GpuRelease<WGPUTexture>         { static void release(WGPUTexture o)         { wgpuTextureRelease(o); } };
template<> struct GpuRelease<WGPUTextureView>     { static void release(WGPUTextureView o)     { wgpuTextureViewRelease(o); } };
template<> struct GpuRelease<WGPUBuffer>          { static void release(WGPUBuffer o)          { wgpuBufferRelease(o); } };
template<> struct GpuRelease<WGPUBindGroup>       { static void release(WGPUBindGroup o)       { wgpuBindGroupRelease(o); } };
template<> struct GpuRelease<WGPUBindGroupLayout> { static void release(WGPUBindGroupLayout o) { wgpuBindGroupLayoutRelease(o); } };
template<> struct GpuRelease<WGPUPipelineLayout>  { static void release(WGPUPipelineLayout o)  { wgpuPipelineLayoutRelease(o); } };
template<> struct GpuRelease<WGPURenderPipeline>  { static void release(WGPURenderPipeline o)  { wgpuRenderPipelineRelease(o); } };
template<> struct GpuRelease<WGPUSampler>         { static void release(WGPUSampler o)         { wgpuSamplerRelease(o); } };
template<> struct GpuRelease<WGPUShaderModule>    { static void release(WGPUShaderModule o)    { wgpuShaderModuleRelease(o); } };

template<class T>
class GpuObject {
public:
    GpuObject() = default;
    explicit GpuObject(T object) : m_object(object) {}
    GpuObject(GpuObject&& o) noexcept : m_object(std::exchange(o.m_object, nullptr)) {}
    GpuObject& operator=(GpuObject&& o) noexcept
    {
        if (this != &o) reset(std::exchange(o.m_object, nullptr));
        return *this;
    }
    GpuObject(const GpuObject&) = delete;
    GpuObject& operator=(const GpuObject&) = delete;
    ~GpuObject() { reset(); }

    void reset(T object = nullptr)   // releases what it held, then holds `object`
    {
        if (m_object) GpuRelease<T>::release(m_object);
        m_object = object;
    }
    T get() const { return m_object; }
    operator T() const { return m_object; }
    explicit operator bool() const { return m_object != nullptr; }

private:
    T m_object = nullptr;
};
