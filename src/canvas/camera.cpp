#include "camera.hpp"
#include "gfx/math.hpp"
#include <algorithm>

namespace {
struct CameraUniform { float viewProj[16]; };
}

bool CanvasCamera::create(const GpuHandles& ctx, WGPUBindGroupLayout layout)
{
    m_ctx = ctx;

    WGPUBufferDescriptor bd{};
    bd.label = sv("Canvas Camera Uniform");
    bd.size  = sizeof(CameraUniform);
    bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_buffer = wgpuDeviceCreateBuffer(ctx.device, &bd);

    WGPUBindGroupEntry ge{};
    ge.binding = 0;
    ge.buffer  = m_buffer;
    ge.size    = sizeof(CameraUniform);

    WGPUBindGroupDescriptor gd{};
    gd.label      = sv("Canvas Camera Bind Group");
    gd.layout     = layout;
    gd.entryCount = 1;
    gd.entries    = &ge;
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &gd);

    return m_buffer && m_bindGroup;
}

void CanvasCamera::release()
{
    if (m_bindGroup) { wgpuBindGroupRelease(m_bindGroup); m_bindGroup = nullptr; }
    if (m_buffer)    { wgpuBufferRelease(m_buffer);       m_buffer    = nullptr; }
}

void CanvasCamera::upload(float viewW, float viewH) const
{
    const Mat4 viewProj = Mat4::ortho(viewW, viewH) * Mat4::translateScale(pan.x, pan.y, zoom, zoom);
    wgpuQueueWriteBuffer(m_ctx.queue, m_buffer, 0, viewProj.m, sizeof(viewProj.m));
}

bool CanvasCamera::zoomAt(Vec2 screen, float factor)
{
    const float newZoom = std::clamp(zoom * factor, kMinZoom, kMaxZoom);
    if (newZoom == zoom) return false;
    const float k = newZoom / zoom;
    pan.x = screen.x + (pan.x - screen.x) * k;
    pan.y = screen.y + (pan.y - screen.y) * k;
    zoom  = newZoom;
    return true;
}

void CanvasCamera::frame(float minX, float minY, float maxX, float maxY, float viewW, float viewH)
{
    if (viewW <= 0.0f || viewH <= 0.0f) return;
    constexpr float kMargin = 0.9f;   // of the view the rectangle fills
    const float w = std::max(maxX - minX, 1.0f), h = std::max(maxY - minY, 1.0f);
    zoom = std::clamp(std::min(viewW / w, viewH / h) * kMargin, kMinZoom, kMaxZoom);
    centerOn((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, viewW, viewH);
}
