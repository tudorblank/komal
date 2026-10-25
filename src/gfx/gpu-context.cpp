#include "gpu-context.h"
#include "surface-factory.h"

#include <SDL3/SDL.h>
#include <webgpu/wgpu.h>      // wgpu-native extras (wgpuDevicePoll, ...)
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {

void logSv(const char* tag, WGPUStringView m)
{
    if (!m.data) return;
    if (m.length == WGPU_STRLEN) std::fprintf(stderr, "[wgpu] %s: %s\n", tag, m.data);
    else std::fprintf(stderr, "[wgpu] %s: %.*s\n", tag, (int)m.length, m.data);
}

struct AdapterRequest { WGPUAdapter adapter = nullptr; };
struct DeviceRequest  { WGPUDevice  device  = nullptr; };

void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
               WGPUStringView msg, void* u1, void*)
{
    if (status == WGPURequestAdapterStatus_Success)
        static_cast<AdapterRequest*>(u1)->adapter = adapter;
    else
        logSv("RequestAdapter failed", msg);
}

void onDevice(WGPURequestDeviceStatus status, WGPUDevice device,
              WGPUStringView msg, void* u1, void*)
{
    if (status == WGPURequestDeviceStatus_Success)
        static_cast<DeviceRequest*>(u1)->device = device;
    else
        logSv("RequestDevice failed", msg);
}

void onUncapturedError(const WGPUDevice*, WGPUErrorType, WGPUStringView msg, void*, void*)
{
    logSv("error", msg);
}

void onDeviceLost(const WGPUDevice*, WGPUDeviceLostReason, WGPUStringView msg, void*, void*)
{
    logSv("device lost", msg);
}

bool isSrgb(WGPUTextureFormat f)
{
    return f == WGPUTextureFormat_BGRA8UnormSrgb || f == WGPUTextureFormat_RGBA8UnormSrgb;
}

} // namespace

bool GpuContext::init(SDL_Window* window)
{
    m_instance = wgpuCreateInstance(nullptr);
    if (!m_instance) { std::fprintf(stderr, "wgpuCreateInstance failed\n"); return false; }

    m_surface = createSurface(m_instance, window, &m_surfaceView);
    if (!m_surface) { std::fprintf(stderr, "Surface creation failed\n"); return false; }

    // ---- adapter (wgpu-native completes this synchronously with AllowSpontaneous)
    AdapterRequest areq;
    WGPURequestAdapterOptions aopts{};
    aopts.compatibleSurface = m_surface;
    aopts.powerPreference   = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo acb{};
    acb.mode      = WGPUCallbackMode_AllowSpontaneous;
    acb.callback  = onAdapter;
    acb.userdata1 = &areq;
    wgpuInstanceRequestAdapter(m_instance, &aopts, acb);
    m_adapter = areq.adapter;
    if (!m_adapter) return false;

    // ---- report which adapter/backend we actually got
    {
        WGPUAdapterInfo info{};
        wgpuAdapterGetInfo(m_adapter, &info);
        const char* backend = "?";
        switch (info.backendType) {
            case WGPUBackendType_Vulkan:   backend = "Vulkan"; break;
            case WGPUBackendType_D3D12:    backend = "D3D12";  break;
            case WGPUBackendType_Metal:    backend = "Metal";  break;
            case WGPUBackendType_OpenGL:   backend = "OpenGL"; break;
            case WGPUBackendType_OpenGLES: backend = "OpenGLES"; break;
            default: break;
        }
        const char* type = "Unknown";
        switch (info.adapterType) {
            case WGPUAdapterType_DiscreteGPU:   type = "Discrete GPU";   break;
            case WGPUAdapterType_IntegratedGPU: type = "Integrated GPU"; break;
            case WGPUAdapterType_CPU:           type = "CPU (SOFTWARE!)"; break;
            default: break;
        }
        std::string dev(info.device.data ? info.device.data : "",
                        info.device.data ? (info.device.length == WGPU_STRLEN
                                            ? std::strlen(info.device.data) : info.device.length) : 0);
        m_adapterInfo = dev + " | " + backend + " | " + type;
        std::fprintf(stderr, "[gpu] %s\n", m_adapterInfo.c_str());
        wgpuAdapterInfoFreeMembers(info);
    }

    // ---- device
    DeviceRequest dreq;
    WGPUDeviceDescriptor ddesc{};
    ddesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    ddesc.deviceLostCallbackInfo.mode          = WGPUCallbackMode_AllowSpontaneous;
    ddesc.deviceLostCallbackInfo.callback      = onDeviceLost;
    WGPURequestDeviceCallbackInfo dcb{};
    dcb.mode      = WGPUCallbackMode_AllowSpontaneous;
    dcb.callback  = onDevice;
    dcb.userdata1 = &dreq;
    wgpuAdapterRequestDevice(m_adapter, &ddesc, dcb);
    m_device = dreq.device;
    if (!m_device) return false;

    m_queue = wgpuDeviceGetQueue(m_device);

    // ---- pick a surface format. ImGui expects a NON-sRGB target for correct colours.
    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(m_surface, m_adapter, &caps);
    m_format = caps.formatCount ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
    for (size_t i = 0; i < caps.formatCount; ++i) {
        if (!isSrgb(caps.formats[i])) { m_format = caps.formats[i]; break; }
    }
    m_supportedModes.assign(caps.presentModes, caps.presentModes + caps.presentModeCount);
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    // Present mode: default Fifo (vsync). Override with KOMAL_PRESENT_MODE=fifo|fifo_relaxed|mailbox|immediate
    if (const char* pm = std::getenv("KOMAL_PRESENT_MODE")) {
        WGPUPresentMode want = WGPUPresentMode_Fifo;
        if      (!std::strcmp(pm, "mailbox"))      want = WGPUPresentMode_Mailbox;
        else if (!std::strcmp(pm, "immediate"))    want = WGPUPresentMode_Immediate;
        else if (!std::strcmp(pm, "fifo_relaxed")) want = WGPUPresentMode_FifoRelaxed;
        for (WGPUPresentMode m : m_supportedModes)
            if (m == want) m_presentMode = want;
    }

    // Frames the swapchain may queue ahead of the display. wgpu defaults to 2, which with Fifo
    // means the picture trails the mouse by ~3 refreshes: panning/dragging feels like rubber.
    // 1 is plenty for a UI that renders in a few ms. Override with KOMAL_FRAME_LATENCY=1..3
    if (const char* fl = std::getenv("KOMAL_FRAME_LATENCY")) {
        const int v = std::atoi(fl);
        if (v >= 1 && v <= 3) m_frameLatency = (uint32_t)v;
    }

    SDL_GetWindowSizeInPixels(window, &m_width, &m_height);
    configureSurface();
    return true;
}

const char* GpuContext::presentModeName() const
{
    switch (m_presentMode) {
        case WGPUPresentMode_Fifo:        return "Fifo (vsync)";
        case WGPUPresentMode_FifoRelaxed: return "FifoRelaxed";
        case WGPUPresentMode_Mailbox:     return "Mailbox";
        case WGPUPresentMode_Immediate:   return "Immediate (no vsync)";
        default:                          return "?";
    }
}

void GpuContext::configureSurface()
{
    m_surfaceStale = false;
    if (m_width <= 0 || m_height <= 0) return;

    WGPUSurfaceConfigurationExtras extras{};
    extras.chain.sType = (WGPUSType)WGPUSType_SurfaceConfigurationExtras;
    extras.desiredMaximumFrameLatency = m_frameLatency;

    WGPUSurfaceConfiguration cfg{};
    cfg.nextInChain = &extras.chain;
    cfg.device      = m_device;
    cfg.format      = m_format;
    cfg.usage       = WGPUTextureUsage_RenderAttachment;
    cfg.width       = (uint32_t)m_width;
    cfg.height      = (uint32_t)m_height;
    cfg.alphaMode   = WGPUCompositeAlphaMode_Auto;
    cfg.presentMode = m_presentMode;
    wgpuSurfaceConfigure(m_surface, &cfg);
}

void GpuContext::resize(int w, int h)
{
    if (w == m_width && h == m_height && !m_surfaceStale) return;
    m_width = w; m_height = h;
    configureSurface();
}

bool GpuContext::acquire(Frame& f)
{
    if (m_width <= 0 || m_height <= 0) return false;

    wgpuSurfaceGetCurrentTexture(m_surface, &f.surfaceTexture);
    switch (f.surfaceTexture.status) {
        case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
            break;
        case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
            m_surfaceStale = true;   // still presentable, fix it up before the next frame
            break;
        case WGPUSurfaceGetCurrentTextureStatus_Outdated:
        case WGPUSurfaceGetCurrentTextureStatus_Lost:
            if (f.surfaceTexture.texture) wgpuTextureRelease(f.surfaceTexture.texture);
            f = Frame{};
            configureSurface();
            return false;
        default:   // Timeout, OutOfMemory, DeviceLost, Error
            if (f.surfaceTexture.texture) wgpuTextureRelease(f.surfaceTexture.texture);
            f = Frame{};
            return false;
    }

    f.width   = wgpuTextureGetWidth(f.surfaceTexture.texture);
    f.height  = wgpuTextureGetHeight(f.surfaceTexture.texture);
    f.view    = wgpuTextureCreateView(f.surfaceTexture.texture, nullptr);

    WGPUCommandEncoderDescriptor edesc{};
    f.encoder = wgpuDeviceCreateCommandEncoder(m_device, &edesc);
    return true;
}

void GpuContext::beginSurfacePass(Frame& f, const float clear[4])
{
    WGPURenderPassColorAttachment ca{};
    ca.view       = f.view;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp     = WGPULoadOp_Clear;
    ca.storeOp    = WGPUStoreOp_Store;
    ca.clearValue = { clear[0], clear[1], clear[2], clear[3] };

    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments     = &ca;
    f.pass = wgpuCommandEncoderBeginRenderPass(f.encoder, &rp);
}

void GpuContext::present(Frame& f)
{
    if (f.pass) {
        wgpuRenderPassEncoderEnd(f.pass);
        wgpuRenderPassEncoderRelease(f.pass);
    }

    WGPUCommandBufferDescriptor cdesc{};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(f.encoder, &cdesc);
    wgpuCommandEncoderRelease(f.encoder);

    wgpuQueueSubmit(m_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);

    wgpuSurfacePresent(m_surface);

    wgpuTextureViewRelease(f.view);
    wgpuTextureRelease(f.surfaceTexture.texture);
    f = Frame{};

    wgpuDevicePoll(m_device, false, nullptr);   // runs completed-work callbacks, frees retired resources
}

void GpuContext::shutdown()
{
    if (m_queue)    { wgpuQueueRelease(m_queue);       m_queue = nullptr; }
    if (m_surface)  { wgpuSurfaceUnconfigure(m_surface);
                      wgpuSurfaceRelease(m_surface);   m_surface = nullptr; }
    destroySurfaceView(m_surfaceView);
    m_surfaceView = nullptr;
    if (m_device)   { wgpuDeviceRelease(m_device);     m_device = nullptr; }
    if (m_adapter)  { wgpuAdapterRelease(m_adapter);   m_adapter = nullptr; }
    if (m_instance) { wgpuInstanceRelease(m_instance); m_instance = nullptr; }
}
