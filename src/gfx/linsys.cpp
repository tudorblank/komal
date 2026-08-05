#include "linsys.hpp"

void LinSys::createRenderPipeline(WGPUBindGroupLayout screenCamLayout)
{
    std::string src = readFile("shaders/line.wgsl");
    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = sv(src.c_str());
    WGPUShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = &wgsl.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(m_ctx.device, &shaderDesc);

    WGPUBindGroupLayoutEntry colorEntry{};
    colorEntry.binding = 0;
    colorEntry.visibility = WGPUShaderStage_Fragment;
    colorEntry.buffer.type = WGPUBufferBindingType_Uniform;
    colorEntry.buffer.minBindingSize = sizeof(float) * 4;

    WGPUBindGroupLayoutDescriptor colorLayoutDesc{};
    colorLayoutDesc.entryCount = 1;
    colorLayoutDesc.entries = &colorEntry;
    WGPUBindGroupLayout lineBindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_ctx.device, &colorLayoutDesc);

    WGPUVertexAttribute attrib{};
    attrib.shaderLocation = 0;
    attrib.offset = 0;
    attrib.format = WGPUVertexFormat_Float32x2;

    WGPUVertexBufferLayout vbLayout{};
    vbLayout.arrayStride = sizeof(float) * 2;
    vbLayout.attributeCount = 1;
    vbLayout.attributes = &attrib;

    WGPUVertexState vertState{};
    vertState.module = shaderModule;
    vertState.entryPoint = sv("vs_main");
    vertState.bufferCount = 1;
    vertState.buffers = &vbLayout;

    WGPUColorTargetState colorTarget{};
    colorTarget.format = m_ctx.surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragState{};
    fragState.module = shaderModule;
    fragState.entryPoint = sv("fs_main");
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPUPrimitiveState primState{};
    primState.topology = WGPUPrimitiveTopology_LineList;

    WGPUBindGroupLayout bgLayouts[2] = { screenCamLayout, lineBindGroupLayout };
    WGPUPipelineLayoutDescriptor plLayoutDesc{};
    plLayoutDesc.bindGroupLayoutCount = 2;
    plLayoutDesc.bindGroupLayouts = bgLayouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_ctx.device, &plLayoutDesc);

    WGPURenderPipelineDescriptor plDesc{};
    plDesc.vertex = vertState;
    plDesc.fragment = &fragState;
    plDesc.primitive = primState;
    plDesc.layout = pipelineLayout;
    plDesc.multisample.count = 1;
    plDesc.multisample.mask = 0xFFFFFFFF;
    m_pipeline = wgpuDeviceCreateRenderPipeline(m_ctx.device, &plDesc);

    wgpuShaderModuleRelease(shaderModule);
    shaderModule = nullptr;
    wgpuPipelineLayoutRelease(pipelineLayout);
    pipelineLayout = nullptr;

    // color bind group
    WGPUBufferDescriptor colorBufDesc{};
    colorBufDesc.size = sizeof(float) * 4;
    colorBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer colorBuf = wgpuDeviceCreateBuffer(m_ctx.device, &colorBufDesc);
    float col[4] = { 0.18f, 0.4f, 1.0f, 1.0f };
    wgpuQueueWriteBuffer(m_ctx.queue, colorBuf, 0, col, sizeof(col));

    WGPUBindGroupEntry colorEntry2{};
    colorEntry2.binding = 0;
    colorEntry2.buffer = colorBuf;
    colorEntry2.size = sizeof(col);

    WGPUBindGroupDescriptor colorGroupDesc{};
    colorGroupDesc.layout = lineBindGroupLayout;
    colorGroupDesc.entryCount = 1;
    colorGroupDesc.entries = &colorEntry2;
    m_bindGroup = wgpuDeviceCreateBindGroup(m_ctx.device, &colorGroupDesc);

    wgpuBufferRelease(colorBuf);
    colorBuf = nullptr;
    wgpuBindGroupLayoutRelease(lineBindGroupLayout);
    lineBindGroupLayout = nullptr;
}

void LinSys::updateBoundsLines(const std::vector<BoundsI>& boxes, Vec2 pan, float zoom)
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

    m_vertCount = (uint32_t)(verts.size() / 2);
    if(m_vertCount == 0) return;

    // vertex buffer size
    if(m_vertCount > m_vertBuffCapacity)
    {
        if(m_vertBuff) wgpuBufferRelease(m_vertBuff); // remove previous vertex buffer
        
        // rebuild vertex buffer
        m_vertBuffCapacity = m_vertCount;
        WGPUBufferDescriptor vbDesc{};
        vbDesc.label = sv("Bounds Line Vertex Buffer");
        vbDesc.size = sizeof(float) * 2 * m_vertBuffCapacity;
        vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_vertBuff = wgpuDeviceCreateBuffer(m_ctx.device, &vbDesc);
    }

    wgpuQueueWriteBuffer(m_ctx.queue, m_vertBuff, 0, verts.data(), verts.size() * sizeof(float));
}

void LinSys::releaseAll()
{
    if(m_vertBuff){
        wgpuBufferRelease(m_vertBuff);
        m_vertBuff = nullptr;
    }
    if(m_pipeline){
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if(m_bindGroup){
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
    }
}