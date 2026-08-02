#include "gfxdevice.hpp"

// CAMERA
void Camera::create(WGPUDevice device, WGPUQueue queue)
{
    QMatrix4x4 identity;
    CameraUniform camInput{};
    memcpy(camInput.viewProj, identity.constData(), sizeof(camInput.viewProj));

    WGPUBufferDescriptor camDesc{};
    camDesc.label = sv("Camera Uniform Buffer");
    camDesc.size = sizeof(CameraUniform);
    camDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_buffer = wgpuDeviceCreateBuffer(device, &camDesc);
    wgpuQueueWriteBuffer(queue, m_buffer, 0, camInput.viewProj, sizeof(camInput.viewProj));

    WGPUBindGroupLayoutEntry camLayoutEntry{};
    camLayoutEntry.binding = 0;
    camLayoutEntry.visibility = WGPUShaderStage_Vertex;
    camLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    camLayoutEntry.buffer.minBindingSize = sizeof(CameraUniform);

    WGPUBindGroupLayoutDescriptor camLayoutDesc{};
    camLayoutDesc.label = sv("Camera Bind Group Layout");
    camLayoutDesc.entryCount = 1;
    camLayoutDesc.entries = &camLayoutEntry;
    m_bindLayout = wgpuDeviceCreateBindGroupLayout(device, &camLayoutDesc);

    WGPUBindGroupEntry camGroupEntry{};
    camGroupEntry.binding = 0;
    camGroupEntry.buffer = m_buffer;
    camGroupEntry.offset = 0;
    camGroupEntry.size = sizeof(CameraUniform);

    WGPUBindGroupDescriptor camGroupDesc{};
    camGroupDesc.label = sv("Camera Bind Group");
    camGroupDesc.layout = m_bindLayout;
    camGroupDesc.entryCount = 1;
    camGroupDesc.entries = &camGroupEntry;
    m_bindGroup = wgpuDeviceCreateBindGroup(device, &camGroupDesc);
}
void Camera::update(WGPUQueue queue, float screenW, float screenH)
{
    QMatrix4x4 view;
    view.translate(pan.x, pan.y, 0.0f);
    view.scale(zoom, zoom, 1.0f);

    QMatrix4x4 projection;
    projection.ortho(0.0f, screenW, screenH, 0.0f, -1.0f, 1.0f);

    QMatrix4x4 viewProj = projection * view;

    CameraUniform data{};
    memcpy(data.viewProj, viewProj.constData(), sizeof(data.viewProj));
    wgpuQueueWriteBuffer(queue, m_buffer, 0, data.viewProj, sizeof(data.viewProj));
}
void Camera::createScreen(WGPUDevice device, WGPUQueue queue, uint32_t width, uint32_t height)
{
    QMatrix4x4 proj;
    proj.ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);

    struct { float proj[16]; } data{};
    memcpy(data.proj, proj.constData(), sizeof(data.proj));

    WGPUBufferDescriptor desc{};
    desc.label = sv("Screen Camera Buffer");
    desc.size = sizeof(data);
    desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_screenBuffer = wgpuDeviceCreateBuffer(device, &desc);
    wgpuQueueWriteBuffer(queue, m_screenBuffer, 0, data.proj, sizeof(data));

    WGPUBindGroupLayoutEntry entry{};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.minBindingSize = sizeof(data);

    WGPUBindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &entry;
    m_screenBindLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    WGPUBindGroupEntry gEntry{};
    gEntry.binding = 0;
    gEntry.buffer = m_screenBuffer;
    gEntry.size = sizeof(data);

    WGPUBindGroupDescriptor gDesc{};
    gDesc.layout = m_screenBindLayout;
    gDesc.entryCount = 1;
    gDesc.entries = &gEntry;
    m_screenBindGroup = wgpuDeviceCreateBindGroup(device, &gDesc);
}
void Camera::updateScreen(WGPUQueue queue, uint32_t width, uint32_t height)
{
    QMatrix4x4 proj;
    proj.ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);

    struct { float proj[16]; } data{};
    memcpy(data.proj, proj.constData(), sizeof(data.proj));
    wgpuQueueWriteBuffer(queue, m_screenBuffer, 0, data.proj, sizeof(data));
}

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

    initCommon(); // adapter/device/queue request, factored out below
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

void GFXDevice::createGeometry()
{   
    // color vert buffer
    ColVert vertsColor[] = {
        {{-0.5f,  0.5f}},
        {{ 0.5f,  0.5f}},
        {{-0.5f, -0.5f}},
        {{ 0.5f, -0.5f}},
    };
    
    WGPUBufferDescriptor colVertDesc{};
    colVertDesc.label = sv("Color Vertex Buffer");
    colVertDesc.size = sizeof(vertsColor);
    colVertDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_colVertBuffer = wgpuDeviceCreateBuffer(m_device, &colVertDesc);
    wgpuQueueWriteBuffer(m_queue, m_colVertBuffer, 0, vertsColor, sizeof(vertsColor));

    // texture vert buffer
    TexVert vertsTex[] = {
        {{-0.5f,  0.5f}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}},
    };

    WGPUBufferDescriptor texVertDesc{};
    texVertDesc.label = sv("Texture Vertex Buffer");
    texVertDesc.size = sizeof(vertsTex);
    texVertDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_texVertBuffer = wgpuDeviceCreateBuffer(m_device, &texVertDesc);
    wgpuQueueWriteBuffer(m_queue, m_texVertBuffer, 0, vertsTex, sizeof(vertsTex));

    // index buffer
    uint16_t idx[] = { 0, 1, 2, 1, 3, 2 };

    WGPUBufferDescriptor idxDesc{};
    idxDesc.label = sv("Index Buffer");
    idxDesc.size = sizeof(idx);
    idxDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    m_indexBuffer = wgpuDeviceCreateBuffer(m_device, &idxDesc);
    wgpuQueueWriteBuffer(m_queue, m_indexBuffer, 0, idx, sizeof(idx));
}

void GFXDevice::createShaders()
{
    // col shader
    std::string colSource = readFile("shaders/color.wgsl");
    WGPUShaderSourceWGSL colWgslDesc{};
    colWgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    colWgslDesc.code = sv(colSource.c_str());

    WGPUShaderModuleDescriptor colShaderDesc{};
    colShaderDesc.nextInChain = &colWgslDesc.chain;
    m_colShaderModule = wgpuDeviceCreateShaderModule(m_device, &colShaderDesc);
    if (!m_colShaderModule)
        printf("Color shader failed\n");

    // tex shader
    std::string texSource = readFile("shaders/texture.wgsl");

    WGPUShaderSourceWGSL texWgslDesc{};
    texWgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    texWgslDesc.code = sv(texSource.c_str());

    WGPUShaderModuleDescriptor texShaderDesc{};
    texShaderDesc.nextInChain = &texWgslDesc.chain;
    m_texShaderModule = wgpuDeviceCreateShaderModule(m_device, &texShaderDesc);
    if(!m_texShaderModule)
        printf("Texture shader failed\n");
}

// color pipeline
void GFXDevice::createColorBindLayout()
{
    WGPUBindGroupLayoutEntry layoutEntry{};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.minBindingSize = sizeof(ColUniform);

    WGPUBindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.label = sv("Color Bind Group Layout");
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    m_colBindLayout = wgpuDeviceCreateBindGroupLayout(m_device, &layoutDesc);
}
void GFXDevice::createColorPipeline(WGPUBindGroupLayout camLayout)
{
    //// vertex
    WGPUVertexState vertState{};
    vertState.module = m_colShaderModule;
    vertState.entryPoint = sv("vs_main");
    
    WGPUVertexAttribute vertAttrib[1]{};
    vertAttrib[0].shaderLocation = 0;
    vertAttrib[0].offset = 0;
    vertAttrib[0].format = WGPUVertexFormat_Float32x2;
    
    WGPUVertexBufferLayout vbLayout{};
    vbLayout.arrayStride = sizeof(ColVert);
    //vbLayout.stepMode = WGPUVertexStepMode_Vertex; // ONLY WHEN MULTIPLE INPUTS
    vbLayout.attributeCount = 1;
    vbLayout.attributes = vertAttrib;
    vertState.bufferCount = 1;
    vertState.buffers = &vbLayout;

    //// fragment
    WGPUFragmentState fragState{};
    fragState.module = m_colShaderModule;
    fragState.entryPoint = sv("fs_main");

    WGPUColorTargetState colorTarget{};
    colorTarget.format = m_surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = WGPUFrontFace_CCW;
    primitiveState.cullMode = WGPUCullMode_None;

    // pipeline layout
    WGPUBindGroupLayout bgLayouts[2] = { camLayout, m_colBindLayout };
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.label = sv("Color Pipeline Layout");
    pipelineLayoutDesc.bindGroupLayoutCount = 2;
    pipelineLayoutDesc.bindGroupLayouts = bgLayouts;
    m_colPipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

    WGPURenderPipelineDescriptor PLDesc{};
    PLDesc.vertex = vertState;
    PLDesc.fragment = &fragState;
    PLDesc.primitive = primitiveState;
    PLDesc.layout = m_colPipelineLayout;
    PLDesc.multisample.count = 1;
    PLDesc.multisample.mask = 0xFFFFFFFF;

    m_colPipeline = wgpuDeviceCreateRenderPipeline(m_device, &PLDesc);
}

// color objects
ColObject& GFXDevice::addColObject(float x, float y, float w, float h, float r, float g, float b, float a)
{
    ColObject obj{};
    obj.x = x; obj.y = y; obj.w = w; obj.h = h;
    obj.color[0] = r; obj.color[1] = g; obj.color[2] = b; obj.color[3] = a;

    WGPUBufferDescriptor buffDesc{};
    buffDesc.label = sv("Color Object Uniform Buffer");
    buffDesc.size = sizeof(ColUniform);
    buffDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    obj.buffer = wgpuDeviceCreateBuffer(m_device, &buffDesc);

    WGPUBindGroupEntry bgEntry{};
    bgEntry.binding = 0;
    bgEntry.buffer = obj.buffer;
    bgEntry.offset = 0;
    bgEntry.size = sizeof(ColUniform);

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.label = sv("Color Object Bind Group");
    bgDesc.layout = m_colBindLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    obj.bindGroup = wgpuDeviceCreateBindGroup(m_device, &bgDesc);

    m_colObjects.push_back(obj);
    updateColObjectMVP(m_colObjects.back());
    return m_colObjects.back();
}
void GFXDevice::updateColObjectMVP(ColObject& obj)
{
    QMatrix4x4 model;
    model.translate(obj.x + obj.w / 2.0f, obj.y + obj.h / 2.0f, 0.0f);
    model.scale(obj.w, obj.h, 1.0f);

    ColUniform data{};
    memcpy(data.model, model.constData(), sizeof(data.model));
    memcpy(data.color, obj.color, sizeof(data.color));
    wgpuQueueWriteBuffer(m_queue, obj.buffer, 0, &data, sizeof(data));
}

// line pipeline
void GFXDevice::createLinePipeline(WGPUBindGroupLayout screenCamLayout)
{
    std::string src = readFile("shaders/line.wgsl");
    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = sv(src.c_str());
    WGPUShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = &wgsl.chain;
    m_lineShaderModule = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);

    WGPUBindGroupLayoutEntry colorEntry{};
    colorEntry.binding = 0;
    colorEntry.visibility = WGPUShaderStage_Fragment;
    colorEntry.buffer.type = WGPUBufferBindingType_Uniform;
    colorEntry.buffer.minBindingSize = sizeof(float) * 4;

    WGPUBindGroupLayoutDescriptor colorLayoutDesc{};
    colorLayoutDesc.entryCount = 1;
    colorLayoutDesc.entries = &colorEntry;
    m_lineBindLayout = wgpuDeviceCreateBindGroupLayout(m_device, &colorLayoutDesc);

    WGPUVertexAttribute attrib{};
    attrib.shaderLocation = 0;
    attrib.offset = 0;
    attrib.format = WGPUVertexFormat_Float32x2;

    WGPUVertexBufferLayout vbLayout{};
    vbLayout.arrayStride = sizeof(float) * 2;
    vbLayout.attributeCount = 1;
    vbLayout.attributes = &attrib;

    WGPUVertexState vertState{};
    vertState.module = m_lineShaderModule;
    vertState.entryPoint = sv("vs_main");
    vertState.bufferCount = 1;
    vertState.buffers = &vbLayout;

    WGPUColorTargetState colorTarget{};
    colorTarget.format = m_surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragState{};
    fragState.module = m_lineShaderModule;
    fragState.entryPoint = sv("fs_main");
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPUPrimitiveState primState{};
    primState.topology = WGPUPrimitiveTopology_LineList;

    WGPUBindGroupLayout bgLayouts[2] = { screenCamLayout, m_lineBindLayout };
    WGPUPipelineLayoutDescriptor plLayoutDesc{};
    plLayoutDesc.bindGroupLayoutCount = 2;
    plLayoutDesc.bindGroupLayouts = bgLayouts;
    m_linePipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &plLayoutDesc);

    WGPURenderPipelineDescriptor plDesc{};
    plDesc.vertex = vertState;
    plDesc.fragment = &fragState;
    plDesc.primitive = primState;
    plDesc.layout = m_linePipelineLayout;
    plDesc.multisample.count = 1;
    plDesc.multisample.mask = 0xFFFFFFFF;
    m_linePipeline = wgpuDeviceCreateRenderPipeline(m_device, &plDesc);

    // color bind group
    WGPUBufferDescriptor colorBufDesc{};
    colorBufDesc.size = sizeof(float) * 4;
    colorBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer colorBuf = wgpuDeviceCreateBuffer(m_device, &colorBufDesc);
    float col[4] = { 0.18f, 0.4f, 1.0f, 1.0f };
    wgpuQueueWriteBuffer(m_queue, colorBuf, 0, col, sizeof(col));

    WGPUBindGroupEntry colorEntry2{};
    colorEntry2.binding = 0;
    colorEntry2.buffer = colorBuf;
    colorEntry2.size = sizeof(col);

    WGPUBindGroupDescriptor colorGroupDesc{};
    colorGroupDesc.layout = m_lineBindLayout;
    colorGroupDesc.entryCount = 1;
    colorGroupDesc.entries = &colorEntry2;
    m_lineColorBindGroup = wgpuDeviceCreateBindGroup(m_device, &colorGroupDesc);
}

void GFXDevice::updateBoundsLines(const std::vector<BoundsI>& boxes, Vec2 pan, float zoom)
{
    std::vector<float> verts;
    verts.reserve(boxes.size() * 8 * 2);

    for(const BoundsI& b : boxes)
    {
        if(!b.valid) continue;

        float x0 = b.minX * zoom + pan.x;
        float y0 = b.minY * zoom + pan.y;
        float x1 = (b.maxX + 1) * zoom + pan.x;
        float y1 = (b.maxY + 1) * zoom + pan.y;

        float edges[8][2] = {
            {x0,y0},{x1,y0},   // top
            {x1,y0},{x1,y1},   // right
            {x1,y1},{x0,y1},   // bottom
            {x0,y1},{x0,y0},   // left
        };
        for(auto& p : edges) { verts.push_back(p[0]); verts.push_back(p[1]); }
    }

    m_lineVertexCount = (uint32_t)(verts.size() / 2);
    if(m_lineVertexCount == 0) return;

    // grow buffer if needed
    if(m_lineVertexCount > m_lineVertexCapacity)
    {
        if(m_lineVertexBuffer) wgpuBufferRelease(m_lineVertexBuffer);

        m_lineVertexCapacity = m_lineVertexCount; // or round up/pad for headroom
        WGPUBufferDescriptor vbDesc{};
        vbDesc.label = sv("Bounds Line Vertex Buffer");
        vbDesc.size = sizeof(float) * 2 * m_lineVertexCapacity;
        vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_lineVertexBuffer = wgpuDeviceCreateBuffer(m_device, &vbDesc);
    }

    wgpuQueueWriteBuffer(m_queue, m_lineVertexBuffer, 0, verts.data(), verts.size() * sizeof(float));
}

// texture pipeline
void GFXDevice::createTexBindLayout()
{
    WGPUBindGroupLayoutEntry entries[3]{};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(TexUniform);

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.label = sv("Texture Bind Group Layout");
    layoutDesc.entryCount = 3;
    layoutDesc.entries = entries;
    m_texBindLayout = wgpuDeviceCreateBindGroupLayout(m_device, &layoutDesc);
}

void GFXDevice::createTexPipeline(WGPUBindGroupLayout camLayout)
{
    //// vertex
    WGPUVertexAttribute vertAttribs[2]{};
    vertAttribs[0].shaderLocation = 0;
    vertAttribs[0].offset = offsetof(TexVert, pos);
    vertAttribs[0].format = WGPUVertexFormat_Float32x2;
    vertAttribs[1].shaderLocation = 1;
    vertAttribs[1].offset = offsetof(TexVert, uv);
    vertAttribs[1].format = WGPUVertexFormat_Float32x2;

    WGPUVertexBufferLayout vbLayout{};
    vbLayout.arrayStride = sizeof(TexVert);
    vbLayout.attributeCount = 2;
    vbLayout.attributes = vertAttribs;

    WGPUVertexState vertState{};
    vertState.module = m_texShaderModule;
    vertState.entryPoint = sv("vs_main");
    vertState.bufferCount = 1;
    vertState.buffers = &vbLayout;

    //// fragment
    WGPUBlendComponent colorBlend{};
    colorBlend.operation = WGPUBlendOperation_Add;
    colorBlend.srcFactor = WGPUBlendFactor_SrcAlpha;
    colorBlend.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    WGPUBlendComponent alphaBlend{};
    alphaBlend.operation = WGPUBlendOperation_Add;
    alphaBlend.srcFactor = WGPUBlendFactor_One;
    alphaBlend.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    WGPUBlendState blendState{};
    blendState.color = colorBlend;
    blendState.alpha = alphaBlend;

    WGPUColorTargetState colorTarget{};
    colorTarget.format = m_surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    colorTarget.blend = &blendState;

    WGPUFragmentState fragState{};
    fragState.module = m_texShaderModule;
    fragState.entryPoint = sv("fs_main");
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = WGPUFrontFace_CCW;
    primitiveState.cullMode = WGPUCullMode_None;

    // pipeline layout
    WGPUBindGroupLayout bgLayouts[2] = { camLayout, m_texBindLayout };
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.label = sv("Texture Pipeline Layout");
    pipelineLayoutDesc.bindGroupLayoutCount = 2;
    pipelineLayoutDesc.bindGroupLayouts = bgLayouts;
    m_texPipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

    WGPURenderPipelineDescriptor PLDesc{};
    PLDesc.vertex = vertState;
    PLDesc.fragment = &fragState;
    PLDesc.primitive = primitiveState;
    PLDesc.layout = m_texPipelineLayout;
    PLDesc.multisample.count = 1;
    PLDesc.multisample.mask = 0xFFFFFFFF;

    m_texPipeline = wgpuDeviceCreateRenderPipeline(m_device, &PLDesc);
}

// texture objects
TexObject GFXDevice::buildAtlasTexObject(AtlasSet& atlas, uint64_t chunkKey,
                                          float x, float y, float w, float h,
                                          const RGBA* pixels)
{
    TexObject obj{};
    obj.x = x; obj.y = y; obj.w = w; obj.h = h;

    ChunkSlot slot = allocateSlot(atlas, chunkKey);
    obj.slot = slot;
    writeChunkToAtlas(atlas.pages[slot.page], slot, pixels);

    WGPUBufferDescriptor modelDesc{};
    modelDesc.label = sv("Chunk Model Buffer");
    modelDesc.size = sizeof(TexUniform);
    modelDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    obj.modelBuffer = wgpuDeviceCreateBuffer(m_device, &modelDesc);

    WGPUBindGroupEntry entries[3]{};
    entries[0].binding = 0; entries[0].buffer = obj.modelBuffer; entries[0].size = sizeof(TexUniform);
    entries[1].binding = 1; entries[1].textureView = atlas.pages[slot.page].view;
    entries[2].binding = 2; entries[2].sampler = m_atlasSampler;

    WGPUBindGroupDescriptor groupDesc{};
    groupDesc.layout = m_texBindLayout;
    groupDesc.entryCount = 3;
    groupDesc.entries = entries;
    obj.bindGroup = wgpuDeviceCreateBindGroup(m_device, &groupDesc);

    updateAtlasTexObjectModel(obj);
    return obj;
}
void GFXDevice::updateAtlasTexObjectModel(TexObject& obj)
{
    QMatrix4x4 model;
    model.translate(obj.x + obj.w / 2.0f, obj.y + obj.h / 2.0f, 0.0f);
    model.scale(obj.w, obj.h, 1.0f);

    TexUniform data{};
    memcpy(data.model, model.constData(), sizeof(data.model));
    data.uvOffset[0] = (float)(obj.slot.slotX * Chunk::SIZE) / AtlasPage::PAGE_SIZE;
    data.uvOffset[1] = (float)(obj.slot.slotY * Chunk::SIZE) / AtlasPage::PAGE_SIZE;
    data.uvScale[0] = (float)Chunk::SIZE / AtlasPage::PAGE_SIZE;
    data.uvScale[1] = (float)Chunk::SIZE / AtlasPage::PAGE_SIZE;

    wgpuQueueWriteBuffer(m_queue, obj.modelBuffer, 0, &data, sizeof(data));
}
void GFXDevice::updateTexObject(TexObject& obj, AtlasSet& atlas, const RGBA* pixels)
{
    writeChunkToAtlas(atlas.pages[obj.slot.page], obj.slot, pixels);
}
void GFXDevice::writeChunkToAtlas(AtlasPage& page, ChunkSlot slot, const RGBA* pixels)
{
    uint32_t pixelX = slot.slotX * Chunk::SIZE;
    uint32_t pixelY = slot.slotY * Chunk::SIZE;

    WGPUTexelCopyTextureInfo dst{};
    dst.texture = page.texture;
    dst.mipLevel = 0;
    dst.origin = {pixelX, pixelY, 0};

    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = Chunk::SIZE * sizeof(RGBA);
    layout.rowsPerImage = Chunk::SIZE;

    WGPUExtent3D writeSize = {Chunk::SIZE, Chunk::SIZE, 1};
    wgpuQueueWriteTexture(m_queue, &dst, pixels, 
                         (size_t)Chunk::SIZE * Chunk::SIZE * sizeof(RGBA), 
                         &layout, &writeSize);
}

// atlas
void GFXDevice::initAtlas()
{
    WGPUSamplerDescriptor samplerDesc{};
    samplerDesc.label = sv("Atlas Sampler");
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    m_atlasSampler = wgpuDeviceCreateSampler(m_device, &samplerDesc);
}
AtlasSet& GFXDevice::atlasForLayer(size_t layerIndex)
{
    if(layerIndex >= m_atlases.size())
        m_atlases.resize(layerIndex + 1);
    return m_atlases[layerIndex];
}
AtlasPage GFXDevice::createAtlasPage()
{
    AtlasPage page{};
    page.slotUsed.assign(AtlasPage::SLOTS_TOTAL, false);

    WGPUTextureDescriptor texDesc{};
    texDesc.label = sv("Atlas Page");
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = { AtlasPage::PAGE_SIZE, AtlasPage::PAGE_SIZE, 1 };
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    page.texture = wgpuDeviceCreateTexture(m_device, &texDesc);
    page.view = wgpuTextureCreateView(page.texture, nullptr);

    return page;
}
ChunkSlot GFXDevice::allocateSlot(AtlasSet& atlas, uint64_t chunkKey)
{
    auto it = atlas.chunkSlots.find(chunkKey);
    if(it != atlas.chunkSlots.end())
        return it->second;

    for(int p = 0; p < (int)atlas.pages.size(); p++)
    {
        AtlasPage& page = atlas.pages[p];
        if(page.isFull()) continue;
        for(int i = 0; i < AtlasPage::SLOTS_TOTAL; i++)
        {
            if(!page.slotUsed[i])
            {
                page.slotUsed[i] = true;
                page.usedSlots++;
                ChunkSlot slot{ p, i % AtlasPage::SLOTS_PER_ROW, i / AtlasPage::SLOTS_PER_ROW };
                atlas.chunkSlots[chunkKey] = slot;
                return slot;
            }
        }
    }

    // new page if no room
    atlas.pages.push_back(createAtlasPage());
    AtlasPage& page = atlas.pages.back();
    page.slotUsed[0] = true;
    page.usedSlots = 1;
    ChunkSlot slot{ (int)atlas.pages.size() - 1, 0, 0 };
    atlas.chunkSlots[chunkKey] = slot;
    return slot;
}
void GFXDevice::freeSlot(AtlasSet& atlas, uint64_t chunkKey)
{
    auto it = atlas.chunkSlots.find(chunkKey);
    if(it == atlas.chunkSlots.end()) return;
    AtlasPage& page = atlas.pages[it->second.page];
    int i = it->second.slotY * AtlasPage::SLOTS_PER_ROW + it->second.slotX;
    page.slotUsed[i] = false;
    page.usedSlots--;
    atlas.chunkSlots.erase(it);
}

// RENDERING
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
    wgpuRenderPassEncoderSetPipeline(pass, m_colPipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_colVertBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, m_indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, cam.m_bindGroup, 0, nullptr);
    for(ColObject& colObj : m_colObjects)
    {
        wgpuRenderPassEncoderSetBindGroup(pass, 1, colObj.bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, 0, 0, 0);
    }

    // texture pass
    wgpuRenderPassEncoderSetPipeline(pass, m_texPipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_texVertBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, cam.m_bindGroup, 0, nullptr);
    for(auto& layerMap : m_chunkTexObjects)
    {
        for(auto& [key, texObj] : layerMap)
        {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, texObj.bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, 0, 0, 0);
        }
    }

    // line pass
    if(m_showBoundsLine && m_lineVertexCount > 0)
    {
        wgpuRenderPassEncoderSetPipeline(pass, m_linePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_lineVertexBuffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, cam.m_screenBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, m_lineColorBindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, m_lineVertexCount, 1, 0, 0);
    }

    // end frame
    wgpuRenderPassEncoderEnd(pass); // end pass
    wgpuRenderPassEncoderRelease(pass); // release pass

    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(m_queue, 1, &cmdBuffer);

    wgpuSurfacePresent(m_surface);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTex.texture);
}