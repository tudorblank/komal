#include "texsys.hpp"

void TexSys::createRenderPipeline(WGPUBindGroupLayout camLayout)
{
    // geometry
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
    m_vertBuff = wgpuDeviceCreateBuffer(m_ctx.device, &texVertDesc);
    wgpuQueueWriteBuffer(m_ctx.queue, m_vertBuff, 0, vertsTex, sizeof(vertsTex));

    // shader module
    std::string texSource = readFile("shaders/texture.wgsl");

    WGPUShaderSourceWGSL texWgslDesc{};
    texWgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    texWgslDesc.code = sv(texSource.c_str());

    WGPUShaderModuleDescriptor texShaderDesc{};
    texShaderDesc.nextInChain = &texWgslDesc.chain;
    WGPUShaderModule texShaderModule = wgpuDeviceCreateShaderModule(m_ctx.device, &texShaderDesc);
    if(!texShaderModule) printf("Texture shader failed\n");

    // bind group layout
    WGPUBindGroupLayoutEntry entries[3]{};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(ChunkUniform);

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
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_ctx.device, &layoutDesc);

    // pipeline
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
    vertState.module = texShaderModule;
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
    colorTarget.format = m_ctx.surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    colorTarget.blend = &blendState;

    WGPUFragmentState fragState{};
    fragState.module = texShaderModule;
    fragState.entryPoint = sv("fs_main");
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = WGPUFrontFace_CCW;
    primitiveState.cullMode = WGPUCullMode_None;

    // pipeline layout
    WGPUBindGroupLayout bgLayouts[2] = { camLayout, m_bindGroupLayout };
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.label = sv("Texture Pipeline Layout");
    pipelineLayoutDesc.bindGroupLayoutCount = 2;
    pipelineLayoutDesc.bindGroupLayouts = bgLayouts;
    WGPUPipelineLayout texPipelineLayout = wgpuDeviceCreatePipelineLayout(m_ctx.device, &pipelineLayoutDesc);

    WGPURenderPipelineDescriptor PLDesc{};
    PLDesc.vertex = vertState;
    PLDesc.fragment = &fragState;
    PLDesc.primitive = primitiveState;
    PLDesc.layout = texPipelineLayout;
    PLDesc.multisample.count = 1;
    PLDesc.multisample.mask = 0xFFFFFFFF;

    m_pipeline = wgpuDeviceCreateRenderPipeline(m_ctx.device, &PLDesc);
    wgpuPipelineLayoutRelease(texPipelineLayout);
    wgpuShaderModuleRelease(texShaderModule);
}
// chunk
ChunkObject TexSys::registerChunk(AtlasSet& atlas, uint64_t chunkKey,
                                    float x, float y, float w, float h,
                                    const RGBA* pixels)
{
    ChunkObject obj{};
    obj.x = x; obj.y = y; obj.w = w; obj.h = h;

    ChunkSlot slot = allocateSlot(atlas, chunkKey);
    writeChunkToAtlas(atlas.pages[slot.page], slot, pixels);

    WGPUBufferDescriptor modelDesc{};
    modelDesc.label = sv("Chunk Model Buffer");
    modelDesc.size = sizeof(ChunkUniform);
    modelDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    obj.modelBuffer = wgpuDeviceCreateBuffer(m_ctx.device, &modelDesc);

    WGPUBindGroupEntry entries[3]{};
    entries[0].binding = 0; entries[0].buffer = obj.modelBuffer; entries[0].size = sizeof(ChunkUniform);
    entries[1].binding = 1; entries[1].textureView = atlas.pages[slot.page].view;
    entries[2].binding = 2; entries[2].sampler = m_atlasSampler;

    WGPUBindGroupDescriptor groupDesc{};
    groupDesc.layout = m_bindGroupLayout;
    groupDesc.entryCount = 3;
    groupDesc.entries = entries;
    obj.bindGroup = wgpuDeviceCreateBindGroup(m_ctx.device, &groupDesc);

    // update chunk uniform
    QMatrix4x4 model;
    model.translate(obj.x + obj.w / 2.0f, obj.y + obj.h / 2.0f, 0.0f);
    model.scale(obj.w, obj.h, 1.0f);

    ChunkUniform data{};
    memcpy(data.model, model.constData(), sizeof(data.model));
    data.uvOffset[0] = (float)(slot.slotX * Chunk::SIZE) / AtlasPage::PAGE_SIZE;
    data.uvOffset[1] = (float)(slot.slotY * Chunk::SIZE) / AtlasPage::PAGE_SIZE;
    data.uvScale[0] = (float)Chunk::SIZE / AtlasPage::PAGE_SIZE;
    data.uvScale[1] = (float)Chunk::SIZE / AtlasPage::PAGE_SIZE;

    wgpuQueueWriteBuffer(m_ctx.queue, obj.modelBuffer, 0, &data, sizeof(data));

    return obj;
}
void TexSys::updateChunkObject(uint64_t chunkKey, ChunkObject& obj, AtlasSet& atlas, const RGBA* pixels)
{
    const ChunkSlot& slot = atlas.chunkSlots.at(chunkKey);
    writeChunkToAtlas(atlas.pages[slot.page], slot, pixels);
}
void TexSys::writeChunkToAtlas(AtlasPage& page, ChunkSlot slot, const RGBA* pixels)
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
    wgpuQueueWriteTexture(m_ctx.queue, &dst, pixels, 
                            (size_t)Chunk::SIZE * Chunk::SIZE * sizeof(RGBA), 
                            &layout, &writeSize);
}
void TexSys::syncChunk(size_t layerIndex, uint64_t chunkKey, float worldX, float worldY, const RGBA* pixels)
{
    if(layerIndex >= m_chunkObjects.size())
        m_chunkObjects.resize(layerIndex + 1);

    auto& texMap = m_chunkObjects[layerIndex];
    AtlasSet& atlas = atlasForLayer(layerIndex);

    auto it = texMap.find(chunkKey);
    if(it == texMap.end())
        texMap.emplace(chunkKey,
            registerChunk(atlas, chunkKey, worldX, worldY, (float)Chunk::SIZE, (float)Chunk::SIZE, pixels));
    else
        updateChunkObject(chunkKey, it->second, atlas, pixels);
}

// atlas
void TexSys::initAtlasSampler()
{
    WGPUSamplerDescriptor samplerDesc{};
    samplerDesc.label = sv("Atlas Sampler");
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    m_atlasSampler = wgpuDeviceCreateSampler(m_ctx.device, &samplerDesc);
}
AtlasSet& TexSys::atlasForLayer(size_t layerIndex)
{
    if(layerIndex >= m_atlases.size())
        m_atlases.resize(layerIndex + 1);
    return m_atlases[layerIndex];
}

AtlasPage TexSys::createAtlasPage()
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
    page.texture = wgpuDeviceCreateTexture(m_ctx.device, &texDesc);
    page.view = wgpuTextureCreateView(page.texture, nullptr);

    return page;
}
ChunkSlot TexSys::allocateSlot(AtlasSet& atlas, uint64_t chunkKey)
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
void TexSys::freeSlot(AtlasSet& atlas, uint64_t chunkKey)
{
    auto it = atlas.chunkSlots.find(chunkKey);
    if(it == atlas.chunkSlots.end()) return;
    AtlasPage& page = atlas.pages[it->second.page];
    int i = it->second.slotY * AtlasPage::SLOTS_PER_ROW + it->second.slotX;
    page.slotUsed[i] = false;
    page.usedSlots--;
    atlas.chunkSlots.erase(it);
}

// destructor
void TexSys::releaseAll()
{
    for(auto& layerMap : m_chunkObjects)
        for(auto& [key, texObj] : layerMap)
        {
            if(texObj.bindGroup)   wgpuBindGroupRelease(texObj.bindGroup);
            if(texObj.modelBuffer) wgpuBufferRelease(texObj.modelBuffer);
        }
    m_chunkObjects.clear();

    for(auto& atlas : m_atlases)
        for(auto& page : atlas.pages)
        {
            if(page.view)    wgpuTextureViewRelease(page.view);
            if(page.texture) wgpuTextureRelease(page.texture);
        }
    m_atlases.clear();

    if(m_atlasSampler){
        wgpuSamplerRelease(m_atlasSampler);
        m_atlasSampler = nullptr;
    }
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