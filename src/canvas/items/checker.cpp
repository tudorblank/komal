#include "checker.hpp"

#include <cmath>
#include <cstring>

void CheckerItem::setColors(const float a[3], const float b[3])
{
    std::memcpy(m_colorA, a, sizeof m_colorA);
    std::memcpy(m_colorB, b, sizeof m_colorB);
    m_dirty = true;
    update();
}

void CheckerItem::setCellSize(float points)
{
    if (points == m_cell || points <= 0.0f) return;
    m_cell = points;
    m_dirty = true;
    update();
}

bool CheckerItem::prepare(CanvasFrame& frame)
{
    const CanvasPipelines& p = frame.pipelines;
    if (!m_buffer) {   // GPU resources on first use
        WGPUBufferDescriptor bd{};
        bd.label = sv("Checker Uniform");
        bd.size  = sizeof(CheckerParams);
        bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        m_buffer = wgpuDeviceCreateBuffer(p.ctx.device, &bd);

        WGPUBindGroupEntry e{};
        e.binding = 0;
        e.buffer  = m_buffer;
        e.size    = sizeof(CheckerParams);
        WGPUBindGroupDescriptor gd{};
        gd.label      = sv("Checker Group");
        gd.layout     = p.checkerLayout;
        gd.entryCount = 1;
        gd.entries    = &e;
        m_group = wgpuDeviceCreateBindGroup(p.ctx.device, &gd);
        m_dirty = true;
    }

    // Screen-space squares: the canvas origin and the square size, in the target's pixels.
    const float pixelsPerPoint = frame.target.viewW > 0.0f ? float(frame.target.pixelW) / frame.target.viewW : 1.0f;
    CheckerParams u{};
    for (int i = 0; i < 3; ++i) { u.colorA[i] = m_colorA[i]; u.colorB[i] = m_colorB[i]; }
    u.colorA[3] = u.colorB[3] = 1.0f;
    u.origin[0] = frame.camera.pan.x * pixelsPerPoint;
    u.origin[1] = frame.camera.pan.y * pixelsPerPoint;
    u.cell      = std::max(1.0f, std::round(m_cell * pixelsPerPoint));
    if (!m_dirty && std::memcmp(&u, &m_uploaded, sizeof u) == 0) return false;
    wgpuQueueWriteBuffer(p.ctx.queue, m_buffer, 0, &u, sizeof u);
    m_uploaded = u;
    // A camera move re-renders the view anyway; only a change of its own needs one.
    const bool own = m_dirty;
    m_dirty = false;
    return own;
}

void CheckerItem::drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame)
{
    if (!m_group) return;
    frame.bindOwn(pass, frame.pipelines.checkerPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
}

void CheckerItem::releaseGpu()
{
    if (m_group)  { wgpuBindGroupRelease(m_group); m_group  = nullptr; }
    if (m_buffer) { wgpuBufferRelease(m_buffer);   m_buffer = nullptr; }
}
