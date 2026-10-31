#include "gfx/error-scope.hpp"
#include "base/log.hpp"

namespace {

struct Popped {
    bool        failed = false;
    std::string message;
};

void pop(WGPUDevice device, Popped& out)
{
    WGPUPopErrorScopeCallbackInfo info{};
    info.mode      = WGPUCallbackMode_AllowSpontaneous;
    info.userdata1 = &out;
    info.callback  = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void* u, void*) {
        auto& p = *static_cast<Popped*>(u);
        if (status != WGPUPopErrorScopeStatus_Success || type == WGPUErrorType_NoError) return;
        p.failed = true;
        if (p.message.empty() && message.data)
            p.message.assign(message.data, message.length == WGPU_STRLEN ? std::char_traits<char>::length(message.data) : message.length);
    };
    wgpuDevicePopErrorScope(device, info);
}

}

GpuErrorScope::GpuErrorScope(WGPUDevice device) : m_device(device)
{
    wgpuDevicePushErrorScope(m_device, WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(m_device, WGPUErrorFilter_Validation);
}

bool GpuErrorScope::failed(const char* what)
{
    if (!m_open) return m_failed;
    m_open = false;
    Popped validation, memory;
    pop(m_device, validation);
    pop(m_device, memory);
    m_failed = validation.failed || memory.failed;
    if (m_failed) Log::error("gpu", "%s: %s", what ? what : "a GPU call failed", (memory.failed ? memory : validation).message.c_str());
    return m_failed;
}
