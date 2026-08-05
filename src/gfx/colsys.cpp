#include "colsys.hpp"

void ColSys::createRenderPipeline(WGPUBindGroupLayout camLayout)
{
    // geometry
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
    m_vertBuff = wgpuDeviceCreateBuffer(m_ctx.device, &colVertDesc);
    wgpuQueueWriteBuffer(m_ctx.queue, m_vertBuff, 0, vertsColor, sizeof(vertsColor));

    // shader module
    std::string colSource = readFile("shaders/color.wgsl");
    WGPUShaderSourceWGSL colWgslDesc{};
    colWgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    colWgslDesc.code = sv(colSource.c_str());

    WGPUShaderModuleDescriptor colShaderDesc{};
    colShaderDesc.nextInChain = &colWgslDesc.chain;
    WGPUShaderModule colShaderModule = wgpuDeviceCreateShaderModule(m_ctx.device, &colShaderDesc);
    if (!colShaderModule) printf("Color shader failed\n");

    // bind group layout
    WGPUBindGroupLayoutEntry layoutEntry{};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.minBindingSize = sizeof(ColUniform);

    WGPUBindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.label = sv("Color Bind Group Layout");
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_ctx.device, &layoutDesc);

    // pipeline
    //// vertex
    WGPUVertexState vertState{};
    vertState.module = colShaderModule;
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
    fragState.module = colShaderModule;
    fragState.entryPoint = sv("fs_main");

    WGPUColorTargetState colorTarget{};
    colorTarget.format = m_ctx.surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = WGPUFrontFace_CCW;
    primitiveState.cullMode = WGPUCullMode_None;

    // pipeline layout
    WGPUBindGroupLayout bgLayouts[2] = { camLayout, m_bindGroupLayout };
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.label = sv("Color Pipeline Layout");
    pipelineLayoutDesc.bindGroupLayoutCount = 2;
    pipelineLayoutDesc.bindGroupLayouts = bgLayouts;
    WGPUPipelineLayout colPipelineLayout = wgpuDeviceCreatePipelineLayout(m_ctx.device, &pipelineLayoutDesc);

    WGPURenderPipelineDescriptor PLDesc{};
    PLDesc.vertex = vertState;
    PLDesc.fragment = &fragState;
    PLDesc.primitive = primitiveState;
    PLDesc.layout = colPipelineLayout;
    PLDesc.multisample.count = 1;
    PLDesc.multisample.mask = 0xFFFFFFFF;

    m_pipeline = wgpuDeviceCreateRenderPipeline(m_ctx.device, &PLDesc);

    wgpuPipelineLayoutRelease(colPipelineLayout);
    wgpuShaderModuleRelease(colShaderModule);
}

// objects
ColObject& ColSys::addColObject(float x, float y, float w, float h, float r, float g, float b, float a)
{
    ColObject obj{};
    obj.x = x; obj.y = y; obj.w = w; obj.h = h;
    obj.color[0] = r; obj.color[1] = g; obj.color[2] = b; obj.color[3] = a;

    WGPUBufferDescriptor buffDesc{};
    buffDesc.label = sv("Color Object Uniform Buffer");
    buffDesc.size = sizeof(ColUniform);
    buffDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    obj.buffer = wgpuDeviceCreateBuffer(m_ctx.device, &buffDesc);

    WGPUBindGroupEntry bgEntry{};
    bgEntry.binding = 0;
    bgEntry.buffer = obj.buffer;
    bgEntry.offset = 0;
    bgEntry.size = sizeof(ColUniform);

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.label = sv("Color Object Bind Group");
    bgDesc.layout = m_bindGroupLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    obj.bindGroup = wgpuDeviceCreateBindGroup(m_ctx.device, &bgDesc);

    m_colObjects.push_back(obj);
    updateColObjectMVP(m_colObjects.back());
    return m_colObjects.back();
}
void ColSys::updateColObjectMVP(ColObject& obj)
{
    QMatrix4x4 model;
    model.translate(obj.x + obj.w / 2.0f, obj.y + obj.h / 2.0f, 0.0f);
    model.scale(obj.w, obj.h, 1.0f);

    ColUniform data{};
    memcpy(data.model, model.constData(), sizeof(data.model));
    memcpy(data.color, obj.color, sizeof(data.color));
    wgpuQueueWriteBuffer(m_ctx.queue, obj.buffer, 0, &data, sizeof(data));
}

void ColSys::releaseAll()
{
    for(auto& colObj : m_colObjects)
        {
            if(colObj.bindGroup)    wgpuBindGroupRelease(colObj.bindGroup);
            if(colObj.buffer)       wgpuBufferRelease(colObj.buffer);
        }
    m_colObjects.clear();

    if(m_pipeline){
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if(m_bindGroupLayout){
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if(m_vertBuff){
        wgpuBufferRelease(m_vertBuff);
        m_vertBuff = nullptr;
    }
}