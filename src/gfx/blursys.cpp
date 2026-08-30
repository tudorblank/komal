#include "blursys.hpp"

void BlurSys::createComputePipeline()
{
    std::string blurSource = readFile("shaders/blur.wgsl");
    WGPUShaderSourceWGSL wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = sv(blurSource.c_str());
    WGPUShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(m_ctx.device, &shaderDesc);
    if(!shaderModule) printf("Blur shader failed\n");

    WGPUBindGroupLayoutEntry entries[3]{};

    entries[0].binding = 0; // input sampled texture (padded)
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[1].binding = 1; // output storage texture (CHUNK_SIZE)
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[1].storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
    entries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2; // params uniform
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].buffer.type = WGPUBufferBindingType_Uniform;
    entries[2].buffer.minBindingSize = sizeof(BlurParams);

    WGPUBindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.label = sv("Blur Bind Group Layout");
    layoutDesc.entryCount = 3;
    layoutDesc.entries = entries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_ctx.device, &layoutDesc);

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.label = sv("Blur Pipeline Layout");
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_ctx.device, &pipelineLayoutDesc);

    WGPUComputePipelineDescriptor pipelineDesc{};
    pipelineDesc.label = sv("Blur Compute Pipeline");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = shaderModule;
    pipelineDesc.compute.entryPoint = sv("cs_main");

    m_pipeline = wgpuDeviceCreateComputePipeline(m_ctx.device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void BlurSys::blurTile(const RGBA* paddedPixels, int paddedSize, int radius, RGBA* outPixels)
{
    // input texture (padded, sampled)
    WGPUTextureDescriptor inDesc{};
    inDesc.label = sv("Blur Input Texture");
    inDesc.dimension = WGPUTextureDimension_2D;
    inDesc.size = { (uint32_t)paddedSize, (uint32_t)paddedSize, 1 };
    inDesc.format = WGPUTextureFormat_RGBA8Unorm;
    inDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    inDesc.mipLevelCount = 1;
    inDesc.sampleCount = 1;
    WGPUTexture inputTex = wgpuDeviceCreateTexture(m_ctx.device, &inDesc);
    WGPUTextureView inputView = wgpuTextureCreateView(inputTex, nullptr);

    WGPUTexelCopyTextureInfo inDst{};
    inDst.texture = inputTex;
    inDst.origin = {0, 0, 0};

    WGPUTexelCopyBufferLayout inLayout{};
    inLayout.offset = 0;
    inLayout.bytesPerRow = (uint32_t)paddedSize * sizeof(RGBA);
    inLayout.rowsPerImage = (uint32_t)paddedSize;

    WGPUExtent3D inWriteSize = { (uint32_t)paddedSize, (uint32_t)paddedSize, 1 };
    wgpuQueueWriteTexture(m_ctx.queue, &inDst, paddedPixels,
                           (size_t)paddedSize * paddedSize * sizeof(RGBA),
                           &inLayout, &inWriteSize);

    // output texture
    WGPUTextureDescriptor outDesc{};
    outDesc.label = sv("Blur Output Texture");
    outDesc.dimension = WGPUTextureDimension_2D;
    outDesc.size = { (uint32_t)Grid::CHUNK_SIZE, (uint32_t)Grid::CHUNK_SIZE, 1 };
    outDesc.format = WGPUTextureFormat_RGBA8Unorm;
    outDesc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
    outDesc.mipLevelCount = 1;
    outDesc.sampleCount = 1;
    WGPUTexture outputTex = wgpuDeviceCreateTexture(m_ctx.device, &outDesc);
    WGPUTextureView outputView = wgpuTextureCreateView(outputTex, nullptr);

    // params uniform
    BlurParams params{ radius };
    WGPUBufferDescriptor paramsDesc{};
    paramsDesc.label = sv("Blur Params");
    paramsDesc.size = sizeof(BlurParams);
    paramsDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer paramsBuffer = wgpuDeviceCreateBuffer(m_ctx.device, &paramsDesc);
    wgpuQueueWriteBuffer(m_ctx.queue, paramsBuffer, 0, &params, sizeof(BlurParams));

    // bind group
    WGPUBindGroupEntry bgEntries[3]{};
    bgEntries[0].binding = 0; bgEntries[0].textureView = inputView;
    bgEntries[1].binding = 1; bgEntries[1].textureView = outputView;
    bgEntries[2].binding = 2; bgEntries[2].buffer = paramsBuffer; bgEntries[2].size = sizeof(BlurParams);

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.label = sv("Blur Bind Group");
    bgDesc.layout = m_bindGroupLayout;
    bgDesc.entryCount = 3;
    bgDesc.entries = bgEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(m_ctx.device, &bgDesc);

    // readback buffer
    size_t readbackSize = (size_t)Grid::CHUNK_SIZE * Grid::CHUNK_SIZE * sizeof(RGBA);
    WGPUBufferDescriptor readDesc{};
    readDesc.label = sv("Blur Readback Buffer");
    readDesc.size = readbackSize;
    readDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(m_ctx.device, &readDesc);

    // encode: dispatch + copy-to-buffer
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_ctx.device, nullptr);

    WGPUComputePassDescriptor passDesc{};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, m_pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

    uint32_t groups = (Grid::CHUNK_SIZE + 7) / 8; // workgroup_size(8,8,1) → 64/8 = 8
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups, groups, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    WGPUTexelCopyTextureInfo copySrc{};
    copySrc.texture = outputTex;
    copySrc.origin = {0, 0, 0};

    WGPUTexelCopyBufferInfo copyDst{};
    copyDst.buffer = readbackBuffer;
    copyDst.layout.offset = 0;
    copyDst.layout.bytesPerRow = Grid::CHUNK_SIZE * sizeof(RGBA);
    copyDst.layout.rowsPerImage = Grid::CHUNK_SIZE;

    WGPUExtent3D copySize = { (uint32_t)Grid::CHUNK_SIZE, (uint32_t)Grid::CHUNK_SIZE, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &copySrc, &copyDst, &copySize);

    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(m_ctx.queue, 1, &cmdBuffer);

    // blocking map
    struct MapResult { bool done = false; } mapResult;
    WGPUBufferMapCallbackInfo mapCbInfo{};
    mapCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    mapCbInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void*) {
        auto* result = (MapResult*)userdata1;
        if(status != WGPUMapAsyncStatus_Success)
            fprintf(stderr, "Blur readback map failed: %.*s\n", (int)message.length, message.data);
        result->done = true;
    };
    mapCbInfo.userdata1 = &mapResult;

    wgpuBufferMapAsync(readbackBuffer, WGPUMapMode_Read, 0, readbackSize, mapCbInfo);

    while(!mapResult.done)
        wgpuInstanceProcessEvents(m_ctx.instance);

    const void* mapped = wgpuBufferGetConstMappedRange(readbackBuffer, 0, readbackSize);
    memcpy(outPixels, mapped, readbackSize);
    wgpuBufferUnmap(readbackBuffer);

    // cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuBufferRelease(readbackBuffer);
    wgpuBindGroupRelease(bindGroup);
    wgpuBufferRelease(paramsBuffer);
    wgpuTextureViewRelease(outputView);
    wgpuTextureRelease(outputTex);
    wgpuTextureViewRelease(inputView);
    wgpuTextureRelease(inputTex);
}

void BlurSys::releaseAll()
{
    if(m_pipeline)         { wgpuComputePipelineRelease(m_pipeline); m_pipeline = nullptr; }
    if(m_bindGroupLayout)  { wgpuBindGroupLayoutRelease(m_bindGroupLayout); m_bindGroupLayout = nullptr; }
}