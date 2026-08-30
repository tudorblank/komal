#include "gfxdevice.hpp"

#include <cmath>

// commons
#ifdef _WIN32
void GFXDevice::init(HWND hwnd)
{
    m_instance = wgpuCreateInstance(nullptr);

    WGPUSurfaceSourceWindowsHWND hwndSrc{};
    hwndSrc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    hwndSrc.hinstance = GetModuleHandle(nullptr);
    hwndSrc.hwnd = hwnd;

    WGPUSurfaceDescriptor surfDesc{};
    surfDesc.nextInChain = (WGPUChainedStruct*)&hwndSrc;
    m_surface = wgpuInstanceCreateSurface(m_instance, &surfDesc);

    initCommon();
}
#else
void GFXDevice::init(const char* platform, void* display, void* handle)
{
    m_instance = wgpuCreateInstance(nullptr);

    WGPUSurfaceDescriptor surfDesc{};

    WGPUSurfaceSourceXlibWindow xlibSrc{};
    WGPUSurfaceSourceWaylandSurface waylandSrc{};

    if(strcmp(platform, "wayland") == 0)
    {
        waylandSrc.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        waylandSrc.display = display;
        waylandSrc.surface = handle;
        surfDesc.nextInChain = (WGPUChainedStruct*)&waylandSrc;
    }
    else // xcb / X11
    {
        xlibSrc.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        xlibSrc.display = display;
        xlibSrc.window = (uint64_t)(uintptr_t)handle;
        surfDesc.nextInChain = (WGPUChainedStruct*)&xlibSrc;
    }

    m_surface = wgpuInstanceCreateSurface(m_instance, &surfDesc);

    initCommon();
}
#endif
void GFXDevice::initCommon()
{
    // request adapter
    WGPURequestAdapterOptions adapterOpts{};
    adapterOpts.compatibleSurface = m_surface;

    struct AdapterResult { WGPUAdapter adapter = nullptr; bool done = false; } adapterResult;

    WGPURequestAdapterCallbackInfo adapterCbInfo{};
    adapterCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    adapterCbInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                 WGPUStringView message, void* userdata1, void*) {
        auto* result = (AdapterResult*)userdata1;
        if(status == WGPURequestAdapterStatus_Success)
            result->adapter = adapter;
        else
            fprintf(stderr, "Adapter request failed: %.*s\n", (int)message.length, message.data);
        result->done = true;
    };
    adapterCbInfo.userdata1 = &adapterResult;

    wgpuInstanceRequestAdapter(m_instance, &adapterOpts, adapterCbInfo);

    while(!adapterResult.done)
        wgpuInstanceProcessEvents(m_instance);
    m_adapter = adapterResult.adapter;
    if(!m_adapter)
    {
        fprintf(stderr, "Fatal: no adapter, cannot continue\n");
        return;
    }

    // request device
    struct DeviceResult { WGPUDevice device = nullptr; bool done = false; } deviceResult;

    WGPUDeviceDescriptor deviceDesc{};
    deviceDesc.label = sv("Komal GPU Device");

    deviceDesc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType type,
                                                       WGPUStringView message, void*, void*) {
        fprintf(stderr, "WebGPU error [%d]: %.*s\n", (int)type, (int)message.length, message.data);
    };

    deviceDesc.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason,
                                                    WGPUStringView message, void*, void*) {
        fprintf(stderr, "WebGPU device lost [%d]: %.*s\n", (int)reason, (int)message.length, message.data);
    };

    WGPURequestDeviceCallbackInfo deviceCbInfo{};
    deviceCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    deviceCbInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                                WGPUStringView message, void* userdata1, void*) {
        auto* result = (DeviceResult*)userdata1;
        if(status == WGPURequestDeviceStatus_Success)
            result->device = device;
        else
            fprintf(stderr, "Device request failed: %.*s\n", (int)message.length, message.data);
        result->done = true;
    };
    deviceCbInfo.userdata1 = &deviceResult;

    wgpuAdapterRequestDevice(m_adapter, &deviceDesc, deviceCbInfo);

    while(!deviceResult.done)
        wgpuInstanceProcessEvents(m_instance);
    m_device = deviceResult.device;

    m_queue = wgpuDeviceGetQueue(m_device);
    m_initialized = true;
}
void GFXDevice::configSurface(uint32_t width, uint32_t height)
{
    if(width <= 0 || height <= 0) return;

    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(m_surface, m_adapter, &caps);

    m_surfaceFormat = caps.formats[0];
    for(size_t i = 0; i < caps.formatCount; i++)
    {
        WGPUTextureFormat f = caps.formats[i];
        if(f == WGPUTextureFormat_BGRA8Unorm || f == WGPUTextureFormat_RGBA8Unorm)
        {
            m_surfaceFormat = f;
            break;
        }
    }

    WGPUPresentMode chosenMode = WGPUPresentMode_Fifo;
    for(size_t i = 0; i < caps.presentModeCount; i++)
    {
        if(caps.presentModes[i] == WGPUPresentMode_Mailbox) { chosenMode = WGPUPresentMode_Mailbox; break; }
        if(caps.presentModes[i] == WGPUPresentMode_Immediate) chosenMode = WGPUPresentMode_Immediate;
    }

    WGPUSurfaceConfiguration config{};
    config.device = m_device;
    config.format = m_surfaceFormat;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = width;
    config.height = height;
    config.presentMode = chosenMode;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;

    wgpuSurfaceConfigure(m_surface, &config);
    wgpuSurfaceCapabilitiesFreeMembers(caps);
}
void GFXDevice::passContext(Camera& cam)
{
    GFXContext ctx = context();
    cam.m_ctx = ctx;        // camera
    m_COLSYS.m_ctx = ctx;   // color
    m_TEXSYS.m_ctx = ctx;   // texture
    m_LINSYS.m_ctx = ctx;   // line
    m_BLURSYS.m_ctx = ctx;  // blur
}

// internal
void GFXDevice::initIndexBuffer()
{   
    // index buffer
    uint16_t idx[] = { 0, 1, 2, 1, 3, 2 };

    WGPUBufferDescriptor idxDesc{};
    idxDesc.label = sv("Index Buffer");
    idxDesc.size = sizeof(idx);
    idxDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    m_indexBuffer = wgpuDeviceCreateBuffer(m_device, &idxDesc);
    wgpuQueueWriteBuffer(m_queue, m_indexBuffer, 0, idx, sizeof(idx));
}

// render
void GFXDevice::renderPass(uint32_t width, uint32_t height, const Camera& cam)
{
    // safety check
    WGPUSurfaceTexture surfaceTex{};
    wgpuSurfaceGetCurrentTexture(m_surface, &surfaceTex);
    if(surfaceTex.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
    surfaceTex.status == WGPUSurfaceGetCurrentTextureStatus_Lost)
    {
        configSurface(width, height);
        return;
    }
    if(surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
    surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
        return;

    WGPUTextureView view = wgpuTextureCreateView(surfaceTex.texture, nullptr);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, nullptr);
    
    // background color
    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = view;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = { 0.10, 0.10, 0.10, 1.0 }; // clear color

    // pass
    WGPURenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    
    // color pass
    wgpuRenderPassEncoderSetPipeline(pass, m_COLSYS.m_pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_COLSYS.m_vertBuff, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, m_indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, cam.m_bindGroup, 0, nullptr);
    for(ColObject& colObj : m_COLSYS.m_colObjects)
    {
        wgpuRenderPassEncoderSetBindGroup(pass, 1, colObj.bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, 0, 0, 0);
    }

    // texture pass
    wgpuRenderPassEncoderSetPipeline(pass, m_TEXSYS.m_pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_TEXSYS.m_vertBuff, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, cam.m_bindGroup, 0, nullptr);

    float viewMinX = (0.0f            - cam.pan.x) / cam.zoom;
    float viewMinY = (0.0f            - cam.pan.y) / cam.zoom;
    float viewMaxX = ((float)width  - cam.pan.x) / cam.zoom;
    float viewMaxY = ((float)height - cam.pan.y) / cam.zoom;

    int minCX = Grid::worldToChunk((int)std::floor(viewMinX)) - 1;
    int maxCX = Grid::worldToChunk((int)std::floor(viewMaxX)) + 1;
    int minCY = Grid::worldToChunk((int)std::floor(viewMinY)) - 1;
    int maxCY = Grid::worldToChunk((int)std::floor(viewMaxY)) + 1;

    size_t visibleTileCount = (size_t)(maxCX - minCX + 1) * (size_t)(maxCY - minCY + 1);

    bool viewChanged = m_TEXSYS.updateView(minCX, maxCX, minCY, maxCY);

    for(size_t layerIndex = 0; layerIndex < m_TEXSYS.m_chunkObjects.size(); layerIndex++)
    {
        auto& objMap = m_TEXSYS.m_chunkObjects[layerIndex];
        if(layerIndex >= m_TEXSYS.m_atlases.size()) continue;
        AtlasSet& atlas = m_TEXSYS.m_atlases[layerIndex];

        bool needsRebuild = viewChanged || atlas.contentDirty ||
                             atlas.cachedInstances.size() != atlas.pages.size();

        if(needsRebuild)
        {
            atlas.cachedInstances.assign(atlas.pages.size(), {});

            auto addIfVisible = [&](uint64_t key, const ChunkObject& obj)
            {
                if(obj.x + obj.w < viewMinX || obj.x > viewMaxX ||
                   obj.y + obj.h < viewMinY || obj.y > viewMaxY)
                    return;

                auto slotIt = atlas.chunkSlots.find(key);
                if(slotIt == atlas.chunkSlots.end()) return;

                atlas.cachedInstances[slotIt->second.page].push_back(makeChunkInstance(obj, slotIt->second));
            };

            if(objMap.size() <= visibleTileCount)
            {
                for(auto& [key, obj] : objMap)
                    addIfVisible(key, obj);
            }
            else
            {
                for(int cy = minCY; cy <= maxCY; cy++)
                    for(int cx = minCX; cx <= maxCX; cx++)
                    {
                        auto it = objMap.find(Key::pack(cx, cy));
                        if(it != objMap.end())
                            addIfVisible(it->first, it->second);
                    }
            }

            // only touch the GPU for pages that actually have visible instances
            for(size_t p = 0; p < atlas.pages.size(); p++)
            {
                auto& instances = atlas.cachedInstances[p];
                if(instances.empty()) continue;

                wgpuQueueWriteBuffer(m_queue, atlas.pages[p].instanceBuffer, 0,
                                      instances.data(), instances.size() * sizeof(ChunkInstance));
            }

            atlas.contentDirty = false;
        }

        // draw from cache every frame
        for(size_t p = 0; p < atlas.pages.size(); p++)
        {
            auto& instances = atlas.cachedInstances[p];
            if(instances.empty()) continue;

            AtlasPage& page = atlas.pages[p];
            wgpuRenderPassEncoderSetBindGroup(pass, 1, page.bindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 1, page.instanceBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(pass, 6, (uint32_t)instances.size(), 0, 0, 0);
        }
    }

    // line pass
    if(m_showBBOXs && m_LINSYS.m_vertCount > 0)
    {
        wgpuRenderPassEncoderSetPipeline(pass, m_LINSYS.m_pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_LINSYS.m_vertBuff, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, cam.m_screenBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, m_LINSYS.m_bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, m_LINSYS.m_vertCount, 1, 0, 0);
    }

    // end frame
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(m_queue, 1, &cmdBuffer);

    wgpuSurfacePresent(m_surface);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTex.texture);
}