#pragma once
// Whether the GPU calls made while it lives failed. wgpu never hands back null for a creation
// that failed (out of memory, a bad descriptor): it hands back an invalid object and reports the
// error, so this is how a creation is checked. Scopes nest; wgpu-native answers at once.
//
//   GpuErrorScope scope(device);
//   texture.reset(wgpuDeviceCreateTexture(device, &desc));
//   if (scope.failed()) ...   // logged, with what went wrong

#include <webgpu/webgpu.h>

#include <string>

class GpuErrorScope {
public:
    explicit GpuErrorScope(WGPUDevice device);
    ~GpuErrorScope() { (void)failed(); }
    GpuErrorScope(const GpuErrorScope&) = delete;
    GpuErrorScope& operator=(const GpuErrorScope&) = delete;

    // Closes the scope (once): true if anything failed in it, with why (logged as `what`).
    bool failed(const char* what = nullptr);

private:
    WGPUDevice  m_device;
    bool        m_open = true;
    bool        m_failed = false;
};
