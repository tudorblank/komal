#include "camera.hpp"

// world
void Camera::create()
{
    QMatrix4x4 identity;
    CameraUniform camInput{};
    memcpy(camInput.viewProj, identity.constData(), sizeof(camInput.viewProj));

    WGPUBufferDescriptor camDesc{};
    camDesc.label = sv("Camera Uniform Buffer");
    camDesc.size = sizeof(CameraUniform);
    camDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_buffer = wgpuDeviceCreateBuffer(m_ctx.device, &camDesc);
    wgpuQueueWriteBuffer(m_ctx.queue, m_buffer, 0, camInput.viewProj, sizeof(CameraUniform));

    WGPUBindGroupLayoutEntry camLayoutEntry{};
    camLayoutEntry.binding = 0;
    camLayoutEntry.visibility = WGPUShaderStage_Vertex;
    camLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    camLayoutEntry.buffer.minBindingSize = sizeof(CameraUniform);

    WGPUBindGroupLayoutDescriptor camLayoutDesc{};
    camLayoutDesc.label = sv("Camera Bind Group Layout");
    camLayoutDesc.entryCount = 1;
    camLayoutDesc.entries = &camLayoutEntry;
    m_bindLayout = wgpuDeviceCreateBindGroupLayout(m_ctx.device, &camLayoutDesc);

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
    m_bindGroup = wgpuDeviceCreateBindGroup(m_ctx.device, &camGroupDesc);
}
void Camera::update(float screenW, float screenH)
{
    QMatrix4x4 view;
    view.translate(pan.x, pan.y, 0.0f);
    view.scale(zoom, zoom, 1.0f);

    QMatrix4x4 projection;
    projection.ortho(0.0f, screenW, screenH, 0.0f, -1.0f, 1.0f);

    QMatrix4x4 viewProj = projection * view;

    CameraUniform data{};
    memcpy(data.viewProj, viewProj.constData(), sizeof(data.viewProj));
    wgpuQueueWriteBuffer(m_ctx.queue, m_buffer, 0, data.viewProj, sizeof(data.viewProj));
}

// screen

void Camera::createScreen(uint32_t width, uint32_t height)
{
    QMatrix4x4 proj;
    proj.ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);

    struct { float proj[16]; } data{};
    memcpy(data.proj, proj.constData(), sizeof(data.proj));

    WGPUBufferDescriptor desc{};
    desc.label = sv("Screen Camera Buffer");
    desc.size = sizeof(data);
    desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_screenBuffer = wgpuDeviceCreateBuffer(m_ctx.device, &desc);
    wgpuQueueWriteBuffer(m_ctx.queue, m_screenBuffer, 0, data.proj, sizeof(data));

    WGPUBindGroupLayoutEntry entry{};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.minBindingSize = sizeof(data);

    WGPUBindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &entry;
    m_screenBindLayout = wgpuDeviceCreateBindGroupLayout(m_ctx.device, &layoutDesc);

    WGPUBindGroupEntry gEntry{};
    gEntry.binding = 0;
    gEntry.buffer = m_screenBuffer;
    gEntry.size = sizeof(data);

    WGPUBindGroupDescriptor gDesc{};
    gDesc.layout = m_screenBindLayout;
    gDesc.entryCount = 1;
    gDesc.entries = &gEntry;
    m_screenBindGroup = wgpuDeviceCreateBindGroup(m_ctx.device, &gDesc);
}
void Camera::updateScreen(uint32_t width, uint32_t height)
{
    QMatrix4x4 proj;
    proj.ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);

    struct { float proj[16]; } data{};
    memcpy(data.proj, proj.constData(), sizeof(data.proj));
    wgpuQueueWriteBuffer(m_ctx.queue, m_screenBuffer, 0, data.proj, sizeof(data));
}